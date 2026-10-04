#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

#include "diskhopper/classifier/Classifier.hpp"
#include "diskhopper/platform/UserPaths.hpp"
#include "diskhopper/rules/CleanupRule.hpp"
#include "diskhopper/scanner/Scanner.hpp"

namespace {

const char* kVersion = "0.2.0";

struct Options {
    std::filesystem::path root = ".";
    int max_depth = 2;
    int top_n = 10;
    bool json = false;
    bool help = false;
    bool version = false;
};

struct ParsedArgs {
    std::string command;
    Options opts;
    bool ok = true;
};

void usage() {
    std::printf(
        "diskhopper %s - storage analyzer\n"
        "\n"
        "Usage:\n"
        "  diskhopper scan [OPTIONS] [PATH]\n"
        "  diskhopper report [OPTIONS] [PATH]\n"
        "  diskhopper explain [OPTIONS] [PATH]\n"
        "\n"
        "Commands:\n"
        "  scan         Scan a directory tree and report sizes\n"
        "  report       Classify storage as SAFE / REVIEW / PROTECTED\n"
        "  explain      Break down why a path uses its space\n"
        "\n"
        "Options:\n"
        "  --depth N    Max tree depth to print (default 2)\n"
        "  --top N      Top entries to list (default 10)\n"
        "  --json       Emit machine-readable JSON (scan only)\n"
        "  --help       Show this help\n"
        "  --version    Show version\n",
        kVersion);
}

std::string human(uint64_t bytes) {
    const char* units[] = {"B", "KB", "MB", "GB", "TB", "PB"};
    double value = static_cast<double>(bytes);
    int unit = 0;
    const int max_unit = static_cast<int>(sizeof(units) / sizeof(units[0])) - 1;
    while (value >= 1024.0 && unit < max_unit) {
        value /= 1024.0;
        ++unit;
    }
    char buf[64];
    if (unit == 0) {
        std::snprintf(buf, sizeof(buf), "%.0f %s", value, units[unit]);
    } else {
        std::snprintf(buf, sizeof(buf), "%.1f %s", value, units[unit]);
    }
    return buf;
}

ParsedArgs parse_args(int argc, char** argv) {
    ParsedArgs out;
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "scan" || arg == "report" || arg == "explain") {
            out.command = arg;
        } else if (arg == "--depth") {
            if (i + 1 >= argc) {
                std::fprintf(stderr, "error: --depth requires a value\n");
                out.ok = false;
                return out;
            }
            out.opts.max_depth = std::atoi(argv[++i]);
        } else if (arg == "--top") {
            if (i + 1 >= argc) {
                std::fprintf(stderr, "error: --top requires a value\n");
                out.ok = false;
                return out;
            }
            out.opts.top_n = std::atoi(argv[++i]);
        } else if (arg == "--json") {
            out.opts.json = true;
        } else if (arg == "--help") {
            out.opts.help = true;
        } else if (arg == "--version") {
            out.opts.version = true;
        } else if (arg.size() > 1 && arg[0] == '-') {
            std::fprintf(stderr, "error: unknown option '%s'\n", arg.c_str());
            out.ok = false;
            return out;
        } else {
            out.opts.root = arg;
        }
    }
    return out;
}

std::vector<const diskhopper::DirNode*> sorted_children(const diskhopper::DirNode& node) {
    std::vector<const diskhopper::DirNode*> children;
    children.reserve(node.children.size());
    for (const auto& child : node.children) children.push_back(child.get());
    std::sort(children.begin(), children.end(),
              [](const diskhopper::DirNode* a, const diskhopper::DirNode* b) {
                  return a->allocated > b->allocated;
              });
    return children;
}

bool empty_node(const diskhopper::DirNode& node) {
    return node.allocated == 0 && node.file_count == 0 && node.children.empty();
}

void print_tree(const diskhopper::DirNode& node, int depth, int max_depth) {
    std::string indent(static_cast<size_t>(depth) * 2, ' ');
    std::printf("%s%s  %s\n", indent.c_str(), node.name.c_str(), human(node.allocated).c_str());
    if (depth >= max_depth) return;
    for (const diskhopper::DirNode* child : sorted_children(node)) {
        if (!empty_node(*child)) {
            print_tree(*child, depth + 1, max_depth);
        }
    }
}

