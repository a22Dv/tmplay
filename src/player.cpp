#define MINIAUDIO_IMPLEMENTATION
#include "player.hpp"
#include "utils.hpp"
#include <ShlObj.h>
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
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

Player::Player() {
    std::filesystem::path configPath{getExecPath().parent_path() / "config.yaml"};
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
    std::vector<std::filesystem::path> paths{};
    for (std::string &pth : configFile["paths"].as<std::vector<std::string>>()) {
        paths.push_back(std::filesystem::path{pth});
    }
    static const std::unordered_set<std::string> supportedExt{".flac", ".wav", ".aiff", ".alac", ".ape",
                                                              ".wma",  ".mp3", ".m4a",  ".aac",  ".ogg",
                                                              ".opus", ".mpc", ".weba", ".webm"};
    for (const std::filesystem::path &path : paths) {
        require(std::filesystem::exists(path), Error::CONFIG_ARGUMENT_PATH_ERROR);
        for (const std::filesystem::path &entry : std::filesystem::recursive_directory_iterator(path)) {
            std::string ext{entry.extension().string()};
            std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return std::tolower(c); });
            if (!supportedExt.contains(ext)) {
                continue;
            }
            audPaths.push_back(entry);
        }
    }
}

Player::~Player() {
     {
        internalState.mutex.lock();
        internalState.terminate = true;
     }
     if (thread.joinable()) {
        thread.join();
     }
}

void Player::run() {
    thread = std::thread([this] { this->pThread(); });
}


void Player::pThread() {

};

const std::vector<std::filesystem::path> &Player::getPaths() const { return audPaths; }

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