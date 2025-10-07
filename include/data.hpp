#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <unordered_map>
#include <vector>
#include <stack>

namespace trm {

struct Entry {
    std::uint64_t nameHash{};
    std::u8string name{};
    std::filesystem::path path{};
    std::uint32_t timesSkipped{};
    std::uint32_t timesPlayed{};
    float durationSeconds{};
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
    std::size_t findChild(const char32_t ch);
    std::size_t addChild(const char32_t ch, const std::size_t idx);
    void removeChild(const char32_t ch);
};

class PrefixTree {
    std::stack<std::size_t> freeStack{};
    std::vector<PrefixNode> data{PrefixNode{}};
    std::size_t insertNode(const std::size_t pIdx, const char32_t ch, const bool endOfWord = false);
    void deleteNode(const std::size_t idx);

  public:
    void insertWord(const std::u8string &str);
    void deleteWord(const std::u8string &str);
    std::vector<std::u8string> traverse(const std::u8string &str);
};

struct Library {
    std::uint64_t libraryHash{};
    std::vector<Entry> entries{};
    std::unordered_map<std::u8string, std::size_t> nameToEntry{};
    PrefixTree searchTree{};
};

std::vector<char32_t> convert(const std::u8string& str);

} // namespace trm