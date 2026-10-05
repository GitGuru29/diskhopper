#include "diskhopper/bridge/diskhopper.h"

#include <algorithm>
#include <filesystem>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "diskhopper/classifier/Classifier.hpp"
#include "diskhopper/cleaner/Cleaner.hpp"
#include "diskhopper/platform/Trash.hpp"
#include "diskhopper/rules/CleanupRule.hpp"
#include "diskhopper/scanner/Scanner.hpp"

namespace fs = std::filesystem;

struct DhEngineImpl {
    std::string home;
};

struct BucketRule {
    std::string path;
    std::string rule_id;
    std::string rule_name;
    uint64_t bytes = 0;
    diskhopper::SafetyLevel level = diskhopper::SafetyLevel::Protected;
};

struct DhScanImpl {
    std::string error;
    diskhopper::ScanResult result;
    std::unique_ptr<diskhopper::Classifier> classifier;
    diskhopper::Report report;
    std::vector<BucketRule> safe;
    std::vector<BucketRule> review;
};

struct DhCleanResultImpl {
    size_t planned = 0;
    size_t succeeded = 0;
    size_t failed = 0;
    uint64_t bytes_freed = 0;
    uint64_t bytes_failed = 0;
    std::string first_error;
};

namespace {

void collect_bucket_rules(const diskhopper::DirNode& node,
                          const diskhopper::Classifier& classifier,
                          std::vector<BucketRule>& safe,
                          std::vector<BucketRule>& review) {
    const diskhopper::Classification c = classifier.describe(node);
    if (c.direct_match) {
        BucketRule rule;
        rule.path = node.path.string();
        rule.rule_id = c.rule_id;
        rule.rule_name = c.name;
        rule.bytes = node.allocated;
        rule.level = c.level;
        if (c.level == diskhopper::SafetyLevel::Safe) {
            safe.push_back(std::move(rule));
        } else if (c.level == diskhopper::SafetyLevel::Review) {
            review.push_back(std::move(rule));
        }
    }
    for (const auto& child : node.children) {
        collect_bucket_rules(*child, classifier, safe, review);
    }
}

void sort_biggest_first(std::vector<BucketRule>& rules) {
    std::sort(rules.begin(), rules.end(),
              [](const BucketRule& a, const BucketRule& b) {
                  return a.bytes > b.bytes;
              });
}

}  // namespace

