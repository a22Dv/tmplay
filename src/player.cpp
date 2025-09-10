#include <algorithm>
#include <cctype>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>
#include <ostream>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <yaml-cpp/yaml.h>

#include "player.hpp"
#include "utils.hpp"

#pragma comment(lib, "Shell32.lib")

namespace tml {

namespace JSON = nlohmann;

namespace detail {

/// @brief Scans the paths from a given list of paths to find audio files
/// and returns a vector containing the paths to those audio files.
std::vector<fs::path> scanPaths(const std::vector<fs::path> &paths) {
    // clang-format off
    static std::unordered_set<std::string> supportedExtensions{
        ".flac", ".wav", ".aiff", ".alac", ".ape",
        ".wma",  ".mp3", ".m4a",  ".aac",  ".ogg",
        ".opus", ".mpc", ".weba", ".webm"
    };
    // clang-format on
    std::vector<fs::path> scannedPaths{};
    for (const auto &path : paths) {
        for (const auto &entry : fs::recursive_directory_iterator(path)) {
            if (!entry.is_regular_file()) {
                continue;
            }
            const fs::path entryPath{entry.path()};
            std::string ext{entryPath.extension().string()};
            std::transform(ext.begin(), ext.end(), ext.begin(), [](const auto &e) { return std::tolower(e); });
            if (!supportedExtensions.contains(ext)) {
                continue;
            }
            scannedPaths.push_back(entryPath);
        }
    }
    return scannedPaths;
}

/// @brief Merges new entries and existing ones, removing those not found in new entries and adding those
/// not found in new entries but not in the existing entries.
std::vector<Entry> setEntries(const std::vector<fs::path> &newEntries, const std::vector<Entry> &existingEntries) {
    std::unordered_map<fs::path, Entry> existing{[&] {
        std::unordered_map<fs::path, Entry> existing{};
        for (const auto &entry : existingEntries) {
            existing[entry.u8filePath] = entry;
        }
        return existing;
    }()};
    std::vector<Entry> out{};
    for (const auto &path : newEntries) {
        if (existing.contains(path)) {
            out.push_back(existing[path]);
        } else {
            out.push_back(Entry{path});
        }
    }
    return out;
}

}; // namespace detail

/// @brief Constructor.
Player::Player() {

    // File default initialization.
    const bool dExists{fs::exists(dataPath)};
    const bool cExists{fs::exists(configPath)};
    if (!dExists) {
        std::ofstream outStream{dataPath, std::ios::out | std::ios::trunc};
        require(outStream.is_open(), Error::WRITE);
        JSON::json data{};
        outStream << data << std::endl;
    }
    if (!cExists) {
        std::ofstream outStream{configPath, std::ios::out | std::ios::trunc};
        require(outStream.is_open(), Error::WRITE);
        YAML::Node defaultConfig{};
        const std::u8string mpath{getUserMusicDirectory().u8string()};
        defaultConfig["scan-paths"] =
            std::vector<std::string>{std::string{reinterpret_cast<const char *>(mpath.data()), mpath.length()}};
        defaultConfig["default-volume"] = 100;
        defaultConfig["visualization"] = true;
        defaultConfig["loop-by-default"] = false;
        outStream << defaultConfig << std::endl;
    }

    std::ifstream inputStreamConfig{configPath};
    std::ifstream inputStreamData{dataPath};
    require(inputStreamConfig.is_open(), Error::READ);
    require(inputStreamData.is_open(), Error::READ);
    YAML::Node yamlConfig{YAML::Load(inputStreamConfig)};
    JSON::json data(JSON::json::parse(inputStreamData));

    config.defaultVolume = yamlConfig["default-volume"].as<std::uint8_t>();
    config.visualization = yamlConfig["visualization"].as<bool>();
    config.loopByDefault = yamlConfig["loop-by-default"].as<bool>();

    std::vector<std::string> rscanPaths{yamlConfig["scan-paths"].as<std::vector<std::string>>()};
    std::transform(rscanPaths.begin(), rscanPaths.end(), std::back_inserter(config.scanPaths), [](const auto &e) {
        return fs::path{std::u8string{reinterpret_cast<const char8_t *>(e.data()), e.length()}};
    });
    fileEntries = detail::setEntries(
        detail::scanPaths(config.scanPaths), data.is_null() ? std::vector<Entry>{} : data.get<std::vector<Entry>>()
    );
    std::ofstream outputStreamData{dataPath, std::ios::out | std::ios::trunc};
    require(outputStreamData.is_open(), Error::WRITE);
    outputStreamData << JSON::json(fileEntries).dump(4) << std::endl;
}

/// @brief Player entry point.
void Player::run() {
    SetConsoleOutputCP(CP_UTF8);
    aud.run();
    for (const auto &entry : fileEntries) {
        std::cout << entry.u8filePath << std::endl;
        aud.playEntry(entry, config.defaultVolume / 100.0f);
        aud.seekTo(50.0f);
        std::this_thread::sleep_for(std::chrono::seconds(5));
    }
}

} // namespace tml