#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "diskhopper/safety/SafetyLevel.hpp"
#include "diskhopper/scanner/FileEntry.hpp"

namespace diskhopper {

struct ChildItem {
    std::string name;
    FileType type = FileType::Other;
};

struct DirNode {
    std::filesystem::path path;
    std::string name;
    uint64_t apparent = 0;
    uint64_t allocated = 0;
    uint64_t file_count = 0;
    uint64_t dir_count = 0;
    uint64_t symlink_count = 0;
    uint64_t other_count = 0;
    std::vector<std::unique_ptr<DirNode>> children;
    std::vector<ChildItem> items;

    SafetyLevel level = SafetyLevel::Protected;
    std::string rule_id;
    std::string rule_name;
    std::string reason;
    bool direct_match = false;
};

struct ScanStats {
    uint64_t file_count = 0;
    uint64_t dir_count = 0;
    uint64_t symlink_count = 0;
    uint64_t other_count = 0;
    uint64_t hardlink_deduped = 0;
    uint64_t errors = 0;
};

struct ScanResult {
    std::filesystem::path root;
    uint64_t apparent = 0;
    uint64_t allocated = 0;
    ScanStats stats;
    std::vector<std::string> errors;
    std::unique_ptr<DirNode> tree;
};

using FileCallback = std::function<void(const FileEntry&)>;

class Scanner {
public:
    struct Options {
        Options() : follow_symlinks(false), dedupe_hardlinks(true), threads(0) {}

        bool follow_symlinks;
        bool dedupe_hardlinks;
        size_t threads;
    };

    ScanResult scan(const std::filesystem::path& root,
                    const Options& options = Options(),
                    const FileCallback& on_file = nullptr);
};

}