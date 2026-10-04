#include "diskhopper/classifier/Classifier.hpp"

#include <algorithm>
#include <functional>

namespace diskhopper {

namespace {

std::string ends_with(const std::string& value, const std::string& suffix) {
    if (suffix.size() > value.size()) return "";
    return value.compare(value.size() - suffix.size(), suffix.size(), suffix) == 0
               ? value
               : "";
}

}

Classifier::Classifier(std::filesystem::path home,
                       std::vector<CleanupRule> cleanup_rules,
                       std::vector<CleanupRule> protection_rules,
                       Options options)
    : cleanup_(std::move(cleanup_rules)),
      protections_(std::move(protection_rules)),
      options_(options) {
    std::error_code ec;
    std::filesystem::path resolved = std::filesystem::weakly_canonical(home, ec);
    home_ = ec || resolved.empty() ? home : resolved;
    for (const auto& rule : cleanup_) cleanups_by_id_[rule.id] = &rule;
    for (const auto& rule : protections_) protections_by_id_[rule.id] = &rule;
}

bool Classifier::is_under_or_equal(const std::filesystem::path& path,
                                   const std::filesystem::path& base) const {
    auto bp = base.begin();
    auto pp = path.begin();
    for (; bp != base.end(); ++bp, ++pp) {
        if (pp == path.end() || *pp != *bp) return false;
    }
    return true;
}

bool Classifier::path_matches(const CleanupRule& rule,
                              const std::filesystem::path& path) const {
    switch (rule.kind) {
        case MatchKind::PathSubtree:
            return is_under_or_equal(path, home_ / rule.value);
        case MatchKind::NameEquals:
            return path.filename().string() == rule.value;
    }
    return false;
}

bool Classifier::rule_matches(const CleanupRule& rule, const DirNode& node) const {
    return path_matches(rule, node.path);
}

bool Classifier::is_protected(const std::filesystem::path& path) const {
    std::error_code ec;
    std::filesystem::path resolved = std::filesystem::weakly_canonical(path, ec);
    if (ec || resolved.empty()) resolved = path;
    for (const auto& rule : protections_) {
        if (path_matches(rule, resolved)) return true;
    }
    return false;
}

bool Classifier::is_project_root(const DirNode& node, std::string* marker_out) const {
    for (const auto& marker : default_project_markers()) {
        for (const auto& item : node.items) {
            if (item.name == marker) {
                if (marker_out) *marker_out = marker;
                return true;
            }
        }
    }
    if (ends_with(node.name, ".xcodeproj") != "" || ends_with(node.name, ".xcworkspace") != "") {
        if (marker_out) *marker_out = node.name;
        return true;
    }
    return false;
}

const CleanupRule* Classifier::best_cleanup_rule(const DirNode& node) const {
    const CleanupRule* best_rule = nullptr;
    size_t best_specificity = 0;

    auto specificity = [&](const CleanupRule& rule) -> size_t {
        if (rule.kind == MatchKind::NameEquals) return static_cast<size_t>(1) << 30;
        const std::filesystem::path target = home_ / rule.value;
        size_t count = 0;
        for (auto it = target.begin(); it != target.end(); ++it) {
            ++count;
        }
        return count;
    };

    for (const auto& rule : cleanup_) {
        if (!rule_matches(rule, node)) continue;
        const size_t spec = specificity(rule);
        if (best_rule == nullptr || spec > best_specificity ||
            (spec == best_specificity && rule.level > best_rule->level)) {
            best_rule = &rule;
            best_specificity = spec;
        }
    }

    return best_rule;
}

Classification Classifier::describe(const DirNode& node) const {
    std::string marker;
    if (options_.enable_project_detection && is_project_root(node, &marker)) {
        Classification c;
        c.level = SafetyLevel::Protected;
        c.rule_id = "project.root";
        c.name = "Project";
        c.reason = "Project detected (marker: " + marker + ")";
        c.direct_match = true;
        return c;
    }

    const CleanupRule* rule = best_cleanup_rule(node);
    if (rule != nullptr) {
        Classification c;
        c.level = rule->level;
        c.rule_id = rule->id;
        c.name = rule->name;
        c.reason = rule->why;
        c.direct_match = true;
        return c;
    }

    Classification c;
    c.level = SafetyLevel::Protected;
    c.rule_id = "other";
    c.name = "Other";
    c.reason = "No rule matched; treated as user data.";
    c.direct_match = true;
    return c;
}

Classification Classifier::evaluate(const DirNode& node,
                                    const Classification* inherited) const {
    if (inherited != nullptr && inherited->level == SafetyLevel::Protected &&
        !inherited->rule_id.empty()) {
        return *inherited;
    }

    for (const auto& rule : protections_) {
        if (rule_matches(rule, node)) {
            Classification c;
            c.level = SafetyLevel::Protected;
            c.rule_id = rule.id;
            c.name = rule.name;
            c.reason = rule.why + "  (hard-coded protection)";
            c.direct_match = true;
            return c;
        }
    }

    if (options_.enable_project_detection) {
        std::string marker;
        if (is_project_root(node, &marker)) {
            Classification c;
            c.level = SafetyLevel::Protected;
            c.rule_id = "project.root";
            c.name = "Project";
            c.reason = "Project detected (marker: " + marker + ") - never auto-removed.";
            c.direct_match = true;
            return c;
        }
    }

    const CleanupRule* best_rule = best_cleanup_rule(node);
    if (best_rule != nullptr) {
        Classification c;
        c.level = best_rule->level;
        c.rule_id = best_rule->id;
        c.name = best_rule->name;
        c.reason = best_rule->why;
        c.direct_match = true;
        return c;
    }

    if (inherited != nullptr) {
        return *inherited;
    }

    Classification c;
    c.level = SafetyLevel::Protected;
    c.name = "Unclassified";
    c.reason = "No cleanup rule applies; protected by default.";
    c.direct_match = true;
    return c;
}

void Classifier::apply(DirNode& root) const {
    std::function<void(DirNode&, const Classification*)> recurse;
    recurse = [&](DirNode& node, const Classification* inherited) {
        Classification result = evaluate(node, inherited);
        node.level = result.level;
        node.rule_id = result.rule_id;
        node.rule_name = result.name;
        node.reason = result.reason;
        node.direct_match = result.direct_match;
        for (auto& child : node.children) {
            recurse(*child, &result);
        }
    };
    recurse(root, nullptr);
}

Report Classifier::summarize(const DirNode& root, size_t top_n) const {
    Report report;

    std::function<void(const DirNode&, SafetyLevel, const std::string&)> recurse;
    recurse = [&](const DirNode& node, SafetyLevel context_level, const std::string& context_rule) {
        SafetyLevel level = context_level;
        std::string rule_id = context_rule;
        if (node.direct_match) {
            level = node.level;
            rule_id = node.rule_id;
        }

        uint64_t own = node.allocated;
        for (const auto& child : node.children) {
            recurse(*child, level, rule_id);
            own -= child->allocated;
        }

        if (level == SafetyLevel::Safe) {
            report.safe[rule_id].bytes += own;
            report.safe_bytes += own;
        } else if (level == SafetyLevel::Review) {
            report.review[rule_id].bytes += own;
            report.review_bytes += own;
        } else {
            report.protected_bytes += own;
            if (rule_id.empty()) {
                report.default_protected_bytes += own;
            } else {
                report.protected_roots[rule_id].bytes += own;
            }
        }
    };
    recurse(root, SafetyLevel::Protected, "");

    for (auto& [id, total] : report.safe) {
        auto it = cleanups_by_id_.find(id);
        if (it != cleanups_by_id_.end()) {
            total.name = it->second->name;
            total.why = it->second->why;
            total.level = it->second->level;
        } else {
            total.name = id;
            total.level = SafetyLevel::Safe;
        }
    }
    for (auto& [id, total] : report.review) {
        auto it = cleanups_by_id_.find(id);
        if (it != cleanups_by_id_.end()) {
            total.name = it->second->name;
            total.why = it->second->why;
            total.level = it->second->level;
        } else {
            total.name = id;
            total.level = SafetyLevel::Review;
        }
    }
    for (auto& [id, total] : report.protected_roots) {
        auto it = protections_by_id_.find(id);
        if (it != protections_by_id_.end()) {
            total.name = it->second->name;
            total.why = it->second->why;
            total.level = SafetyLevel::Protected;
        } else {
            total.name = id;
            total.level = SafetyLevel::Protected;
        }
    }

    std::vector<const DirNode*> cleanable;
    std::function<void(const DirNode&)> collect;
    collect = [&](const DirNode& node) {
        if (node.direct_match &&
            (node.level == SafetyLevel::Safe || node.level == SafetyLevel::Review)) {
            cleanable.push_back(&node);
        }
        for (const auto& child : node.children) collect(*child);
    };
    collect(root);
    std::sort(cleanable.begin(), cleanable.end(),
              [](const DirNode* a, const DirNode* b) {
                  return a->allocated > b->allocated;
              });
    if (cleanable.size() > top_n) cleanable.resize(top_n);
    report.top_cleanable = std::move(cleanable);

    return report;
}

}