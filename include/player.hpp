#pragma once

#include <array>
#include <condition_variable>
#include <cstddef>
#include <filesystem>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <ftxui/component/component.hpp>
#include <ftxui/component/screen_interactive.hpp>

#include <nlohmann/json.hpp>

#include "utils.hpp"
#include "ui.hpp"

namespace tml {

constexpr std::size_t sigSize{8};
constexpr std::size_t comQueueLen{5};
constexpr std::size_t cStyleBufferLimit{512};

struct Entry {
    std::string u8filePath{};
    std::vector<float> sig{};
    std::uint32_t timesPlayed{};
    std::uint32_t timesSkipped{};
    std::uint64_t timeModified{};
    std::uint64_t lastPlayed{};
    float avgPlaytime{};
    NLOHMANN_DEFINE_TYPE_INTRUSIVE(
        Entry, u8filePath, sig, timesPlayed, timesSkipped, timeModified, lastPlayed, avgPlaytime
    );
    fs::path asPath() const {
        return fs::path(std::u8string{reinterpret_cast<const char8_t *>(u8filePath.data()), u8filePath.length()});
    };
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

struct AudioMetadata {
    float duration{};
};

struct AudioState {
    
    // Lock-free.
    std::atomic<std::chrono::duration<float>> timestamp{};

    /**
        NOTE:
        ended has to be initialized to false.
        autoplay feature depends on this set to false 
        so as to not immediately play the first track.
    */
    std::atomic<bool> ended{};
    std::atomic<bool> terminate{};
    std::atomic<bool> muted{};
    std::atomic<bool> looped{};
    std::atomic<bool> playback{};
    std::atomic<float> volume{};
    std::atomic<std::size_t> wIdx{};
    std::atomic<std::size_t> rIdx{};
    std::vector<std::int16_t> buffer{};

    // Mutex-protected.
    std::size_t commandW{};
    std::size_t commandR{};
    std::array<Command, comQueueLen> commandQueue{};
    std::condition_variable conVar{};
    std::mutex mutex{};
    EntryId currentEntry{};
};

class Audio {
    AudioState state{};
    AudioMetadata metadata;
    std::thread producerThread{};
    void pthread();
    void sendCommand(const Command command);

  public:
    static constexpr std::uint32_t sampleRate{48'000};
    static constexpr std::chrono::duration<float> samplePeriod{1 / sampleRate};
    static constexpr std::uint32_t channels{2};
    static constexpr std::uint32_t sampleBufferSize{static_cast<uint32_t>(sampleRate * channels * 0.05)};
    AudioState &getState() { return state; };
    float getDuration() const { return metadata.duration; };
    void run(const bool loopDefault = false, const std::uint8_t volume = 100);
    void seekTo(const float v = 0.0f);
    void seekForward(const float v = 1.0f);
    void seekBackward(const float v = 1.0f);
    void volUp(const float v = 1.0f);
    void volDown(const float v = 1.0f);
    void volSet(const float v = 1.0f);
    void playEntry(const Entry &entry);
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

struct Playlist {
    std::string playlistName{};
    std::vector<std::string> playlistEntries{};
    NLOHMANN_DEFINE_TYPE_INTRUSIVE(Playlist, playlistName, playlistEntries);
    Playlist() {};
    Playlist(std::string name, std::vector<std::string> entries) : playlistName{name}, playlistEntries{entries} {};
};

struct PlaylistCompact {
    std::string playlistName{};
    std::vector<EntryId> playlistEntries{};
    PlaylistCompact() {};
    PlaylistCompact(std::vector<EntryId> pEntries) : playlistEntries{pEntries} {};
};

enum class PlayerView : int {
    HOME,
    PLAY,
    NONE
};

struct PlayerConfig {
    bool visByDefault{};
    bool loopByDefault{};
    std::vector<fs::path> scanPaths{};
    std::uint8_t volByDefault{};
};

struct PlayerData {
    std::vector<Entry> fileEntries{};
    std::vector<PlaylistCompact> playlists{};
};

struct PlayerPaths {
    fs::path execPath{getExecDirectory()};
    fs::path configPath{execPath / "config.yaml"};
    fs::path dataPath{execPath / "data.json"};
    fs::path playlistsPath{execPath / "playlists.json"};
};

class Player {
    PlayerConfig config{};
    PlayerData data{};
    PlayerPaths paths{};
    Interface ui{*this};
    Audio aud{};
    friend class Interface;

  public:
    Player();
    void run();
    void quit();
};

} // namespace tml