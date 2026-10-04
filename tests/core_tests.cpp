#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <string>

#include "diskhopper/classifier/Classifier.hpp"
#include "diskhopper/cleaner/Cleaner.hpp"
#include "diskhopper/rules/CleanupRule.hpp"
#include "diskhopper/scanner/Scanner.hpp"

namespace dh = diskhopper;
namespace fs = std::filesystem;

namespace {

int failures = 0;
int checks = 0;

void check(bool condition, const std::string& message) {
    ++checks;
    if (!condition) {
        ++failures;
        std::printf("FAIL: %s\n", message.c_str());
    }
}

void write_bytes(const fs::path& path, uint64_t bytes) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    char buffer[4096];
    std::memset(buffer, 0, sizeof(buffer));
    while (bytes > 0) {
        const size_t n = bytes > sizeof(buffer) ? sizeof(buffer) : static_cast<size_t>(bytes);
        out.write(buffer, static_cast<std::streamsize>(n));
        bytes -= n;
    }
}

fs::path build_fixture() {
    fs::path root = fs::temp_directory_path() / "diskhopper_tests";
    std::error_code ec;
    fs::remove_all(root, ec);
    fs::create_directories(root / "Library/Caches/com.google.Chrome", ec);
    fs::create_directories(root / "Library/Developer/Xcode/DerivedData", ec);
    fs::create_directories(root / "Documents/notes", ec);
    fs::create_directories(root / "Downloads/AeroDrop/src", ec);
    fs::create_directories(root / "Downloads/AeroDrop/.git", ec);
    fs::create_directories(root / "project/.git", ec);
    fs::create_directories(root / "project/src", ec);
    fs::create_directories(root / "node_modules/package", ec);
    fs::create_directories(root / "plain", ec);

    write_bytes(root / "big.dat", 5u * 1024 * 1024);
    fs::create_hard_link(root / "big.dat", root / "hardlink.dat", ec);
    std::ofstream(root / "Library/Caches/com.google.Chrome/entry", std::ios::binary) << "x";
    std::ofstream(root / "Library/Developer/Xcode/DerivedData/entry", std::ios::binary) << "x";
    std::ofstream(root / "Documents/notes/note.txt", std::ios::binary) << "note";
    std::ofstream(root / "Downloads/AeroDrop/src/main.c", std::ios::binary) << "main";
    std::ofstream(root / "project/src/main.c", std::ios::binary) << "main";
    std::ofstream(root / "node_modules/package/index.js", std::ios::binary) << "index";
    std::ofstream(root / "plain/data.bin", std::ios::binary) << "data";
    fs::create_directory_symlink(root, root / "loop", ec);
    return root;
}

void test_scan() {
    fs::path root = build_fixture();
    dh::Scanner scanner;
    dh::ScanResult result = scanner.scan(root);

    check(result.stats.file_count == 9, "scan: expected 9 files");
    check(result.stats.dir_count == 19, "scan: expected 19 directories");
    check(result.stats.symlink_count == 1, "scan: symlink counted, not followed");
    check(result.stats.hardlink_deduped == 1, "scan: hard link deduplicated");
    check(result.allocated >= 5u * 1024 * 1024, "scan: allocated counts big file");
    check(result.allocated < 6u * 1024 * 1024, "scan: hard link not double counted");
    check(result.stats.errors == 0, "scan: no errors");
    std::error_code canonical_ec;
    fs::path expected_root = fs::weakly_canonical(root, canonical_ec);
    check(result.tree->path == expected_root, "scan: root canonicalized");
}

const dh::DirNode* find_dir(const dh::DirNode& node, const std::string& name) {
    for (const auto& child : node.children) {
        if (child->name == name) return child.get();
    }
    return nullptr;
}

