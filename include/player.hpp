#pragma once

#include <array>
#include <condition_variable>
#include <cstddef>
#include <filesystem>
#include <mutex>
#include <nlohmann/json.hpp>
#include <string>
#include <thread>
#include <vector>

#include "utils.hpp"

namespace tml {

constexpr std::size_t sigSize{8};
constexpr std::size_t comQueueLen{5};
constexpr std::size_t cStyleBufferLimit{512};

struct Entry {
    std::string u8filePath{};
    std::vector<float> sig{};
    std::uint32_t timesPlayed{};
    std::uint32_t timesSkipped{};
    float avgPlaytime{};

    fs::path asPath() const {
        return fs::path(std::u8string{reinterpret_cast<const char8_t *>(u8filePath.data()), u8filePath.length()});
    };
    NLOHMANN_DEFINE_TYPE_INTRUSIVE(Entry, u8filePath, sig, timesPlayed, timesSkipped, avgPlaytime);
    Entry() { sig.resize(sigSize); };
    Entry(const fs::path &path) {
        std::u8string u8str{path.u8string()};
        u8filePath = std::string{reinterpret_cast<const char *>(u8str.data()), u8str.length()};
        sig.resize(sigSize);
    };
};

enum class CommandType : std::uint8_t {
    PLAY_ENTRY,
    STOP_CURRENT,
    VOL_SET,
    VOL_UP,
    VOL_DOWN,
    SEEK_BACKWARD,
    SEEK_FORWARD,
    SEEK_TO,
    TOGGLE_PLAYBACK,
    TOGGLE_MUTE,
    TOGGLE_LOOP,
    COUNT,
    NONE,
};

struct Command {
    CommandType comType{CommandType::NONE};
    float fVal{};
    Entry ent{};
    Command(const CommandType c = CommandType::NONE, const float f = 0.0f, const Entry e = Entry{})
        : comType{c}, fVal{f}, ent{e} {};
};

struct AudioState {
    // Lock-free.
    std::atomic<std::size_t> serial{};
    std::atomic<std::chrono::duration<float>> timestamp{};
    std::atomic<bool> terminate{};
    std::atomic<bool> muted{};
    std::atomic<bool> looped{};
    std::atomic<bool> playback{};
    std::atomic<float> volume{};

    // Mutex-protected.
    std::size_t commandW{};
    std::size_t commandR{};
    std::array<Command, comQueueLen> commandQueue{};
    std::condition_variable conVar{};
    std::mutex mutex{};
};

class Audio {
    AudioState state{};
    std::thread producerThread{};
    void pthread();
    void sendCommand(const Command command);

  public:
    static const std::uint32_t sampleRate{48'000};
    static constexpr std::chrono::duration<float> samplePeriod{1 / sampleRate};
    static const std::uint32_t channels{2};
    AudioState &getState() { return state; };
    void run();
    void seekTo(const float v = 0.0f);
    void seekForward(const float v = 1.0f);
    void seekBackward(const float v = 1.0f);
    void volUp(const float v = 1.0f);
    void volDown(const float v = 1.0f);
    void volSet(const float v = 1.0f);
    void playEntry(const Entry &entry, const float v = 1.0f);
    void toggleMute();
    void togglePlayback();
    void toggleLooping();
    void stopCurrent();
    Audio();
    Audio &operator=(const Audio &) = delete;
    Audio(const Audio &) = delete;
    Audio &operator=(Audio &&) = delete;
    Audio(Audio &&) = delete;
    ~Audio();
};

struct PlayerConfig {
    bool visualization{};
    bool loopByDefault{};
    std::vector<fs::path> scanPaths{};
    std::uint8_t defaultVolume{};
};

class Player {
    PlayerConfig config{};
    Audio aud{};
    fs::path execPath{getExecDirectory()};
    fs::path configPath{execPath / "config.yaml"};
    fs::path dataPath{execPath / "data.json"};
    std::vector<Entry> fileEntries{};

  public:
    Player();
    void run();
};

} // namespace tml