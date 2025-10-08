#pragma once

#ifdef _WIN32
#include <Windows.h>

#include <ShlObj.h>
#pragma comment(lib, "shell32.lib")
#else
#error "FILE NOT IMPLEMENTED"
#endif

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

#include <nlohmann/detail/macro_scope.hpp>
#include <nlohmann/json.hpp>

#include "utils.hpp"

namespace trm {

struct Entry {
    std::string name{};
    std::filesystem::path path{};
    std::uint32_t timesSkipped{};
    std::uint32_t timesPlayed{};
    float durationSeconds{};
    NLOHMANN_DEFINE_TYPE_INTRUSIVE(Entry, name, path, timesSkipped, timesPlayed, durationSeconds)
};

class PrefixNode {
    bool endOfWord{false};
    char32_t ch{U'\0'};
    std::vector<std::pair<char32_t, std::size_t>> children{};
    friend class PrefixTree;

  public:
    constexpr void setCh(const char32_t chr) { ch = chr; }
    constexpr void setEndOfWord(const bool st) { endOfWord = st; }
    constexpr bool isEndOfWord() const { return endOfWord; }
    constexpr char32_t getCh() const { return ch; }
    inline void reset() {
        endOfWord = false;
        ch = U'\0';
        children.clear();
    }

    // Returns the index of the child saved when found, else returns SIZE_MAX.
    std::size_t findChild(const char32_t ch);
    std::size_t addChild(const char32_t ch, const std::size_t idx);
    void removeChild(const char32_t ch);
};

class PrefixTree {
    std::vector<std::size_t> freeStack{};
    std::vector<PrefixNode> data{PrefixNode{}};

    // Adds the node onto the arena, and makes it a child of the passed parent.
    std::size_t insertNode(const std::size_t pIdx, const char32_t ch, const bool endOfWord = false);
    void deleteSubtree(const std::size_t idx);

  public:
    void insertWord(const std::u8string &str);
    void deleteWord(const std::u8string &str);
    std::vector<std::u8string> traverse(const std::u8string &str);
};

struct Configuration {
    std::vector<std::string> supportedExtensions{};
    std::vector<std::string> rootDirectories{};
    bool defaultAutoplay{};
    bool defaultLooped{};
    std::uint8_t defaultVolume{};
    Configuration() {};
    Configuration(const std::filesystem::path &path);
    static Configuration getDefaultConfig();
    NLOHMANN_DEFINE_TYPE_INTRUSIVE(
        Configuration, supportedExtensions, rootDirectories, defaultAutoplay, defaultLooped, defaultVolume
    );
};

// Read-only representation.
using DirectoryEntry = std::pair<std::filesystem::path, std::chrono::milliseconds>;
class Directories {
    std::unordered_map<std::filesystem::path, std::uint64_t> directoryList{};
    std::chrono::milliseconds queryTimestamp(const std::filesystem::path &path);
    std::vector<DirectoryEntry> getChildren(const std::filesystem::path &parent);
    bool matches_directory_timestamp(const std::filesystem::path &path);

  public:
    void insertEntry(const std::filesystem::path& directory);
    std::vector<std::filesystem::path> getModifiedPaths(const std::vector<std::filesystem::path> &rootDirectories);
    Directories() {}
    NLOHMANN_DEFINE_TYPE_INTRUSIVE(Directories, directoryList);
};

struct Library {
    Configuration config{};
    std::vector<Entry> entries{};
    std::unordered_map<std::u8string, std::size_t> nameToEntry{};
    PrefixTree searchTree{};
    Directories dirCache{};
    Library();
    NLOHMANN_DEFINE_TYPE_INTRUSIVE(Library, config, entries, dirCache);
};

// Does not check for invalid UTF-8 sequences. Input MUST be valid.
std::u32string convertToUTF32(const std::u8string &str);

// Does not check for invalid UTF-32 sequences. Input MUST be valid.
std::u8string convertToUTF8(const std::u32string &str);

// Wraps the contents of a u8string in a string container.
inline std::string asString(const std::u8string &str) {
    return {reinterpret_cast<const char *>(str.data()), str.length()};
}

// Converts to UTF-8 before outputting a string.
inline std::string asString(const std::u32string &str) { return asString(convertToUTF8(str)); }

inline std::filesystem::path getExecutablePath() {
#ifdef _WIN32
    std::wstring wstr{};
    wstr.resize(MAX_PATH);
    DWORD ret{GetModuleFileNameW(nullptr, wstr.data(), MAX_PATH)};
    require(ret < MAX_PATH && ret != 0, Error::EXEC_PATH_RETRIEVAL);
    return wstr;
#else
#error "FUNCTION NOT IMPLEMENTED."
#endif
}

inline std::filesystem::path getUserMusicDirectory() {
#ifdef _WIN32
    PWSTR pwstr{};
    HRESULT hr{SHGetKnownFolderPath(FOLDERID_Music, 0, nullptr, &pwstr)};
    require(SUCCEEDED(hr), Error::MUSIC_PATH_RETRIEVAL);
    std::filesystem::path dirPath{pwstr};
    CoTaskMemFree(pwstr);
    return dirPath;
#else
#error "FUNCTION NOT IMPLEMENTED."
#endif
}

} // namespace trm