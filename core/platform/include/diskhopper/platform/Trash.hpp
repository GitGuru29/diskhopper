#pragma once

#include <filesystem>
#include <string>

namespace diskhopper {

bool move_to_trash(const std::filesystem::path& path, std::string& error);
bool time_machine_available();

}