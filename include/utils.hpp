#pragma once

/**
    TODO:
    Handle cross-platform. Features that are preventing this:
    Windows-only path resolve to program executable and music directory.
    Windows-only console clearing.
*/
#ifdef _WIN32
#include <Windows.h>

#include <ShlObj.h>
#else
#error "UNSUPPORTED OS."
#endif

#include <cstddef>
#include <filesystem>
#include <stdexcept>
#include <type_traits>

#define ERRORS                                                                                                         \
    E(FF_NOT_FOUND, "FFmpeg/FFprobe cannot be found on this system's path.")                                           \
    E(EXEC_PATH, "Cannot resolve path to executable.")                                                                 \
    E(MUSIC_PATH, "Cannot resolve path to current user's Music directory.")                                            \
    E(WRITE, "Encountered an error when writing to a file.")                                                           \
    E(READ, "Encountered an error when reading from a file.")                                                          \
    E(MINIAUDIO, "Encountered an error during audio setup/playback.")

namespace tml {

namespace fs = std::filesystem;

enum class Error : std::uint8_t {
#define E(code, msg) code,
    ERRORS
#undef E
};

constexpr const char *errorMessages[] = {
#define E(code, msg) msg,
    ERRORS
#undef E
};

inline void require(const bool condition, const Error err) {
    if (!condition) {
        throw std::runtime_error(errorMessages[static_cast<std::size_t>(err)]);
    }
}

inline fs::path getUserMusicDirectory() {
    PWSTR pPath{};
    HRESULT ret{SHGetKnownFolderPath(FOLDERID_Music, 0, nullptr, &pPath)};
    require(SUCCEEDED(ret), Error::MUSIC_PATH);
    fs::path mPath{pPath};
    CoTaskMemFree(pPath);
    return mPath;
}

inline fs::path getExecDirectory() {
    WCHAR buffer[MAX_PATH];
    DWORD ret{GetModuleFileNameW(nullptr, buffer, MAX_PATH)};
    require(ret != 0 && ret < MAX_PATH, Error::EXEC_PATH);
    return fs::path{buffer}.parent_path();
};

inline void clearConsole() { system("cls"); }

template <typename T, typename = std::enable_if_t<std::is_enum_v<T> && !std::is_convertible_v<T, int>>>
constexpr std::size_t szT(const T eVal) {
    return static_cast<std::size_t>(eVal);
}

} // namespace tml
