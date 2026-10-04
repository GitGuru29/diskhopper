#pragma once

#include <cstdint>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

#include "diskhopper/rules/CleanupRule.hpp"
#include "diskhopper/scanner/Scanner.hpp"
#include "diskhopper/safety/SafetyLevel.hpp"

namespace diskhopper {

struct Classification {
    SafetyLevel level = SafetyLevel::Protected;
    std::string rule_id;
    std::string name;
    std::string reason;
    bool direct_match = false;
};

struct RuleTotal {
    std::string name;
    std::string why;
    SafetyLevel level = SafetyLevel::Protected;
    uint64_t bytes = 0;
};

struct Report {
    std::map<std::string, RuleTotal> safe;
    std::map<std::string, RuleTotal> review;
    std::map<std::string, RuleTotal> protected_roots;
    uint64_t default_protected_bytes = 0;
    uint64_t protected_bytes = 0;
    uint64_t safe_bytes = 0;
    uint64_t review_bytes = 0;
    std::vector<const DirNode*> top_cleanable;
};

class Classifier {
public:
    struct Options {
        Options() : enable_project_detection(true) {}

        bool enable_project_detection;
    };

    Classifier(std::filesystem::path home,
               std::vector<CleanupRule> cleanup_rules,
               std::vector<CleanupRule> protection_rules,
               Options options = Options());

    void apply(DirNode& root) const;
    Report summarize(const DirNode& root, size_t top_n) const;

    Classification describe(const DirNode& node) const;

    bool is_protected(const std::filesystem::path& path) const;

private:
    bool rule_matches(const CleanupRule& rule, const DirNode& node) const;
    bool path_matches(const CleanupRule& rule, const std::filesystem::path& path) const;
    const CleanupRule* best_cleanup_rule(const DirNode& node) const;
    bool is_under_or_equal(const std::filesystem::path& path,
                           const std::filesystem::path& base) const;
    bool is_project_root(const DirNode& node, std::string* marker_out) const;
    Classification evaluate(const DirNode& node, const Classification* inherited) const;

    std::filesystem::path home_;
    std::vector<CleanupRule> cleanup_;
    std::vector<CleanupRule> protections_;
    std::map<std::string, const CleanupRule*> cleanups_by_id_;
    std::map<std::string, const CleanupRule*> protections_by_id_;
    Options options_;
};

}