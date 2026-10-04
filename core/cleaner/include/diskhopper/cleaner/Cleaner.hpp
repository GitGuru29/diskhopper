#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

#include "diskhopper/safety/SafetyLevel.hpp"

namespace diskhopper {

enum class CleanupActionKind {
    Trash,
    Delete,
};

struct CleanupItem {
    std::filesystem::path path;
    std::string rule_id;
    std::string rule_name;
    SafetyLevel level = SafetyLevel::Review;
    uint64_t bytes = 0;
    CleanupActionKind action = CleanupActionKind::Trash;
};

struct CleanupItemResult {
    std::filesystem::path path;
    std::string rule_id;
    uint64_t bytes = 0;
    CleanupActionKind action = CleanupActionKind::Trash;
    bool success = false;
    std::string note;
};

struct CleanupSessionResult {
    size_t planned = 0;
    size_t succeeded = 0;
    size_t failed = 0;
    uint64_t bytes_freed = 0;
    uint64_t bytes_failed = 0;
    std::vector<CleanupItemResult> items;
};

class Cleaner {
public:
    struct Options {
        bool allow_permanent = false;
        std::function<bool(const std::filesystem::path&)> protection_check;
        std::function<bool()> permanent_gate;
        std::function<bool(const std::filesystem::path&, std::string&)> trash_fn;
        std::filesystem::path audit_path;
    };

    static CleanupSessionResult run(const std::vector<CleanupItem>& items,
                                    const Options& options);
};

}