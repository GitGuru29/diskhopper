#include "diskhopper/platform/Trash.hpp"

#include <CoreServices/CoreServices.h>

namespace diskhopper {

bool move_to_trash(const std::filesystem::path& path, std::string& error) {
    const std::string text = path.string();
    const OSStatus status = FSPathMoveObjectToTrashSync(
        text.c_str(), nullptr, kFSFileOperationDefaultOptions);
    if (status == noErr || status == userCanceledErr) return true;
    error = "FSPathMoveObjectToTrashSync failed with status ";
    error += std::to_string(static_cast<long>(status));
    return false;
}

bool time_machine_available() {
    CFArrayRef destinations = static_cast<CFArrayRef>(
        CFPreferencesCopyAppValue(CFSTR("Destinations"),
                                  CFSTR("com.apple.TimeMachine")));
    bool available = false;
    if (destinations != nullptr) {
        available = CFGetTypeID(destinations) == CFArrayGetTypeID() &&
                    CFArrayGetCount(destinations) > 0;
        CFRelease(destinations);
    }
    return available;
}

}