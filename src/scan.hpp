#pragma once

#include <string>
#include <vector>

namespace aviffy {

struct Job {
    std::string src;
    std::string dst;
};

// Expands the user-supplied inputs (files and/or directories) into a concrete
// list of source -> destination conversions.
//
//   * A regular file input must have a .avif extension.
//   * A directory input is scanned for .avif files. Without recursion only the
//     top level is scanned; with recursion the whole tree is walked.
//   * When outputDir is empty, each output .jpg is written next to its source.
//   * When outputDir is set, the output tree mirrors the input tree; files that
//     came directly from a directory keep their relative sub-path under outputDir.
//   * Duplicate sources (by canonical path) are collapsed to a single job.
//
// Non-fatal problems (unreadable paths, non-avif files) are appended to
// `errors` and do not abort planning.
std::vector<Job> planJobs(const std::vector<std::string>& inputs,
                          const std::string& outputDir,
                          bool recursive,
                          std::vector<std::string>& errors);

} // namespace aviffy