extern "C" {

DhEngine* dh_engine_create(const char* home_dir) {
    try {
        DhEngineImpl* engine = new DhEngineImpl();
        engine->home = (home_dir != nullptr) ? home_dir : "";
        return reinterpret_cast<DhEngine*>(engine);
    } catch (...) {
        return nullptr;
    }
}

void dh_engine_destroy(DhEngine* engine) {
    delete reinterpret_cast<DhEngineImpl*>(engine);
}

DhScan* dh_scan(DhEngine* engine, const char* path, int threads) {
    if (engine == nullptr || path == nullptr || path[0] == '\0') {
        return nullptr;
    }
    try {
        DhScanImpl* scan = new DhScanImpl();
        diskhopper::Scanner scanner;
        diskhopper::Scanner::Options options;
        options.threads = threads > 0 ? static_cast<size_t>(threads) : 0;
        scan->result = scanner.scan(fs::path(path), options);
        if (scan->result.tree == nullptr) {
            scan->error = "scan failed: " + fs::path(path).string();
            if (!scan->result.errors.empty()) {
                scan->error += " - " + scan->result.errors.front();
            }
            return reinterpret_cast<DhScan*>(scan);
        }
        const DhEngineImpl* host = reinterpret_cast<DhEngineImpl*>(engine);
        scan->classifier = std::make_unique<diskhopper::Classifier>(
            host->home, diskhopper::default_cleanup_rules(),
            diskhopper::default_protection_rules());
        scan->classifier->apply(*scan->result.tree);
        scan->report = scan->classifier->summarize(*scan->result.tree, 1);
        collect_bucket_rules(*scan->result.tree, *scan->classifier,
                             scan->safe, scan->review);
        sort_biggest_first(scan->safe);
        sort_biggest_first(scan->review);
        return reinterpret_cast<DhScan*>(scan);
    } catch (...) {
        return nullptr;
    }
}

const char* dh_scan_error(const DhScan* scan) {
    const DhScanImpl* impl = reinterpret_cast<const DhScanImpl*>(scan);
    return (impl != nullptr) ? impl->error.c_str() : "no scan";
}

void dh_scan_destroy(DhScan* scan) {
    delete reinterpret_cast<DhScanImpl*>(scan);
}

const char* dh_scan_root(const DhScan* scan) {
    const DhScanImpl* impl = reinterpret_cast<const DhScanImpl*>(scan);
    return (impl != nullptr) ? impl->result.root.string().c_str() : "";
}

uint64_t dh_scan_apparent(const DhScan* scan) {
    const DhScanImpl* impl = reinterpret_cast<const DhScanImpl*>(scan);
    return (impl != nullptr) ? impl->result.apparent : 0;
}

uint64_t dh_scan_allocated(const DhScan* scan) {
    const DhScanImpl* impl = reinterpret_cast<const DhScanImpl*>(scan);
    return (impl != nullptr) ? impl->result.allocated : 0;
}

uint64_t dh_scan_files(const DhScan* scan) {
    const DhScanImpl* impl = reinterpret_cast<const DhScanImpl*>(scan);
    return (impl != nullptr) ? impl->result.stats.file_count : 0;
}

uint64_t dh_scan_dirs(const DhScan* scan) {
    const DhScanImpl* impl = reinterpret_cast<const DhScanImpl*>(scan);
    return (impl != nullptr) ? impl->result.stats.dir_count : 0;
}

uint64_t dh_scan_symlinks(const DhScan* scan) {
    const DhScanImpl* impl = reinterpret_cast<const DhScanImpl*>(scan);
    return (impl != nullptr) ? impl->result.stats.symlink_count : 0;
}

uint64_t dh_scan_errors(const DhScan* scan) {
    const DhScanImpl* impl = reinterpret_cast<const DhScanImpl*>(scan);
    return (impl != nullptr) ? impl->result.stats.errors : 0;
}

uint64_t dh_scan_bucket_bytes(const DhScan* scan, int bucket) {
    const DhScanImpl* impl = reinterpret_cast<const DhScanImpl*>(scan);
    if (impl == nullptr) return 0;
    switch (bucket) {
    case DhBucketSafe:
        return impl->report.safe_bytes;
    case DhBucketReview:
        return impl->report.review_bytes;
    default:
        return impl->report.protected_bytes;
    }
}

size_t dh_scan_cleanable_count(const DhScan* scan, int bucket) {
    const DhScanImpl* impl = reinterpret_cast<const DhScanImpl*>(scan);
    if (impl == nullptr) return 0;
    switch (bucket) {
    case DhBucketSafe:
        return impl->safe.size();
    case DhBucketReview:
        return impl->review.size();
    default:
        return 0;
    }
}

const char* dh_scan_cleanable_path(const DhScan* scan, int bucket,
                                   size_t index) {
    const DhScanImpl* impl = reinterpret_cast<const DhScanImpl*>(scan);
    if (impl == nullptr) return "";
    const std::vector<BucketRule>& rules =
        (bucket == DhBucketSafe) ? impl->safe : impl->review;
    return (index < rules.size()) ? rules[index].path.c_str() : "";
}

const char* dh_scan_cleanable_rule_id(const DhScan* scan, int bucket,
                                      size_t index) {
    const DhScanImpl* impl = reinterpret_cast<const DhScanImpl*>(scan);
    if (impl == nullptr) return "";
    const std::vector<BucketRule>& rules =
        (bucket == DhBucketSafe) ? impl->safe : impl->review;
    return (index < rules.size()) ? rules[index].rule_id.c_str() : "";
}

const char* dh_scan_cleanable_rule_name(const DhScan* scan, int bucket,
                                        size_t index) {
    const DhScanImpl* impl = reinterpret_cast<const DhScanImpl*>(scan);
    if (impl == nullptr) return "";
    const std::vector<BucketRule>& rules =
        (bucket == DhBucketSafe) ? impl->safe : impl->review;
    return (index < rules.size()) ? rules[index].rule_name.c_str() : "";
}

uint64_t dh_scan_cleanable_size(const DhScan* scan, int bucket,
                                size_t index) {
    const DhScanImpl* impl = reinterpret_cast<const DhScanImpl*>(scan);
    if (impl == nullptr) return 0;
    const std::vector<BucketRule>& rules =
        (bucket == DhBucketSafe) ? impl->safe : impl->review;
    return (index < rules.size()) ? rules[index].bytes : 0;
}

DhCleanResult* dh_clean(DhScan* scan, int include_safe, int include_review,
                        int permanent_safe, const char* audit_path) {
    DhScanImpl* impl = reinterpret_cast<DhScanImpl*>(scan);
    if (impl == nullptr || impl->result.tree == nullptr) return nullptr;
    try {
        std::vector<diskhopper::CleanupItem> items;
        auto add = [&](const BucketRule& rule,
                       diskhopper::CleanupActionKind action) {
            diskhopper::CleanupItem item;
            item.path = rule.path;
            item.rule_id = rule.rule_id;
            item.rule_name = rule.rule_name;
            item.level = rule.level;
            item.bytes = rule.bytes;
            item.action = action;
            items.push_back(std::move(item));
        };
        if (include_review != 0) {
            for (const auto& rule : impl->review) {
                add(rule, diskhopper::CleanupActionKind::Trash);
            }
        }
        if (include_safe != 0) {
            const diskhopper::CleanupActionKind action =
                permanent_safe != 0 ? diskhopper::CleanupActionKind::Delete
                                    : diskhopper::CleanupActionKind::Trash;
            for (const auto& rule : impl->safe) {
                add(rule, action);
            }
        }

        diskhopper::Cleaner::Options options;
        options.allow_permanent = permanent_safe != 0;
        options.protection_check = [classifier = impl->classifier.get()](
                                       const fs::path& path) {
            return !classifier->is_protected(path);
        };
        options.permanent_gate = diskhopper::time_machine_available;
        options.trash_fn = diskhopper::move_to_trash;
        options.audit_path = (audit_path != nullptr && audit_path[0] != '\0')
                                 ? fs::path(audit_path)
                                 : fs::path();

        const diskhopper::CleanupSessionResult outcome =
            diskhopper::Cleaner::run(items, options);

        DhCleanResultImpl* result = new DhCleanResultImpl();
        result->planned = outcome.planned;
        result->succeeded = outcome.succeeded;
        result->failed = outcome.failed;
        result->bytes_freed = outcome.bytes_freed;
        result->bytes_failed = outcome.bytes_failed;
        for (const auto& item : outcome.items) {
            if (!item.success) {
                result->first_error = item.note;
                break;
            }
        }
        return reinterpret_cast<DhCleanResult*>(result);
    } catch (...) {
        return nullptr;
    }
}

size_t dh_clean_planned(const DhCleanResult* result) {
    const DhCleanResultImpl* impl =
        reinterpret_cast<const DhCleanResultImpl*>(result);
    return (impl != nullptr) ? impl->planned : 0;
}

size_t dh_clean_succeeded(const DhCleanResult* result) {
    const DhCleanResultImpl* impl =
        reinterpret_cast<const DhCleanResultImpl*>(result);
    return (impl != nullptr) ? impl->succeeded : 0;
}

size_t dh_clean_failed(const DhCleanResult* result) {
    const DhCleanResultImpl* impl =
        reinterpret_cast<const DhCleanResultImpl*>(result);
    return (impl != nullptr) ? impl->failed : 0;
}

uint64_t dh_clean_bytes_freed(const DhCleanResult* result) {
    const DhCleanResultImpl* impl =
        reinterpret_cast<const DhCleanResultImpl*>(result);
    return (impl != nullptr) ? impl->bytes_freed : 0;
}

uint64_t dh_clean_bytes_failed(const DhCleanResult* result) {
    const DhCleanResultImpl* impl =
        reinterpret_cast<const DhCleanResultImpl*>(result);
    return (impl != nullptr) ? impl->bytes_failed : 0;
}

const char* dh_clean_first_error(const DhCleanResult* result) {
    const DhCleanResultImpl* impl =
        reinterpret_cast<const DhCleanResultImpl*>(result);
    return (impl != nullptr) ? impl->first_error.c_str() : "";
}

void dh_clean_destroy(DhCleanResult* result) {
    delete reinterpret_cast<DhCleanResultImpl*>(result);
}

int dh_time_machine_available(void) {
    return diskhopper::time_machine_available() ? 1 : 0;
}

}  // extern "C"