void test_classification() {
    fs::path root = build_fixture();
    dh::Scanner scanner;
    dh::ScanResult result = scanner.scan(root);

    dh::Classifier classifier(root, dh::default_cleanup_rules(), dh::default_protection_rules());
    classifier.apply(*result.tree);
    dh::Report report = classifier.summarize(*result.tree, 10);

    const dh::DirNode* chrome = find_dir(*find_dir(*find_dir(*result.tree, "Library"), "Caches"), "com.google.Chrome");
    check(chrome != nullptr && chrome->level == dh::SafetyLevel::Safe, "class: Chrome cache SAFE");

    const dh::DirNode* derived = find_dir(*find_dir(*find_dir(*find_dir(*result.tree, "Library"), "Developer"), "Xcode"), "DerivedData");
    check(derived != nullptr && derived->level == dh::SafetyLevel::Review, "class: DerivedData REVIEW");

    const dh::DirNode* documents = find_dir(*result.tree, "Documents");
    check(documents != nullptr && documents->level == dh::SafetyLevel::Protected, "class: Documents PROTECTED");

    const dh::DirNode* project = find_dir(*result.tree, "project");
    check(project != nullptr && project->level == dh::SafetyLevel::Protected, "class: project PROTECTED");
    check(project != nullptr && project->rule_id == "project.root", "class: project detected by marker");
    const dh::DirNode* project_src = project ? find_dir(*project, "src") : nullptr;
    check(project_src != nullptr && project_src->level == dh::SafetyLevel::Protected,
          "class: inside project protected");

    const dh::DirNode* aerodrop = find_dir(*find_dir(*result.tree, "Downloads"), "AeroDrop");
    check(aerodrop != nullptr && aerodrop->level == dh::SafetyLevel::Protected,
          "class: AeroDrop protected (Downloads + project)");

    const dh::DirNode* node_modules = find_dir(*result.tree, "node_modules");
    check(node_modules != nullptr && node_modules->level == dh::SafetyLevel::Review,
          "class: node_modules REVIEW");

    const dh::DirNode* plain = find_dir(*result.tree, "plain");
    check(plain != nullptr && plain->level == dh::SafetyLevel::Protected,
          "class: unclassified protected by default");
    check(plain != nullptr && plain->direct_match, "class: unclassified direct match");

    check(report.safe.find("chrome-cache") != report.safe.end(), "report: chrome-cache present");
    check(report.safe["chrome-cache"].bytes > 0, "report: chrome-cache bytes > 0");
    check(report.review.find("node_modules") != report.review.end(), "report: node_modules present");
    check(report.protected_roots.find("prot-documents") != report.protected_roots.end(),
          "report: Documents in protected roots");
    check(report.safe_bytes + report.review_bytes == report.safe["chrome-cache"].bytes +
              report.review["node_modules"].bytes + report.review["all-caches"].bytes +
              report.review["xcode-data"].bytes + report.review["xcode-derived"].bytes,
          "report: cleanable total is sum of rule totals");
}

