#include "diskhopper/cleaner/Cleaner.hpp"

#include <sys/stat.h>

#include <cerrno>
#include <cstring>
#include <ctime>
#include <fstream>
#include <functional>

namespace diskhopper {

namespace {

bool lstat_check(const std::filesystem::path& path, std::string& error) {
    struct stat st;
    if (::lstat(path.c_str(), &st) != 0) {
        error = "no longer exists: " + path.string();
        return false;
    }
    if (S_ISLNK(st.st_mode)) {
        error = "refusing symlink target: " + path.string();
        return false;
    }
    if (!S_ISDIR(st.st_mode) && !S_ISREG(st.st_mode)) {
        error = "unsupported file type: " + path.string();
        return false;
    }
    return true;
}

std::string timestamp() {
    std::time_t now = std::time(nullptr);
    std::tm tm{};
    ::localtime_r(&now, &tm);
    char buf[32];
    std::strftime(buf, sizeof buf, "%Y-%m-%d %H:%M:%S", &tm);
    return buf;
}

std::string replace_home(const std::filesystem::path& path) {
    const std::string value = path.string();
    const std::string home = getenv("HOME") ? getenv("HOME") : "";
    if (!home.empty() && value.rfind(home, 0) == 0) {
        return "~" + value.substr(home.size());
    }
    return value;
}

}  // namespace

CleanupSessionResult Cleaner::run(const std::vector<CleanupItem>& items,
                                  const Options& options) {
    CleanupSessionResult result;
    result.planned = items.size();

    auto refuse = [&](const CleanupItem& item, const std::string& note) {
        result.failed += 1;
        result.bytes_failed += item.bytes;
        result.items.push_back({item.path, item.rule_id, item.bytes,
                                item.action, false, note});
    };

    for (const auto& item : items) {
        std::string error;
        if (!lstat_check(item.path, error)) {
            refuse(item, error);
            continue;
        }
        if (options.protection_check &&
            !options.protection_check(item.path)) {
            refuse(item, "protected path rejected at delete time");
            continue;
        }
        if (item.action == CleanupActionKind::Delete) {
            if (!options.allow_permanent) {
                refuse(item, "permanent deletion disabled");
                continue;
            }
            if (options.permanent_gate && !options.permanent_gate()) {
                refuse(item, "safety gate blocked (Time Machine not found)");
                continue;
            }
        }
        if (item.action == CleanupActionKind::Trash) {
            if (!options.trash_fn) {
                refuse(item, "no trash provider configured");
                continue;
            }
            if (!options.trash_fn(item.path, error)) {
                refuse(item, error.empty() ? "trash failed" : error);
                continue;
            }
        } else {
            std::error_code ec;
            std::filesystem::remove_all(item.path, ec);
            if (ec) {
                refuse(item, ec.message());
                continue;
            }
        }
        std::error_code check_ec;
        if (std::filesystem::exists(item.path, check_ec)) {
            refuse(item, "path still present after cleanup");
            continue;
        }
        result.succeeded += 1;
        result.bytes_freed += item.bytes;
        result.items.push_back({item.path, item.rule_id, item.bytes,
                                item.action, true, "OK"});
    }

    if (!options.audit_path.empty()) {
        std::ofstream audit(options.audit_path, std::ios::app);
        if (audit) {
            const std::string ts = timestamp();
            audit << "diskhopper cleanup session " << ts << "\n";
            for (const auto& item : result.items) {
                const char* action =
                    item.action == CleanupActionKind::Trash ? "Trash" : "Delete";
                audit << "  " << (item.success ? "OK" : "FAIL") << "  "
                      << item.bytes << " bytes  " << action << "  "
                      << item.rule_id << "  " << replace_home(item.path) << "\n";
            }
        }
    }

    return result;
}

}  // namespace diskhopper