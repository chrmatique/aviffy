#include "scan.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <set>

namespace aviffy {
namespace {

namespace fs = std::filesystem;

std::string toLower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

bool hasAvifExtension(const fs::path& p) {
    return toLower(p.extension().string()) == ".avif";
}

void addJob(std::vector<Job>& jobs, std::set<std::string>& seen, std::set<std::string>& dstSeen,
            const fs::path& src, const fs::path& root, const fs::path& outRoot,
            bool fromDirectory, std::vector<std::string>& errors) {
    std::error_code ec;
    const fs::path canon = fs::canonical(src, ec);
    const std::string key = ec ? src.string() : canon.string();
    if (!seen.insert(key).second) {
        return; // duplicate source
    }

    fs::path jpgName = src.filename();
    jpgName.replace_extension(".jpg");

    fs::path dst;
    if (outRoot.empty()) {
        dst = src.parent_path() / jpgName;
    } else if (fromDirectory) {
        fs::path rel = src.lexically_relative(root);
        rel.replace_extension(".jpg");
        dst = outRoot / rel;
    } else {
        dst = outRoot / jpgName;
    }

    // Refuse to let two distinct sources target the same output file: silently
    // overwriting would lose an image.
    if (!dstSeen.insert(dst.string()).second) {
        errors.push_back("output collision: '" + src.string() + "' and another input both map to '" +
                         dst.string() + "' (job skipped)");
        return;
    }

    jobs.push_back(Job{src.string(), dst.string()});
}

} // namespace

std::vector<Job> planJobs(const std::vector<std::string>& inputs, const std::string& outputDir,
                          bool recursive, std::vector<std::string>& errors) {
    std::vector<Job> jobs;
    std::set<std::string> seen;
    std::set<std::string> dstSeen;
    const fs::path outRoot = outputDir.empty() ? fs::path() : fs::path(outputDir);

    for (const std::string& in : inputs) {
        std::error_code ec;
        const fs::path p(in);
        const fs::file_status st = fs::status(p, ec);
        if (ec) {
            errors.push_back("cannot access '" + in + "': " + ec.message());
            continue;
        }

        if (fs::is_regular_file(st)) {
            if (!hasAvifExtension(p)) {
                errors.push_back("not an .avif file: '" + in + "'");
                continue;
            }
            addJob(jobs, seen, dstSeen, p, fs::path(), outRoot, false, errors);
        } else if (fs::is_directory(st)) {
            const auto options = fs::directory_options::skip_permission_denied;
            if (recursive) {
                for (auto it = fs::recursive_directory_iterator(p, options, ec);
                     !ec && it != fs::recursive_directory_iterator(); it.increment(ec)) {
                    const auto& entry = *it;
                    if (!entry.is_regular_file(ec) || ec) {
                        continue;
                    }
                    if (hasAvifExtension(entry.path())) {
                        addJob(jobs, seen, dstSeen, entry.path(), p, outRoot, true, errors);
                    }
                }
            } else {
                for (auto it = fs::directory_iterator(p, options, ec);
                     !ec && it != fs::directory_iterator(); it.increment(ec)) {
                    const auto& entry = *it;
                    if (!entry.is_regular_file(ec) || ec) {
                        continue;
                    }
                    if (hasAvifExtension(entry.path())) {
                        addJob(jobs, seen, dstSeen, entry.path(), p, outRoot, true, errors);
                    }
                }
            }
            if (ec) {
                errors.push_back("error while scanning '" + in + "': " + ec.message());
            }
        } else {
            errors.push_back("unsupported input (not a file or directory): '" + in + "'");
        }
    }

    return jobs;
}

} // namespace aviffy