void test_cleaner() {
    fs::path root = build_fixture();
    dh::Scanner scanner;
    dh::ScanResult result = scanner.scan(root);
    dh::Classifier classifier(root, dh::default_cleanup_rules(),
                              dh::default_protection_rules());
    classifier.apply(*result.tree);

    check(classifier.is_protected(root / "Documents"), "cleaner: Documents protected");
    check(classifier.is_protected(root / "Downloads/AeroDrop"),
          "cleaner: project inside Downloads protected");

    fs::path trash_dir = root / "TRASH";
    fs::create_directories(trash_dir);
    dh::Cleaner::Options options;
    options.allow_permanent = true;
    options.permanent_gate = [] { return true; };
    options.protection_check = [&](const fs::path& path) {
        return path != (root / "cleanable");
    };
    options.trash_fn = [&](const fs::path& path, std::string& error) {
        std::error_code ec;
        fs::rename(path, trash_dir / path.filename(), ec);
        if (ec) {
            error = ec.message();
            return false;
        }
        return true;
    };
    options.audit_path = root / "audit.log";

    fs::path trash_target = root / "trash_me";
    fs::path delete_target = root / "delete_me";
    fs::path blocked = root / "cleanable";
    fs::create_directories(trash_target);
    fs::create_directories(delete_target);
    fs::create_directories(blocked);
    write_bytes(trash_target / "a.dat", 1024);
    write_bytes(delete_target / "b.dat", 2048);
    write_bytes(blocked / "keep.dat", 4096);

    std::vector<dh::CleanupItem> items;
    items.push_back({trash_target, "trash-rule", "Trash Rule",
                     dh::SafetyLevel::Review, 1024,
                     dh::CleanupActionKind::Trash});
    items.push_back({delete_target, "delete-rule", "Delete Rule",
                     dh::SafetyLevel::Safe, 2048,
                     dh::CleanupActionKind::Delete});
    items.push_back({blocked, "blocked-rule", "Blocked Rule",
                     dh::SafetyLevel::Review, 123,
                     dh::CleanupActionKind::Trash});

    dh::CleanupSessionResult result_run = dh::Cleaner::run(items, options);
    check(result_run.succeeded == 2, "cleaner: two items succeeded");
    check(result_run.failed == 1, "cleaner: protected item refused");
    check(result_run.bytes_freed == 3072, "cleaner: freed bytes accounted");
    check(result_run.bytes_failed == 123, "cleaner: failed bytes accounted");
    check(!fs::exists(trash_target), "cleaner: trashed target gone");
    check(!fs::exists(delete_target), "cleaner: deleted target gone");
    check(fs::exists(trash_dir / "trash_me"), "cleaner: trash received file");
    check(fs::exists(blocked), "cleaner: protected item untouched");
    check(fs::exists(root / "audit.log"), "cleaner: audit log written");
    {
        std::ifstream audit(root / "audit.log");
        std::string contents((std::istreambuf_iterator<char>(audit)),
                             std::istreambuf_iterator<char>());
        check(contents.find("delete-rule") != std::string::npos,
              "cleaner: audit records deleted path");
        check(contents.find("FAIL") != std::string::npos,
              "cleaner: audit records refusal");
    }

    dh::Cleaner::Options refused;
    refused.allow_permanent = false;
    refused.trash_fn = options.trash_fn;
    dh::Cleaner::Options symlink_only;
    symlink_only.allow_permanent = true;
    symlink_only.trash_fn = options.trash_fn;

    items.clear();
    fs::path perm_target = root / "perm_me";
    fs::create_directories(perm_target);
    write_bytes(perm_target / "c.dat", 512);
    items.push_back({perm_target, "delete-rule", "Delete Rule",
                     dh::SafetyLevel::Safe, 1,
                     dh::CleanupActionKind::Delete});
    dh::CleanupSessionResult permanent_refused = dh::Cleaner::run(items, refused);
    check(permanent_refused.failed == 1 &&
              permanent_refused.items[0].note == "permanent deletion disabled",
          "cleaner: permanent deletion requires allow_permanent");
    check(fs::exists(perm_target), "cleaner: disabled permanent delete left target");

    items.clear();
    items.push_back({root / "no_such_path", "missing", "Missing",
                     dh::SafetyLevel::Review, 1,
                     dh::CleanupActionKind::Trash});
    dh::CleanupSessionResult missing =
        dh::Cleaner::run(items, symlink_only);
    check(missing.failed == 1 && missing.items[0].note.find("no longer exists") != std::string::npos,
          "cleaner: missing path refused");

    items.clear();
    items.push_back({root / "loop", "symlink", "Symlink",
                     dh::SafetyLevel::Review, 1,
                     dh::CleanupActionKind::Trash});
    dh::CleanupSessionResult symlink_res =
        dh::Cleaner::run(items, symlink_only);
    check(symlink_res.failed == 1 &&
              symlink_res.items[0].note.find("symlink") != std::string::npos,
          "cleaner: symlink target refused");
    check(fs::exists(root / "loop"), "cleaner: symlink untouched (loop survives)");
}

}

int main() {
    test_scan();
    test_classification();
    test_cleaner();
    std::printf("core_tests: %d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}