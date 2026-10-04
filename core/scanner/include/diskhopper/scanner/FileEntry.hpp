#pragma once

#include <chrono>
#include <cstdint>
#include <filesystem>

namespace diskhopper {

enum class FileType {
    File,
    Directory,
    Symlink,
    Other,
};

struct FileEntry {
    std::filesystem::path path;
    uint64_t apparent = 0;
    uint64_t allocated = 0;
    FileType type = FileType::Other;
    std::chrono::system_clock::time_point modified{};
};

}