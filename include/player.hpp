#pragma once

#include "miniaudio.h"
#include <array>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <mutex>
#include <nlohmann/detail/macro_scope.hpp>
#include <nlohmann/json.hpp>
#include <optional>
#include <unordered_map>
#include <vector>

namespace tml {

constexpr std::size_t commandQueueLength{5};
constexpr std::size_t sampleRate{48000};
constexpr std::size_t channels{2};
constexpr std::chrono::milliseconds rBufLatency{100};
constexpr std::size_t rBufferLength{sampleRate * channels / (1000 / rBufLatency.count())};
using s16_le = std::int16_t;

enum class CommandType : std::uint8_t { PLAY_FILE, PAUSE_FILE, MUTE_FILE, SEEK_TO, SET_VOL, INC_VOL, DEC_VOL, NONE };

/**
    Values are interpreted based on `CommandType`. `VOL/SEEK_TO` uses `float`. `MUTE/PAUSE` uses `bool`.
    `PLAY_FILE` uses `szT`.
*/

struct Command {
    CommandType comType{};
    std::optional<bool> b{};
    std::optional<float> f{};
    std::optional<std::size_t> sz{};
    Command(
        const CommandType comType = CommandType::NONE, const bool b = false, const float f = 0.0,
        const std::size_t sz = 0
    )
        : comType{comType}, b{b}, f{f}, sz{sz} {};
};

struct AudioEntry {
    std::uint32_t uid{};
    std::filesystem::path filePath{};
    float duration{};
    std::uint32_t tPlayed{};        // Considered played after 3s.
    std::uint32_t tSkipped{};       // Must be played (>3s) then skipped.
    std::vector<float> signature{}; // Audio signature.
    float avgPlaytime{};            // Avg time before skipped.
    NLOHMANN_DEFINE_TYPE_INTRUSIVE(AudioEntry, uid, filePath, duration, tPlayed, tSkipped, signature, avgPlaytime);
    AudioEntry() {};
    AudioEntry(std::filesystem::path fPath, std::uint32_t uid) : uid{uid}, filePath{fPath} {};
};

struct PlayerInternalState {
    bool terminate{};
    std::size_t rComIdx{};
    std::size_t wComIdx{};
    std::size_t rBufIdx{};
    std::size_t wBufIdx{};
    std::vector<s16_le> rBuffer{};
    std::array<Command, commandQueueLength> commands{};
    std::condition_variable cond{};
    std::mutex mutex{};
    PlayerInternalState();
};

struct PlayerStateSnapshot {
    float timestamp{};
    bool playing{};
    bool muted{};
    float cVol{};
    std::size_t cFileIdx{};
};

struct PlayerState {
    std::atomic<float> timestamp{};
    std::atomic<bool> playing{};
    std::atomic<bool> muted{};
    std::atomic<float> cVol{};
    std::atomic<std::size_t> cFileIdx{};
};

class Player {
    std::unordered_map<std::uint32_t, AudioEntry> entries{};
    PlayerInternalState internalState{};
    PlayerState state{};
    std::thread thread{};
    void pThread();
    void sendCommand(const Command com);

  public:
    Player();
    Player(const Player &) = delete;
    Player operator=(const Player &) = delete;
    Player(Player &&) = delete;
    Player operator=(Player &&) = delete;
    ~Player();
    const std::unordered_map<std::uint32_t, AudioEntry> &getEntries() const;
    PlayerStateSnapshot getState() const;
    void run();
    void playFile(const std::size_t idx);
    void pauseFile();
    void muteFile();
    void seekFile(const std::chrono::milliseconds ms);
    void setVol(const float nVol = 1.0);
    void incVol(const float iVal = 0.01);
    void decVol(const float dVal = 0.01);
};

} // namespace tml