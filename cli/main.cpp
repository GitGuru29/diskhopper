#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

#include "diskhopper/scanner/Scanner.hpp"

namespace {

const char* kVersion = "0.1.0";

struct Options {
    std::filesystem::path root = ".";
    int max_depth = 2;
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
        "\n"
        "Commands:\n"
        "  scan         Scan a directory tree and report sizes\n"
        "\n"
        "Options:\n"
        "  --depth N    Max tree depth to print (default 2)\n"
        "  --json       Emit machine-readable JSON\n"
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
        if (arg == "scan") {
            out.command = arg;
        } else if (arg == "--depth") {
            if (i + 1 >= argc) {
                std::fprintf(stderr, "error: --depth requires a value\n");
                out.ok = false;
                return out;
            }
            out.opts.max_depth = std::atoi(argv[++i]);
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
    if (args.command != "scan") {
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