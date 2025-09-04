#ifdef _WIN32
#include <Windows.h>
#else
#error "UNSUPPORTED OS"
#endif
#include <cstddef>
#include <filesystem>
#include <stdexcept>

#define ERROR_LIST                                                                                                     \
    E(EXEC_PATH_ERROR, "Unable to get path to process executable.")                                                    \
    E(CONFIG_PATH_ERROR, "Unable to find configuration file.")                                                         \
    E(CONFIG_READ_ERROR, "Unable to read from configuration file.")                                                    \
    E(MUSIC_PATH_ERROR, "Unable to get path to user's Music/ folder.")                                                 \
    E(CONFIG_WRITE_ERROR, "Unable to write to configuration file.")                                                    \
    E(CONFIG_ARGUMENT_PATH_ERROR, "Invalid path found in configuration file.")                                         \
    E(DATA_WRITE_ERROR, "Unable to write to data file.")                                                               \
    E(DATA_PATH_ERROR, "Unable to find data file.") \
    E(DATA_READ_ERROR, "Unable to read from data file.")

namespace tml {

enum class Error : std::size_t {
#define E(code, message) code,
    ERROR_LIST
#undef E
};

constexpr const char *messages[]{
#define E(code, message) "EXCEPTION: " message,
    ERROR_LIST
#undef E
};

inline void require(const bool cond, const Error err) {
    if (!cond) {
        throw std::runtime_error(messages[static_cast<std::size_t>(err)]);
    }
};

constexpr std::size_t bufSize{MAX_PATH};
inline std::filesystem::path getExecPath() {
    WCHAR fName[bufSize]{};
    const DWORD ret{GetModuleFileNameW(NULL, fName, bufSize)};
    require(ret != 0 && ret < bufSize, Error::EXEC_PATH_ERROR);
    return std::filesystem::path(fName);
}

} // namespace tml