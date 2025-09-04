#define NOMINMAX
#define MINIAUDIO_IMPLEMENTATION
#include "player.hpp"
#include "utils.hpp"
#include <ShlObj.h>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <miniaudio.h>
#include <mutex>
#include <nlohmann/json.hpp>
#include <string>
#include <unordered_set>
#include <yaml-cpp/yaml.h>

#pragma comment(lib, "Shell32.lib")

namespace tml {

PlayerInternalState::PlayerInternalState() { rBuffer.resize(rBufferLength); }

namespace {

std::filesystem::path getMusicFolderPath() {
    PWSTR path{};
    HRESULT r{SHGetKnownFolderPath(FOLDERID_Music, 0, nullptr, &path)};
    require(SUCCEEDED(r), Error::MUSIC_PATH_ERROR);
    std::filesystem::path mPath{path};
    CoTaskMemFree(path);
    return mPath;
}

} // namespace

/*
    Refactor for data integrity. Writes/Reads to the file.
    Consolidate multiple open/close streams.
*/
Player::Player() {
    std::filesystem::path configPath{getExecPath().parent_path() / "config.yaml"};
    std::filesystem::path entriesPath{getExecPath().parent_path() / "data.json"};

    // Config setup.
    if (!std::filesystem::exists(configPath)) {
        YAML::Node wConfig{};
        wConfig["paths"].push_back(std::string{reinterpret_cast<const char *>(getMusicFolderPath().u8string().data())});
        std::ofstream oStream{configPath};
        require(oStream.is_open(), Error::CONFIG_WRITE_ERROR);
        oStream << wConfig;
        oStream.close();
    }
    std::ifstream fStream{configPath};
    require(fStream.is_open(), Error::CONFIG_READ_ERROR);
    YAML::Node configFile{YAML::Load(fStream)};
    std::vector<std::filesystem::path> scanPaths{};
    for (std::string &pth : configFile["paths"].as<std::vector<std::string>>()) {
        scanPaths.push_back(std::filesystem::path{pth});
    }
    fStream.close();

    // Data setup.
    if (!std::filesystem::exists(entriesPath)) {
        nlohmann::json file{};
        std::ofstream oStream{entriesPath};
        require(oStream.is_open(), Error::DATA_WRITE_ERROR);
        oStream << file.dump() << std::endl;
        oStream.close();
    }
    std::ifstream jsonStream{entriesPath};
    require(jsonStream.is_open(), Error::DATA_READ_ERROR);
    nlohmann::json file{nlohmann::json::parse(jsonStream)};
    file.clear();
    for (const nlohmann::json &entry : file) {
        AudioEntry jEntry{entry.get<AudioEntry>()};
        entries[jEntry.uid] = jEntry;
    }
    std::uint32_t maxUid{[&] {
        std::uint32_t maxUid{};
        if (entries.empty()) {
            return maxUid;
        }
        return std::max_element(
                   entries.begin(), entries.end(),
                   [](const std::pair<const uint32_t, AudioEntry> &a, const std::pair<const uint32_t, AudioEntry> &b) {
                       return a.first < b.first;
                   }
        )->first;
    }()};
    std::unordered_set<std::filesystem::path> filepaths{[&] {
        std::unordered_set<std::filesystem::path> filepaths{};
        for (const std::pair<unsigned int, AudioEntry> &entry : entries) {
            filepaths.insert(entry.second.filePath);
        }
        return filepaths;
    }()};
    static const std::unordered_set<std::filesystem::path> supportedExt{".flac", ".wav", ".aiff", ".alac", ".ape",
                                                                        ".wma",  ".mp3", ".m4a",  ".aac",  ".ogg",
                                                                        ".opus", ".mpc", ".weba", ".webm"};
    for (const std::filesystem::path &path : scanPaths) {
        require(std::filesystem::exists(path), Error::CONFIG_ARGUMENT_PATH_ERROR);
        for (const std::filesystem::path &entryPath : std::filesystem::recursive_directory_iterator(path)) {
            if (!supportedExt.contains(entryPath.extension())) {
                continue;
            }
            if (!filepaths.contains(entryPath)) {
                entries[++maxUid] = AudioEntry{entryPath, maxUid};

                file.push_back(entries[maxUid]);
            }
        }
    }
    std::ofstream oStream{entriesPath};
    require(oStream.is_open(), Error::DATA_WRITE_ERROR);
    oStream << file.dump() << std::endl;
    oStream.close();
    jsonStream.close();
}

Player::~Player() {
    {
        std::lock_guard<std::mutex> lock{internalState.mutex};
        internalState.terminate = true;
    }
    if (thread.joinable()) {
        thread.join();
    }
}

void Player::run() {
    thread = std::thread([this] { this->pThread(); });
}

namespace {

void callback(ma_device *pDevice, void *pOut, const void *pIn, unsigned int fCount) {
    Player& player{*static_cast<Player*>(pDevice->pUserData)};
}

}

void Player::pThread() {
    ma_device_config config{};
    config.sampleRate = sampleRate;
    config.deviceType = ma_device_type_playback;
    config.dataCallback = callback;
    config.pUserData = this;
    ma_device device{};
    ma_device_init(nullptr, &config, &device);
    while (true) {        
        continue;
    }
};

const std::unordered_map<std::uint32_t, AudioEntry> &Player::getEntries() const { return entries; }

PlayerStateSnapshot Player::getState() const {
    PlayerStateSnapshot snapshot{};
    snapshot.timestamp = state.timestamp.load();
    snapshot.playing = state.playing.load();
    snapshot.muted = state.muted.load();
    snapshot.cVol = state.cVol.load();
    snapshot.cFileIdx = state.cFileIdx.load();
    return snapshot;
}

void Player::playFile(const std::size_t idx) {}
void Player::pauseFile() {}
void Player::muteFile() {}
void Player::seekFile(const std::chrono::milliseconds ms) {}
void Player::setVol(const float nVol) {}
void Player::incVol(const float iVal) {}
void Player::decVol(const float dVal) {}

} // namespace tml