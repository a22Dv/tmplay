#include <algorithm>
#include <cctype>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <ostream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>


#include <nlohmann/json.hpp>
#include <yaml-cpp/yaml.h>

#include "player.hpp"
#include "utils.hpp"

#pragma comment(lib, "Shell32.lib")

/**
    TODO:
    - Allow Audio class to show the last few samples as an FFT graph.
    - Create UI canvas, function to draw the visualization.
        - Create a custom ftxui::Node class for the visualizer.
        - Implement rescaling logic, aspect ratio keeping.
        - Implement actual FFT->Frequency graph->Image logic.
    - Global basic title search functionality, songs and playlists.
    - State management when switching between playlists. 
      Switching from playlist->home->same playlist must preserve state.
    ROADMAP:
    - YT-DLP integration
    - Better playlists.
    -----------------------------
    AI integration
    Auto-generated playlists based on:
        Time of day
        Past patterns
        Audio fingerprints
*/

namespace tml {

namespace JSON = nlohmann;

namespace detail {

// Scans the paths from a given list of paths to find audio files
// and returns a vector containing the paths to those audio files.
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

// Merges new entries and existing ones, removing those not found in new entries and adding those
// not found in new entries but not in the existing entries.
std::vector<Entry> setEntries(const std::vector<fs::path> &newEntries, const std::vector<Entry> &existingEntries) {
    std::unordered_map<fs::path, Entry> existing{[&] {
        std::unordered_map<fs::path, Entry> existing{};
        for (const auto &entry : existingEntries) {
            existing[entry.asPath()] = entry;
        }
        return existing;
    }()};
    std::vector<Entry> out{};
    for (const auto &path : newEntries) {
        if (existing.contains(path)) {
            out.push_back(existing[path]);
        } else {
            std::chrono::seconds time{
                std::chrono::duration_cast<std::chrono::seconds>(fs::last_write_time(path).time_since_epoch())
            };
            Entry e{path};
            e.timeModified = time.count();
            out.push_back(e);
        }
    }
    return out;
}

// Maps the repeated path data to compact IDs.
std::vector<PlaylistCompact> getCompacted(std::vector<Playlist> &pLists, std::vector<Entry> &fileEntries) {
    std::vector<PlaylistCompact> compacts{};
    std::unordered_map<std::string, std::uint64_t> map{};
    EntryId idx{};
    for (const auto &entry : fileEntries) {
        map[entry.u8filePath] = idx++;
    }
    for (const auto &pList : pLists) {
        std::vector<EntryId> entryIndices{};
        for (const auto &path : pList.playlistEntries) {
            entryIndices.push_back(map[path]);
        }
        PlaylistCompact pCompact{PlaylistCompact{entryIndices}};
        pCompact.playlistName = pList.playlistName;
        compacts.push_back(pCompact);
    }
    return compacts;
}

}; // namespace detail

// Constructor.
Player::Player() {

    // File default initialization.
    if (!fs::exists(paths.dataPath)) {
        std::ofstream outStream{paths.dataPath, std::ios::out | std::ios::trunc};
        require(outStream.is_open(), Error::WRITE);
        JSON::json data{};
        outStream << data << std::endl;
    }
    if (!fs::exists(paths.configPath)) {
        std::ofstream outStream{paths.configPath, std::ios::out | std::ios::trunc};
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

    std::ifstream inputStreamConfig{paths.configPath};
    std::ifstream inputStreamData{paths.dataPath};
    require(inputStreamConfig.is_open(), Error::READ);
    require(inputStreamData.is_open(), Error::READ);
    YAML::Node yamlConfig{YAML::Load(inputStreamConfig)};
    JSON::json jsonData(JSON::json::parse(inputStreamData));

    config.volByDefault = yamlConfig["default-volume"].as<std::uint8_t>();
    config.visByDefault = yamlConfig["visualization"].as<bool>();
    config.loopByDefault = yamlConfig["loop-by-default"].as<bool>();

    std::vector<std::string> rscanPaths{yamlConfig["scan-paths"].as<std::vector<std::string>>()};
    std::transform(rscanPaths.begin(), rscanPaths.end(), std::back_inserter(config.scanPaths), [](const auto &e) {
        return fs::path{std::u8string{reinterpret_cast<const char8_t *>(e.data()), e.length()}};
    });
    data.fileEntries = detail::setEntries(
        detail::scanPaths(config.scanPaths),
        jsonData.is_null() ? std::vector<Entry>{} : jsonData.get<std::vector<Entry>>()
    );
    std::ofstream outputStreamData{paths.dataPath, std::ios::out | std::ios::trunc};
    require(outputStreamData.is_open(), Error::WRITE);
    outputStreamData << JSON::json(data.fileEntries).dump(4) << std::endl;

    // Playlist default initialization.
    if (!fs::exists(paths.playlistsPath)) {
        std::ofstream out{paths.playlistsPath};
        Playlist pl{std::string{"All"}, {}};
        std::transform(
            data.fileEntries.begin(), data.fileEntries.end(), std::back_inserter(pl.playlistEntries),
            [](const auto &e) { return e.u8filePath; }
        );
        out << JSON::json{pl}.dump(4) << std::endl;
    }

    // Load the playlists, map to indices in fileEntries.
    std::ifstream inPLists{paths.playlistsPath};
    require(inPLists.is_open(), Error::READ);
    std::vector<Playlist> pLists{JSON::json::parse(inPLists).get<std::vector<Playlist>>()};
    data.playlists = detail::getCompacted(pLists, data.fileEntries);
}

// Player entry point.
void Player::run() {
    aud.run();
    ui.run();
}

void Player::quit() { ui.quit(); }

} // namespace tml