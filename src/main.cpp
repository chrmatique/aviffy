#include "convert.hpp"
#include "scan.hpp"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <getopt.h>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>

namespace {

namespace fs = std::filesystem;

constexpr const char* kVersion = "1.0.0";

enum LongOnly {
    OPT_FORCE = 1000,
    OPT_SKIP_EXISTING,
    OPT_DRY_RUN,
    OPT_QUIET,
};

const struct option kLongOptions[] = {
    {"output", required_argument, nullptr, 'o'},
    {"recursive", no_argument, nullptr, 'r'},
    {"quality", required_argument, nullptr, 'q'},
    {"threads", required_argument, nullptr, 'j'},
    {"force", no_argument, nullptr, OPT_FORCE},
    {"skip-existing", no_argument, nullptr, OPT_SKIP_EXISTING},
    {"dry-run", no_argument, nullptr, OPT_DRY_RUN},
    {"quiet", no_argument, nullptr, OPT_QUIET},
    {"verbose", no_argument, nullptr, 'v'},
    {"help", no_argument, nullptr, 'h'},
    {"version", no_argument, nullptr, 'V'},
    {nullptr, 0, nullptr, 0},
};

void printUsage(std::ostream& os, const char* argv0) {
    os << "aviffy " << kVersion << " - convert .avif images to .jpg\n\n"
       << "Usage: " << argv0 << " [OPTIONS] INPUT...\n\n"
       << "INPUT may be one or more .avif files and/or directories containing .avif files.\n"
       << "With no output option, each .jpg is written next to its source .avif.\n\n"
       << "Options:\n"
       << "  -o, --output DIR     Output directory. The output tree mirrors the input tree\n"
       << "                       (relative sub-paths are preserved).\n"
       << "  -r, --recursive      Recurse into input directories.\n"
       << "  -q, --quality N      JPEG quality, 1-100 (default 90).\n"
       << "  -j, --threads N      Number of worker threads (default: number of CPU cores).\n"
       << "      --force          Overwrite existing .jpg files.\n"
       << "      --skip-existing  Skip files whose .jpg already exists (default).\n"
       << "      --dry-run        Show planned conversions without writing anything.\n"
       << "  -v, --verbose        Print extra detail.\n"
       << "      --quiet          Print only the final summary (errors still shown).\n"
       << "  -h, --help           Show this help and exit.\n"
       << "  -V, --version        Show version and exit.\n";
}

bool parsePositiveInt(const char* text, int minValue, int maxValue, int& out) {
    if (text == nullptr || *text == '\0') {
        return false;
    }
    char* end = nullptr;
    const long value = std::strtol(text, &end, 10);
    if (end == text || *end != '\0' || value < minValue || value > maxValue) {
        return false;
    }
    out = static_cast<int>(value);
    return true;
}

unsigned int defaultThreadCount() {
    unsigned int n = std::thread::hardware_concurrency();
    return n == 0 ? 1u : n;
}

} // namespace