std::string json_escape(const std::string& input) {
    std::string out;
    out.reserve(input.size());
    for (char c : input) {
        switch (c) {
            case '"':
                out += "\\\"";
                break;
            case '\\':
                out += "\\\\";
                break;
            case '\n':
                out += "\\n";
                break;
            case '\t':
                out += "\\t";
                break;
            default:
                out += c;
                break;
        }
    }
    return out;
}

void print_json(const diskhopper::DirNode& node, int depth) {
    std::printf("{\"name\":\"%s\",\"apparent\":%llu,\"allocated\":%llu,"
                "\"files\":%llu,\"dirs\":%llu",
                json_escape(node.name).c_str(),
                static_cast<unsigned long long>(node.apparent),
                static_cast<unsigned long long>(node.allocated),
                static_cast<unsigned long long>(node.file_count),
                static_cast<unsigned long long>(node.dir_count));
    if (depth > 0) {
        std::printf(",\"children\":[");
        bool first = true;
        for (const diskhopper::DirNode* child : sorted_children(node)) {
            if (!first) std::printf(",");
            first = false;
            print_json(*child, depth - 1);
        }
        std::printf("]");
    }
    std::printf("}");
}

void print_summary(const diskhopper::ScanResult& result, double seconds) {
    std::printf("diskhopper %s\n", kVersion);
    std::printf("Scanning: %s\n\n", result.root.string().c_str());
    std::printf("Files: %llu   Directories: %llu   Symlinks: %llu   Other: %llu\n",
                static_cast<unsigned long long>(result.stats.file_count),
                static_cast<unsigned long long>(result.stats.dir_count),
                static_cast<unsigned long long>(result.stats.symlink_count),
                static_cast<unsigned long long>(result.stats.other_count));
    if (result.stats.hardlink_deduped > 0) {
        std::printf("Hard-linked files counted once: %llu\n",
                    static_cast<unsigned long long>(result.stats.hardlink_deduped));
    }
    std::printf("Apparent:  %s\n", human(result.apparent).c_str());
    std::printf("Allocated: %s\n", human(result.allocated).c_str());

    const uint64_t items =
        result.stats.file_count + result.stats.dir_count +
        result.stats.symlink_count + result.stats.other_count;
    if (seconds > 0.0 && items > 0) {
        std::printf("Scanned %llu items in %.2fs (%.0f items/s)\n",
                    static_cast<unsigned long long>(items), seconds,
                    static_cast<double>(items) / seconds);
    }
    if (result.stats.errors > 0) {
        std::printf("\nErrors: %llu\n", static_cast<unsigned long long>(result.stats.errors));
        const size_t shown = std::min<size_t>(result.errors.size(), 5);
        for (size_t i = 0; i < shown; ++i) {
            std::printf("  %s\n", result.errors[i].c_str());
        }
        if (result.errors.size() > shown) {
            std::printf("  ... and %zu more\n", result.errors.size() - shown);
        }
    }
}

std::vector<std::pair<std::string, diskhopper::RuleTotal>> sorted_totals(
    const std::map<std::string, diskhopper::RuleTotal>& totals) {
    std::vector<std::pair<std::string, diskhopper::RuleTotal>> rows(totals.begin(), totals.end());
    std::sort(rows.begin(), rows.end(),
              [](const auto& a, const auto& b) { return a.second.bytes > b.second.bytes; });
    return rows;
}

void print_rule_table(const std::map<std::string, diskhopper::RuleTotal>& totals,
                      const std::string& prefix) {
    size_t width = 0;
    for (const auto& [id, total] : totals) {
        width = std::max(width, total.name.size());
    }
    std::string prefix_path = prefix + "/";
    for (const auto& [id, total] : sorted_totals(totals)) {
        if (total.bytes == 0) continue;
        std::printf("  %-*s  %8s   %s\n",
                    static_cast<int>(width), total.name.c_str(),
                    human(total.bytes).c_str(), prefix_path.c_str());
    }
}

