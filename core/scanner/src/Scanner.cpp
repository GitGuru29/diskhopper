#include "diskhopper/scanner/Scanner.hpp"

#include <sys/stat.h>

#include <cerrno>
#include <cstring>
#include <functional>
#include <unordered_set>

namespace diskhopper {

namespace {

#if defined(__APPLE__)
#define DH_MTIME st_mtimespec
#else
#define DH_MTIME st_mtim
#endif

struct InodeKey {
    dev_t dev;
    ino_t ino;
    bool operator==(const InodeKey& other) const {
        return dev == other.dev && ino == other.ino;
    }
};

struct InodeKeyHash {
    size_t operator()(const InodeKey& key) const {
        return std::hash<dev_t>()(key.dev) ^ (std::hash<ino_t>()(key.ino) << 1);
    }
};

FileType classify(const struct stat& st) {
    if (S_ISREG(st.st_mode)) return FileType::File;
    if (S_ISDIR(st.st_mode)) return FileType::Directory;
    if (S_ISLNK(st.st_mode)) return FileType::Symlink;
    return FileType::Other;
}

FileEntry make_entry(const std::filesystem::path& path, const struct stat& st) {
    FileEntry entry;
    entry.path = path;
    entry.type = classify(st);
    entry.apparent = static_cast<uint64_t>(st.st_size);
    entry.allocated = static_cast<uint64_t>(st.st_blocks) * 512ULL;
    entry.modified = std::chrono::system_clock::from_time_t(st.DH_MTIME.tv_sec);
    return entry;
}

}

ScanResult Scanner::scan(const std::filesystem::path& root,
                         const Options& options,
                         const FileCallback& on_file) {
    ScanResult result;
    result.root = root;

    struct stat root_st;
    if (::lstat(root.c_str(), &root_st) != 0) {
        ++result.stats.errors;
        result.errors.emplace_back(root.string() + ": " + std::strerror(errno));
        return result;
    }
    if (S_ISLNK(root_st.st_mode)) {
        if (::stat(root.c_str(), &root_st) != 0) {
            ++result.stats.errors;
            result.errors.emplace_back(root.string() + ": " + std::strerror(errno));
            return result;
        }
    }
    if (!S_ISDIR(root_st.st_mode)) {
        ++result.stats.errors;
        result.errors.emplace_back(root.string() + ": not a directory");
        return result;
    }

    auto tree = std::make_unique<DirNode>();
    tree->path = root;
    tree->name = root.filename().empty() ? root.string() : root.filename().string();

    std::unordered_set<InodeKey, InodeKeyHash> seen_inodes;
    constexpr size_t kMaxRecordedErrors = 100;

    auto record_error = [&](const std::string& message) {
        ++result.stats.errors;
        if (result.errors.size() < kMaxRecordedErrors) {
            result.errors.push_back(message);
        }
    };

    std::function<void(const std::filesystem::path&, DirNode&, const struct stat&)> walk;

    walk = [&](const std::filesystem::path& dir, DirNode& node, const struct stat& dst) {
        ++result.stats.dir_count;
        node.dir_count = 1;
        node.allocated += static_cast<uint64_t>(dst.st_blocks) * 512ULL;
        node.apparent += static_cast<uint64_t>(dst.st_size);

        std::error_code ec;
        auto it = std::filesystem::directory_iterator(dir, ec);
        if (ec) {
            record_error(dir.string() + ": " + ec.message());
            return;
        }

        for (std::filesystem::directory_iterator end; it != end; it.increment(ec)) {
            if (ec) {
                record_error(dir.string() + ": " + ec.message());
                ec.clear();
                continue;
            }
            const auto& entry = *it;

            struct stat st;
            if (::lstat(entry.path().c_str(), &st) != 0) {
                record_error(entry.path().string() + ": " + std::strerror(errno));
                continue;
            }

            switch (classify(st)) {
                case FileType::Directory: {
                    auto child = std::make_unique<DirNode>();
                    child->path = entry.path();
                    child->name = entry.path().filename().string();
                    walk(entry.path(), *child, st);
                    node.dir_count += child->dir_count;
                    node.file_count += child->file_count;
                    node.symlink_count += child->symlink_count;
                    node.other_count += child->other_count;
                    node.apparent += child->apparent;
                    node.allocated += child->allocated;
                    node.children.push_back(std::move(child));
                    break;
                }
                case FileType::File: {
                    const uint64_t apparent = static_cast<uint64_t>(st.st_size);
                    const uint64_t allocated = static_cast<uint64_t>(st.st_blocks) * 512ULL;
                    if (!options.dedupe_hardlinks ||
                        seen_inodes.insert(InodeKey{st.st_dev, st.st_ino}).second) {
                        node.apparent += apparent;
                        node.allocated += allocated;
                    } else {
                        ++result.stats.hardlink_deduped;
                    }
                    ++node.file_count;
                    if (on_file) {
                        on_file(make_entry(entry.path(), st));
                    }
                    break;
                }
                case FileType::Symlink:
                    ++node.symlink_count;
                    break;
                default:
                    ++node.other_count;
                    break;
            }
        }
    };

    walk(root, *tree, root_st);

    result.stats.dir_count = tree->dir_count;
    result.stats.file_count = tree->file_count;
    result.stats.symlink_count = tree->symlink_count;
    result.stats.other_count = tree->other_count;
    result.apparent = tree->apparent;
    result.allocated = tree->allocated;
    result.tree = std::move(tree);

    return result;
}

}