int main(int argc, char** argv) {
    std::string outputDir;
    bool recursive = false;
    bool force = false;
    bool dryRun = false;
    bool verbose = false;
    bool quiet = false;
    int quality = 90;
    int threadCount = static_cast<int>(defaultThreadCount());

    optind = 1;
    int opt = 0;
    while ((opt = getopt_long(argc, argv, "o:rq:j:vhV", kLongOptions, nullptr)) != -1) {
        switch (opt) {
        case 'o':
            outputDir = optarg;
            break;
        case 'r':
            recursive = true;
            break;
        case 'q':
            if (!parsePositiveInt(optarg, 1, 100, quality)) {
                std::cerr << "aviffy: --quality must be an integer in 1..100\n";
                return 2;
            }
            break;
        case 'j':
            if (!parsePositiveInt(optarg, 1, 4096, threadCount)) {
                std::cerr << "aviffy: --threads must be a positive integer\n";
                return 2;
            }
            break;
        case OPT_FORCE:
            force = true;
            break;
        case OPT_SKIP_EXISTING:
            force = false;
            break;
        case OPT_DRY_RUN:
            dryRun = true;
            break;
        case OPT_QUIET:
            quiet = true;
            break;
        case 'v':
            verbose = true;
            break;
        case 'h':
            printUsage(std::cout, argv[0]);
            return 0;
        case 'V':
            std::cout << "aviffy " << kVersion << "\n";
            return 0;
        default:
            printUsage(std::cerr, argv[0]);
            return 2;
        }
    }

    std::vector<std::string> inputs;
    for (int i = optind; i < argc; ++i) {
        inputs.emplace_back(argv[i]);
    }
    if (inputs.empty()) {
        std::cerr << "aviffy: no input given\n\n";
        printUsage(std::cerr, argv[0]);
        return 2;
    }

    std::vector<std::string> scanErrors;
    std::vector<aviffy::Job> jobs = aviffy::planJobs(inputs, outputDir, recursive, scanErrors);
    for (const std::string& e : scanErrors) {
        std::cerr << "aviffy: " << e << "\n";
    }

    if (jobs.empty()) {
        std::cerr << "aviffy: no .avif files to convert\n";
        return 1;
    }

    if (dryRun) {
        for (const auto& job : jobs) {
            std::cout << job.src << " -> " << job.dst << "\n";
        }
        std::cout << jobs.size() << " file(s) would be converted (dry run)\n";
        return 0;
    }

    // Create every distinct output directory once, up front (single-threaded).
    {
        std::set<std::string> dirs;
        for (const auto& job : jobs) {
            const fs::path parent = fs::path(job.dst).parent_path();
            if (!parent.empty()) {
                dirs.insert(parent.string());
            }
        }
        for (const auto& dir : dirs) {
            std::error_code ec;
            fs::create_directories(dir, ec);
            if (ec && !fs::is_directory(dir)) {
                std::cerr << "aviffy: cannot create output directory '" << dir
                          << "': " << ec.message() << "\n";
            }
        }
    }

    aviffy::ConvertOptions convertOptions;
    convertOptions.quality = quality;
    convertOptions.force = force;

    const unsigned int workerCount = static_cast<unsigned int>(
        std::min<size_t>(static_cast<size_t>(threadCount), jobs.size()));

    std::atomic<size_t> nextIndex{0};
    std::atomic<size_t> converted{0};
    std::atomic<size_t> skipped{0};
    std::atomic<size_t> failed{0};
    std::mutex ioMutex;

    if (verbose && !quiet) {
        std::cout << "Using " << workerCount << " thread(s) for " << jobs.size() << " file(s)\n";
    }

    const auto start = std::chrono::steady_clock::now();

    auto worker = [&]() {
        for (;;) {
            const size_t index = nextIndex.fetch_add(1, std::memory_order_relaxed);
            if (index >= jobs.size()) {
                return;
            }
            const aviffy::Job& job = jobs[index];
            const aviffy::ConvertResult result =
                aviffy::convertFile(job.src, job.dst, convertOptions);

            std::lock_guard<std::mutex> lock(ioMutex);
            switch (result.status) {
            case aviffy::ConvertStatus::Converted:
                converted.fetch_add(1, std::memory_order_relaxed);
                if (!quiet) {
                    std::printf("[ ok ] %s -> %s\n", job.src.c_str(), job.dst.c_str());
                }
                break;
            case aviffy::ConvertStatus::Skipped:
                skipped.fetch_add(1, std::memory_order_relaxed);
                if (verbose && !quiet) {
                    std::printf("[skip] %s (exists)\n", job.src.c_str());
                }
                break;
            case aviffy::ConvertStatus::Failed:
                failed.fetch_add(1, std::memory_order_relaxed);
                std::fprintf(stderr, "[fail] %s: %s\n", job.src.c_str(), result.detail.c_str());
                break;
            }
        }
    };

    std::vector<std::thread> pool;
    pool.reserve(workerCount);
    for (unsigned int i = 0; i < workerCount; ++i) {
        pool.emplace_back(worker);
    }
    for (auto& t : pool) {
        t.join();
    }

    const double seconds =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();

    std::cout << "\n" << converted.load() << " converted, " << skipped.load() << " skipped, "
              << failed.load() << " failed (" << jobs.size() << " total) in " << std::fixed
              << std::setprecision(2) << seconds << "s\n";

    return (failed.load() > 0 || !scanErrors.empty()) ? 1 : 0;
}