void print_report(const diskhopper::ScanResult& scan_result,
                  const diskhopper::Report& report,
                  const std::vector<diskhopper::CleanupRule>& protections) {
    std::printf("diskhopper %s - storage report\n", kVersion);
    std::printf("Scanning: %s\n\n", scan_result.root.string().c_str());
    std::printf("Files: %llu   Directories: %llu   Symlinks: %llu   Other: %llu\n",
                static_cast<unsigned long long>(scan_result.stats.file_count),
                static_cast<unsigned long long>(scan_result.stats.dir_count),
                static_cast<unsigned long long>(scan_result.stats.symlink_count),
                static_cast<unsigned long long>(scan_result.stats.other_count));
    std::printf("Allocated: %s   (%llu files)\n\n",
                human(scan_result.allocated).c_str(),
                static_cast<unsigned long long>(scan_result.stats.file_count));

    std::printf("SAFE - regenerable, deterministic\n");
    print_rule_table(report.safe, "~");
    std::printf("  SAFE TOTAL:             %s\n", human(report.safe_bytes).c_str());

    std::printf("\nREVIEW - potentially removable, check first\n");
    print_rule_table(report.review, "~");
    std::printf("  REVIEW TOTAL:           %s\n", human(report.review_bytes).c_str());

    std::printf("\nEstimated cleanable: %s\n",
                human(report.safe_bytes + report.review_bytes).c_str());

    std::printf("\nPROTECTED - never offered for cleanup\n");
    std::printf("  Protected total:        %s\n", human(report.protected_bytes).c_str());
    size_t shown = 0;
    for (const auto& [id, total] : sorted_totals(report.protected_roots)) {
        if (shown >= 6) break;
        if (total.bytes == 0) continue;
        std::string target;
        for (const auto& rule : protections) {
            if (rule.id == id) {
                target = rule.value;
                break;
            }
        }
        if (target.empty()) target = id;
        std::printf("  %-28s %8s   ~/%s\n", total.name.c_str(),
                    human(total.bytes).c_str(), target.c_str());
        ++shown;
    }
    if (report.default_protected_bytes > 0) {
        std::printf("  %-28s %8s   (unclassified, protected by default)\n",
                    "Other / unclassified", human(report.default_protected_bytes).c_str());
    }

    if (!report.top_cleanable.empty()) {
        std::printf("\nLargest cleanable locations:\n");
        for (const diskhopper::DirNode* node : report.top_cleanable) {
            std::printf("  %8s  %-22s  %s\n",
                        human(node->allocated).c_str(),
                        node->rule_name.c_str(),
                        node->path.string().c_str());
        }
    }

    std::printf("\nProtection: %zu hard-coded rules.\n", protections.size());
    std::printf("Nothing was deleted. This report is read-only.\n");
}

uint64_t own_bytes(const diskhopper::DirNode& node) {
    uint64_t own = node.allocated;
    for (const auto& child : node.children) own -= child->allocated;
    return own;
}

const char* explain_tag(const diskhopper::Classification& classification) {
    if (classification.name == "Project") return "PROJECT";
    if (classification.name == "Other") return "OTHER";
    switch (classification.level) {
        case diskhopper::SafetyLevel::Safe:
            return "SAFE";
        case diskhopper::SafetyLevel::Review:
            return "REVIEW";
        default:
            return "PROTECTED";
    }
}

struct ExplainTotals {
    uint64_t project = 0;
    uint64_t safe = 0;
    uint64_t review = 0;
    uint64_t other = 0;
};

void collect_totals(const diskhopper::DirNode& node,
                    const diskhopper::Classifier& classifier,
                    ExplainTotals& totals) {
    const diskhopper::Classification c = classifier.describe(node);
    if (c.name == "Project") {
        totals.project += node.allocated;
        return;
    }
    const uint64_t own = own_bytes(node);
    if (c.name == "Other") {
        totals.other += own;
    } else if (c.level == diskhopper::SafetyLevel::Safe) {
        totals.safe += own;
    } else if (c.level == diskhopper::SafetyLevel::Review) {
        totals.review += own;
    } else {
        totals.other += own;
    }
    for (const auto& child : node.children) collect_totals(*child, classifier, totals);
}

