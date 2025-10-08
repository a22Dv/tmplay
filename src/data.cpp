#include <algorithm>
#include <array>
#include <bitset>
#include <chrono>
#include <climits>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <stack>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "data.hpp"

namespace trm {

std::size_t PrefixNode::findChild(const char32_t ch) {
    auto lowerIter{std::lower_bound(children.begin(), children.end(), ch, [](const auto e, const auto f) {
        return e.first < f;
    })};
    if (lowerIter == children.end() || lowerIter->first != ch) {
        return SIZE_MAX;
    }
    return lowerIter->second;
}

std::size_t PrefixNode::addChild(const char32_t ch, const std::size_t idx) {
    if (children.empty() || ch > children.back().first) [[likely]] {
        children.emplace_back(ch, idx);
        return children.size() - 1;
    }
    auto insertIter{std::lower_bound(children.begin(), children.end(), ch, [](const auto a, const auto b) {
        return a.first < b;
    })};
    if (insertIter != children.end() && insertIter->first == ch) [[unlikely]] {
        return static_cast<std::size_t>(insertIter - children.begin());
    }
    return static_cast<std::size_t>(children.emplace(insertIter, ch, idx) - children.begin());
}

void PrefixNode::removeChild(const char32_t ch) {
    auto removeIter{std::lower_bound(children.begin(), children.end(), ch, [](const auto a, const auto b) {
        return a.first < b;
    })};
    if (removeIter != children.end() && removeIter->first == ch) {
        children.erase(removeIter);
    }
}

std::size_t PrefixTree::insertNode(const std::size_t pIdx, const char32_t ch, const bool endOfWord) {
    std::size_t cNIdx{};
    if (freeStack.empty()) {
        data.emplace_back();
        cNIdx = data.size() - 1;
    } else {
        cNIdx = freeStack.back();
        freeStack.pop_back();
    }
    PrefixNode &pNode{data[pIdx]};
    PrefixNode &cNode{data[cNIdx]};
    cNode.setCh(ch);
    cNode.setEndOfWord(endOfWord);
    pNode.addChild(ch, cNIdx);
    return cNIdx;
}

void PrefixTree::deleteSubtree(const std::size_t idx) {
    PrefixNode &node{data[idx]};
    for (const auto pair : node.children) {
        deleteSubtree(pair.second);
    }
    node.reset();
    freeStack.push_back(idx);
}

void PrefixTree::insertWord(const std::u8string &str) {
    std::u32string codepoints{convertToUTF32(str)};
    std::size_t cIdx{};
    for (auto iter{codepoints.begin()}; iter != codepoints.end(); ++iter) {
        const char32_t sChar{*iter};
        PrefixNode &cNode{data[cIdx]};
        std::size_t childIdx{};
        if ((childIdx = cNode.findChild(sChar)) == SIZE_MAX) {
            childIdx = insertNode(cIdx, sChar);
        }
        if (std::next(iter) == codepoints.end()) {
            data[childIdx].setEndOfWord(true);
            return;
        }
        cIdx = childIdx;
    }
}

void PrefixTree::deleteWord(const std::u8string &str) {
    std::u32string codepoints{convertToUTF32(str)};
    std::size_t cIdx{};
    PrefixNode &cNode{data[cIdx]};
    std::stack<std::size_t> path{};
    for (auto iter{codepoints.begin()}; iter != codepoints.end(); ++iter) {
        const char32_t sChar{*iter};
        std::size_t childIdx{};
        if ((childIdx = cNode.findChild(sChar)) == SIZE_MAX) {
            return;
        }
        path.push(childIdx);
    }
    data[path.top()].setEndOfWord(false);
    auto codepointsIter{codepoints.end() - 1};
    while (!path.empty() && data[path.top()].children.empty() && !data[path.top()].isEndOfWord()) {
        deleteSubtree(path.top());
        path.pop();
        if (!path.empty()) {
            data[path.top()].removeChild(*codepointsIter);
            if (codepointsIter != codepoints.begin()) {
                --codepointsIter;
            }
        }
    }
}

std::vector<std::u8string> PrefixTree::traverse(const std::u8string &str) {
    std::vector<std::u8string> results{};
    std::u32string codepoints{convertToUTF32(str)};
    std::size_t startNodeIdx{};
    for (const auto ch : codepoints) {
        const std::size_t childIdx{data[startNodeIdx].findChild(ch)};
        if (childIdx == SIZE_MAX) {
            return {};
        }
        startNodeIdx = childIdx;
    }
    using StackFrame = std::pair<std::size_t, std::size_t>;
    std::u32string chars{};
    std::stack<StackFrame> callstack{};
    callstack.emplace(startNodeIdx, 0);
    while (!callstack.empty()) {
        StackFrame &cFrame{callstack.top()};
        auto &children{data[cFrame.first].children};
        if (cFrame.second >= children.size()) {
            if (data[cFrame.first].isEndOfWord()) {
                results.push_back(convertToUTF8(codepoints + chars));
            }
            callstack.pop();
            if (!chars.empty()) [[likely]] {
                chars.pop_back();
            }
            continue;
        }
        ++cFrame.second;
        callstack.emplace(children[cFrame.second - 1].second, 0);
        chars.push_back(data[callstack.top().first].getCh());
    }
    return results;
}

std::u32string convertToUTF32(const std::u8string &str) {
    constexpr std::size_t markerTypeCount{5};
    constexpr std::size_t contBytePayloadBitSize{6};
    constexpr std::size_t utf8Bits{sizeof(char8_t) * CHAR_BIT};
    constexpr std::array<char8_t, markerTypeCount> mask{0b01111111, 0b00111111, 0b00011111, 0b00001111, 0b00000111};
    std::u32string u32str{};
    u32str.reserve(static_cast<std::size_t>(str.length() * 0.9));
    std::size_t offset{};
    char32_t cPoint{};
    for (const auto ch : str) {
        const std::bitset<utf8Bits> chBits{ch};
        std::size_t marker{1};
        while (chBits[utf8Bits - marker]) {
            ++marker;
        }
        if (marker != 2) {
            if (cPoint != U'\0') [[likely]] {
                u32str.push_back(cPoint);
            }
            offset = marker == 1 ? 0 : marker - 2;
            cPoint = U'\0';
        }
        cPoint |= (ch & mask[marker - 1]) << (offset * contBytePayloadBitSize);
        --offset;
    }
    if (cPoint) {
        u32str.push_back(cPoint);
    }
    return u32str;
}

std::u8string convertToUTF8(const std::u32string &str) {
    constexpr std::size_t typesCount{4};
    constexpr std::size_t maxBytes{4};
    constexpr std::array<char8_t, typesCount> startBits{0b0, 0b11000000, 0b11100000, 0b11110000};
    constexpr std::array<std::size_t, typesCount> thresholds{0x7F, 0x7FF, 0xFFFF, 0x10FFFF};
    std::u8string u8str{};
    u8str.reserve(str.length());
    std::u8string tmp{};
    tmp.reserve(maxBytes);
    for (auto ch : str) {
        if (ch <= 0x7F) [[likely]] {
            u8str.push_back(static_cast<char8_t>(ch));
            continue;
        }
        const std::size_t contBytes{[&] {
            std::size_t type{};
            for (const auto th : thresholds) {
                if (ch <= th) {
                    break;
                }
                ++type;
            }
            return type;
        }()};
        for (std::size_t i{}; i < contBytes; ++i) {
            tmp.push_back((0b10 << 6) | (ch & 0b111111));
            ch >>= 6;
        }
        tmp.push_back(ch | startBits[contBytes]);
        std::reverse(tmp.begin(), tmp.end());
        u8str.append(tmp);
        tmp.clear();
    }
    return u8str;
}

Configuration Configuration::getDefaultConfig() {
    Configuration config{};
    config.defaultAutoplay = true;
    config.defaultLooped = false;
    config.defaultVolume = 100;
    config.rootDirectories = {asString(getUserMusicDirectory().u8string())};
    config.supportedExtensions = {".mp3", ".m4a", ".flac", ".opus", ".webm", ".ogg", ".wav"};
    return config;
}

Library::Library() {
    const std::filesystem::path execPath{getExecutablePath().parent_path()};
    const std::filesystem::path configPath{execPath / "tmplay_config.json"};
    const std::filesystem::path dataPath{execPath / "tmplay_data.json"};
    if (!std::filesystem::exists(configPath)) {
        Configuration config{Configuration::getDefaultConfig()};
        nlohmann::json node(config);
        std::ofstream outStream{configPath};
        require(outStream.is_open(), Error::WRITE);
        outStream << node.dump(4);
    }
    if (!std::filesystem::exists(dataPath)) {
        std::ofstream outStream{dataPath};
        require(outStream.is_open(), Error::WRITE);
        outStream << nlohmann::json{}.dump(4);
    }
    std::ifstream configStream{configPath};
    std::ifstream dataStream{dataPath};
    require(configStream.is_open(), Error::READ);
    require(dataStream.is_open(), Error::READ);
    config = nlohmann::json::parse(configStream);
    nlohmann::json json(nlohmann::json::parse(dataStream));
    std::vector<Entry> existingEntries{};
    if (!json.is_null() && json.is_object()) {
        existingEntries = json["entries"];
        dirCache = json["dirCache"];
    }
    std::unordered_map<std::u8string, std::size_t> existingNamesToEntries{};
    for (std::size_t i{}; const auto &entry : existingEntries) {
        existingNamesToEntries[asU8(entry.name)] = i++;
    }
    std::unordered_set<std::string> supportedExtensions{};
    for (const auto &ext : config.supportedExtensions) {
        supportedExtensions.insert(ext);
    }
    std::vector<std::filesystem::path> rootDirs{};
    for (const auto &rootDir : config.rootDirectories) {
        rootDirs.emplace_back(asU8(rootDir));
    }
    for (const auto &rootDir : dirCache.getModifiedPaths(rootDirs)) {
        dirCache.insertEntry(rootDir);
        for (const auto &entry : std::filesystem::recursive_directory_iterator(rootDir)) {
            if (entry.is_directory()) {
                dirCache.insertEntry(entry.path());
                continue;
            }
            if (!entry.is_regular_file()) {
                continue;
            }
            const std::filesystem::path path{entry.path()};
            if (supportedExtensions.find(path.extension().string()) == supportedExtensions.end()) {
                continue;
            }
            const std::u8string name{path.filename().replace_extension("").u8string()};
            if (auto it{existingNamesToEntries.find(name)}; it == existingNamesToEntries.end()) {
                entries.emplace_back(asChar(name), path, 0, 0, getFileDuration(path));
            } else {
                entries.emplace_back(existingEntries[it->second]);
            }
            nameToEntry[name] = entries.size() - 1;
            searchTree.insertWord(name);
        }
    }
    std::ofstream outDataStream{dataPath};
    outDataStream << nlohmann::json(*this).dump(4);
}

std::chrono::milliseconds Directories::queryTimestamp(const std::filesystem::path &path) {
    if (auto it = directoryList.find(path); it != directoryList.end()) {
        return std::chrono::milliseconds{it->second};
    }
    return std::chrono::milliseconds{0};
}

std::vector<DirectoryEntry> Directories::getChildren(const std::filesystem::path &parent) {
    std::vector<DirectoryEntry> children{};
    for (const auto &pair : directoryList) {
        if (!pair.first.has_parent_path()) {
            continue;
        }
        if (pair.first.parent_path() == parent) {
            children.push_back(DirectoryEntry{pair.first, std::chrono::milliseconds{pair.second}});
        }
    }
    return children;
}

bool Directories::matches_directory_timestamp(const std::filesystem::path &path) {
    if (auto it = directoryList.find(path); it != directoryList.end()) {
        return std::chrono::milliseconds{it->second} == std::filesystem::last_write_time(path).time_since_epoch();
    }
    return false;
}

std::vector<std::filesystem::path>
Directories::getModifiedPaths(const std::vector<std::filesystem::path> &rootDirectories) {
    std::vector<std::filesystem::path> modified{};
    for (const auto &pair : directoryList) {
        if (!matches_directory_timestamp(pair.first)) {
            modified.emplace_back(pair.first);
        }
    }
    for (const auto &root : rootDirectories) {
        if (directoryList.find(root) == directoryList.end()) {
            modified.emplace_back(root);
        }
    }
    return modified;
}

void Directories::insertEntry(const std::filesystem::path &directory) {
    if (directoryList.find(directory) == directoryList.end()) {
        std::chrono::milliseconds mils{std::chrono::duration_cast<std::chrono::milliseconds>(
            std::filesystem::last_write_time(directory).time_since_epoch()
        )};
        directoryList[directory] = mils.count();
    }
}

} // namespace trm