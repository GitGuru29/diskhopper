#pragma once

#include <filesystem>

namespace diskhopper {

std::filesystem::path home_directory();
std::filesystem::path expand_home(const std::filesystem::path& input,
                                  const std::filesystem::path& home);

}