void print_explain_children(const diskhopper::DirNode& node,
                            const diskhopper::Classifier& classifier,
                            int max_children) {
    std::vector<const diskhopper::DirNode*> kids;
    kids.reserve(node.children.size());
    for (const auto& child : node.children) kids.push_back(child.get());
    std::sort(kids.begin(), kids.end(),
              [](const diskhopper::DirNode* a, const diskhopper::DirNode* b) {
                  return a->allocated > b->allocated;
              });

    size_t shown = 0;
    uint64_t hidden_bytes = 0;
    size_t hidden_count = 0;
    for (const diskhopper::DirNode* kid : kids) {
        if (empty_node(*kid)) continue;
        if (shown >= static_cast<size_t>(max_children)) {
            hidden_bytes += kid->allocated;
            ++hidden_count;
            continue;
        }
        const diskhopper::Classification c = classifier.describe(*kid);
        std::printf("  %8s  %-24s  [%-7s]  %s\n",
                    human(kid->allocated).c_str(),
                    kid->name.c_str(),
                    explain_tag(c),
                    c.reason.c_str());
        ++shown;
    }
    if (hidden_count > 0) {
        std::printf("  %8s  ... and %zu more items\n",
                    human(hidden_bytes).c_str(), hidden_count);
    }
}

void print_explain(const diskhopper::ScanResult& scan_result,
                   const diskhopper::Classifier& classifier,
                   int max_children) {
    const diskhopper::DirNode& root = *scan_result.tree;
    std::printf("diskhopper %s - explain\n", kVersion);
    std::printf("Path: %s\n", root.path.string().c_str());
    std::printf("  Total: %s - %s\n", human(root.allocated).c_str(), root.reason.c_str());
    std::printf("  %llu files, %llu dirs\n\n",
                static_cast<unsigned long long>(scan_result.stats.file_count),
                static_cast<unsigned long long>(scan_result.stats.dir_count));

    std::printf("Breakdown of what this path contains:\n");
    print_explain_children(root, classifier, max_children);

    ExplainTotals totals;
    collect_totals(root, classifier, totals);
    std::printf("\nTotals:  PROJECT %s  |  REVIEW %s  |  SAFE %s  |  OTHER %s\n",
                human(totals.project).c_str(), human(totals.review).c_str(),
                human(totals.safe).c_str(), human(totals.other).c_str());
    std::printf("Nothing was deleted. This report is read-only.\n");
}

}

int main(int argc, char** argv) {
    ParsedArgs args = parse_args(argc, argv);
    if (!args.ok) return 2;
    if (args.opts.help) {
        usage();
        return 0;
    }
    if (args.opts.version) {
        std::printf("diskhopper %s\n", kVersion);
        return 0;
    }
    if (args.command != "scan" && args.command != "report" && args.command != "explain") {
        if (args.command.empty()) {
            std::fprintf(stderr, "error: no command given\n\n");
        } else {
            std::fprintf(stderr, "error: unknown command '%s'\n", args.command.c_str());
        }
        usage();
        return 1;
    }

    std::error_code ec;
    if (!std::filesystem::exists(args.opts.root, ec)) {
        std::fprintf(stderr, "error: path does not exist: %s\n", args.opts.root.c_str());
        return 1;
    }

    diskhopper::Scanner scanner;
    const auto start = std::chrono::steady_clock::now();
    diskhopper::ScanResult result = scanner.scan(args.opts.root);
    const auto end = std::chrono::steady_clock::now();
    const double seconds = std::chrono::duration<double>(end - start).count();

    if (!result.tree) {
        std::fprintf(stderr, "error: unable to scan '%s'\n", args.opts.root.c_str());
        return 1;
    }

    if (args.command == "report") {
        const std::filesystem::path home = diskhopper::home_directory();
        diskhopper::Classifier classifier(home,
                                          diskhopper::default_cleanup_rules(),
                                          diskhopper::default_protection_rules());
        classifier.apply(*result.tree);
        diskhopper::Report report =
            classifier.summarize(*result.tree, static_cast<size_t>(args.opts.top_n));
        print_report(result, report, diskhopper::default_protection_rules());
        return 0;
    }

    if (args.command == "explain") {
        const std::filesystem::path home = diskhopper::home_directory();
        diskhopper::Classifier classifier(home,
                                          diskhopper::default_cleanup_rules(),
                                          diskhopper::default_protection_rules());
        classifier.apply(*result.tree);
        print_explain(result, classifier, args.opts.top_n);
        return 0;
    }

    if (args.opts.json) {
        print_json(*result.tree, args.opts.max_depth);
        std::printf("\n");
        return 0;
    }

    print_summary(result, seconds);
    std::printf("\n");
    print_tree(*result.tree, 0, args.opts.max_depth);
    return 0;
}