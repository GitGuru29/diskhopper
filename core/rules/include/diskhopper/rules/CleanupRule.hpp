#pragma once

#include <string>
#include <vector>

#include "diskhopper/safety/SafetyLevel.hpp"

namespace diskhopper {

enum class MatchKind {
    PathSubtree,
    NameEquals,
};

struct CleanupRule {
    std::string id;
    std::string name;
    SafetyLevel level = SafetyLevel::Review;
    MatchKind kind = MatchKind::PathSubtree;
    std::string value;
    std::string why;
};

const std::vector<CleanupRule>& default_cleanup_rules();
const std::vector<CleanupRule>& default_protection_rules();
const std::vector<std::string>& default_project_markers();

}