#pragma once

#include <filesystem>
#include <iostream>
#include <stdexcept>

#ifdef _DEBUG
#define START_TMR(name)                                                                                                \
    std::chrono::time_point name##_start { std::chrono::steady_clock::now() }
#define END_TMR(name)                                                                                                  \
    do {                                                                                                               \
        std::chrono::milliseconds name##_duration{                                                                       \
            std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - name##_start)       \
        };                                                                                                             \
        std::cout << #name << "_duration: " << name##_duration.count() << " ms\n";                                          \
    } while (0)
#endif

extern "C" {
#include <libavformat/avformat.h>
}

#define ERR_LIST                                                                                                       \
    E(GENERIC, "A generic exception has been thrown.")                                                                 \
    E(MA_INIT, "Miniaudio initialization encountered an error.")                                                       \
    E(DOES_NOT_EXIST, "File does not exist.")                                                                          \
    E(ALLOC, "Memory allocation failure.")                                                                             \
    E(FFMPEG_OPEN, "File cannot be opened.")                                                                           \
    E(FFMPEG_FILTER, "Filter graph failure.")                                                                          \
    E(FFMPEG_DECODE, "File decode failure.")                                                                           \
    E(INVALID_COMMAND, "Invalid command.")                                                                             \
    E(EXEC_PATH_RETRIEVAL, "Failure to retrieve path to executable.")                                                  \
    E(MUSIC_PATH_RETRIEVAL, "Failure to retrieve path to music directory.")                                            \
    E(READ, "Read failure.")                                                                                           \
    E(WRITE, "Write failure.")

namespace trm {

inline void clearConsole() { std::cout << "\033[2J\033[H" << std::flush; }

inline void showError(const std::string &errMsg) {
    clearConsole();
    std::cout << "\e[0;31m" << "EXCEPTION:\n" << errMsg << "\e[0m" << std::endl;
}

enum class Error : std::uint8_t {
#define E(err, str) err,
    ERR_LIST COUNT,
#undef E
};

constexpr const char *errMsg[static_cast<std::size_t>(Error::COUNT)] = {
#define E(err, str) str,
    ERR_LIST
#undef E
};

inline std::string asU8(const std::filesystem::path &path) {
    std::u8string u8{path.u8string()};
    return std::string{reinterpret_cast<const char *>(u8.data()), u8.length()};
}

inline std::u8string asU8(const std::string &str) {
    return std::u8string{reinterpret_cast<const char8_t *>(str.data()), str.length()};
}

inline std::string asChar(const std::u8string &str) {
    return std::string{reinterpret_cast<const char *>(str.data()), str.length()};
}

inline void require(const bool cond, const Error err) {
    if (!cond) [[unlikely]] {
        throw std::runtime_error(errMsg[static_cast<std::size_t>(err)]);
    }
}

inline float getFileDuration(const std::filesystem::path &path) {
    AVFormatContext *fctxRaw{};
    require(std::filesystem::exists(path), Error::DOES_NOT_EXIST);
    require(avformat_open_input(&fctxRaw, asU8(path).data(), nullptr, nullptr) >= 0, Error::FFMPEG_OPEN);
    std::unique_ptr<AVFormatContext, decltype([](AVFormatContext *f) { avformat_close_input(&f); })> fctx{};
    fctx.reset(fctxRaw);
    require(avformat_find_stream_info(fctx.get(), nullptr) >= 0, Error::FFMPEG_OPEN);
    int aStreamIdx = av_find_best_stream(fctx.get(), AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
    require(aStreamIdx >= 0, Error::FFMPEG_OPEN);
    return static_cast<float>(fctx->duration) / AV_TIME_BASE;
}

} // namespace trm
