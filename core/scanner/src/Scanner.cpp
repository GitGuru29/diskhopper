#include "diskhopper/scanner/Scanner.hpp"

#include <sys/stat.h>

#include <cerrno>
#include <condition_variable>
#include <cstring>
#include <deque>
#include <mutex>
#include <thread>
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

struct NodeCtrl {
    DirNode* node;
    std::shared_ptr<NodeCtrl> parent;
    std::mutex mu;
    int pending_children = 0;
    bool done = false;
};

struct Job {
    std::filesystem::path dir;
    struct stat st;
    std::shared_ptr<NodeCtrl> ctrl;
};

constexpr size_t kMaxRecordedErrors = 100;

}  // namespace

ScanResult Scanner::scan(const std::filesystem::path& root,
                         const Options& options,
                         const FileCallback& on_file) {
    ScanResult result;
    result.root = root;

    std::error_code resolve_ec;
    std::filesystem::path resolved = std::filesystem::weakly_canonical(root, resolve_ec);
    if (resolve_ec || resolved.empty()) {
        resolved = root;
    }

    struct stat root_st;
    if (::lstat(resolved.c_str(), &root_st) != 0) {
        ++result.stats.errors;
        result.errors.emplace_back(resolved.string() + ": " + std::strerror(errno));
        return result;
    }
    if (S_ISLNK(root_st.st_mode)) {
        if (::stat(resolved.c_str(), &root_st) != 0) {
            ++result.stats.errors;
            result.errors.emplace_back(resolved.string() + ": " + std::strerror(errno));
            return result;
        }
    }
    if (!S_ISDIR(root_st.st_mode)) {
        ++result.stats.errors;
        result.errors.emplace_back(resolved.string() + ": not a directory");
        return result;
    }

    result.root = resolved;
    auto tree = std::make_unique<DirNode>();
    tree->path = resolved;
    tree->name = resolved.filename().empty() ? resolved.string() : resolved.filename().string();

    const size_t threads = options.threads == 0
                               ? std::max(1u, std::thread::hardware_concurrency())
                               : options.threads;
    const size_t workers = std::max<size_t>(1, threads);

    std::mutex shared_mu;
    std::condition_variable cv;
    std::deque<Job> jobs;
    std::unordered_set<InodeKey, InodeKeyHash> seen_inodes;
    seen_inodes.reserve(1u << 20);
    size_t pending_jobs = 0;

    auto record_error = [&](const std::string& message) {
        ++result.stats.errors;
        if (result.errors.size() < kMaxRecordedErrors) {
            result.errors.push_back(message);
        }
    };

    std::function<void(NodeCtrl*)> resolve_node;
    resolve_node = [&](NodeCtrl* ctrl) {
        NodeCtrl* cur = ctrl;
        while (cur != nullptr) {
            NodeCtrl* parent = cur->parent.get();
            if (parent == nullptr) {
                {
                    std::lock_guard<std::mutex> lock(shared_mu);
                    --pending_jobs;
                }
                cv.notify_all();
                return;
            }
            NodeCtrl* next = nullptr;
            {
                std::lock_guard<std::mutex> lock(parent->mu);
                parent->node->dir_count += cur->node->dir_count;
                parent->node->file_count += cur->node->file_count;
                parent->node->symlink_count += cur->node->symlink_count;
                parent->node->other_count += cur->node->other_count;
                parent->node->apparent += cur->node->apparent;
                parent->node->allocated += cur->node->allocated;
                --parent->pending_children;
                if (parent->done && parent->pending_children == 0) {
                    next = parent;
                }
            }
            {
                std::lock_guard<std::mutex> lock(shared_mu);
                --pending_jobs;
            }
            cv.notify_all();
            cur = next;
        }
    };

    std::function<void(const Job&)> scan_directory;
    scan_directory = [&](const Job& job) {
        DirNode& node = *job.ctrl->node;

        {
            std::lock_guard<std::mutex> lock(job.ctrl->mu);
            node.dir_count = 1;
            node.allocated += static_cast<uint64_t>(job.st.st_blocks) * 512ULL;
            node.apparent += static_cast<uint64_t>(job.st.st_size);
        }

        std::error_code ec;
        auto it = std::filesystem::directory_iterator(job.dir, ec);
        if (ec) {
            {
                std::lock_guard<std::mutex> lock(shared_mu);
                record_error(job.dir.string() + ": " + ec.message());
            }
        } else {
            for (std::filesystem::directory_iterator end; it != end; it.increment(ec)) {
                if (ec) {
                    {
                        std::lock_guard<std::mutex> lock(shared_mu);
                        record_error(job.dir.string() + ": " + ec.message());
                    }
                    ec.clear();
                    continue;
                }
                const auto& entry = *it;

                struct stat st;
                if (::lstat(entry.path().c_str(), &st) != 0) {
                    {
                        std::lock_guard<std::mutex> lock(shared_mu);
                        record_error(entry.path().string() + ": " + std::strerror(errno));
                    }
                    continue;
                }

                const FileType type = classify(st);

                if (type == FileType::Directory) {
                    Job child_job;
                    {
                        std::lock_guard<std::mutex> lock(job.ctrl->mu);
                        node.items.push_back(ChildItem{entry.path().filename().string(), type});
                        auto child = std::make_unique<DirNode>();
                        child->path = entry.path();
                        child->name = entry.path().filename().string();
                        auto child_ctrl = std::make_shared<NodeCtrl>();
                        child_ctrl->node = child.get();
                        child_ctrl->parent = job.ctrl;
                        ++job.ctrl->pending_children;
                        node.children.push_back(std::move(child));
                        child_job = Job{entry.path(), st, std::move(child_ctrl)};
                    }
                    {
                        std::lock_guard<std::mutex> lock(shared_mu);
                        ++pending_jobs;
                        jobs.push_back(std::move(child_job));
                    }
                    cv.notify_one();
                    continue;
                }

                bool first = true;
                if (type == FileType::File) {
                    std::lock_guard<std::mutex> lock(shared_mu);
                    if (options.dedupe_hardlinks) {
                        first = seen_inodes.insert(InodeKey{st.st_dev, st.st_ino}).second;
                        if (!first) {
                            ++result.stats.hardlink_deduped;
                        }
                    }
                }

                {
                    std::lock_guard<std::mutex> lock(job.ctrl->mu);
                    node.items.push_back(ChildItem{entry.path().filename().string(), type});
                    switch (type) {
                        case FileType::File:
                            if (first) {
                                node.apparent += static_cast<uint64_t>(st.st_size);
                                node.allocated +=
                                    static_cast<uint64_t>(st.st_blocks) * 512ULL;
                            }
                            ++node.file_count;
                            break;
                        case FileType::Symlink:
                            ++node.symlink_count;
                            break;
                        default:
                            ++node.other_count;
                            break;
                    }
                }
                if (type == FileType::File && on_file) {
                    on_file(make_entry(entry.path(), st));
                }
            }
        }

        bool resolve_self = false;
        {
            std::lock_guard<std::mutex> lock(job.ctrl->mu);
            job.ctrl->done = true;
            if (job.ctrl->pending_children == 0) {
                resolve_self = true;
            }
        }
        if (resolve_self) {
            resolve_node(job.ctrl.get());
        }
    };

    auto root_ctrl = std::make_shared<NodeCtrl>();
    root_ctrl->node = tree.get();
    root_ctrl->parent = nullptr;
    {
        std::lock_guard<std::mutex> lock(shared_mu);
        ++pending_jobs;
        jobs.push_back(Job{resolved, root_st, root_ctrl});
    }

    std::vector<std::thread> pool;
    pool.reserve(workers);
    for (size_t i = 0; i < workers; ++i) {
        pool.emplace_back([&]() {
            while (true) {
                Job job;
                {
                    std::unique_lock<std::mutex> lock(shared_mu);
                    cv.wait(lock, [&] {
                        return !jobs.empty() || pending_jobs == 0;
                    });
                    if (jobs.empty() && pending_jobs == 0) break;
                    job = std::move(jobs.front());
                    jobs.pop_front();
                }
                scan_directory(job);
            }
        });
    }

    for (auto& thread : pool) {
        thread.join();
    }

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