#pragma once

#include <string>

namespace aviffy {

struct ConvertOptions {
    int quality = 90;  // JPEG quality, 1..100
    bool force = false; // overwrite an existing output file
};

enum class ConvertStatus {
    Converted,
    Skipped, // output already exists and force is false
    Failed,
};

struct ConvertResult {
    ConvertStatus status = ConvertStatus::Failed;
    std::string detail; // human-readable reason on Failed
};

// Decodes a single .avif file and writes it as a JPEG to dstPath.
// Sets parent directories of dstPath are not created here; the caller
// (or the write itself, via writeJpeg) is responsible.
ConvertResult convertFile(const std::string& srcPath,
                          const std::string& dstPath,
                          const ConvertOptions& opt);

} // namespace aviffy
