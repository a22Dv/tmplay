#include <algorithm>
#include <array>
#include <bitset>
#include <climits>
#include <cstddef>
#include <stack>
#include <utility>
#include <vector>

#include "data.hpp"

namespace trm {

std::size_t PrefixNode::findChild(const char32_t ch) {
    auto lowerIter{std::lower_bound(children.begin(), children.end(), ch, [](const auto e, const auto f) {
        return e.first < f;
    })};
    if (lowerIter == children.end() || lowerIter->first != ch) {
        return children.size();
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
        cNIdx = freeStack.top();
        freeStack.pop();
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
    freeStack.push(idx);
}

void PrefixTree::insertWord(const std::u8string &str) {
    std::u32string codepoints{convertToUTF32(str)};
    std::size_t cIdx{};
    for (auto iter{codepoints.begin()}; iter != codepoints.end(); ++iter) {
        const char32_t sChar{*iter};
        PrefixNode &cNode{data[cIdx]};
        std::size_t childIdx{};
        if ((childIdx = cNode.findChild(sChar)) == cNode.children.size()) {
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
        if ((childIdx = cNode.findChild(sChar)) == cNode.children.size()) {
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
        if (childIdx == data[startNodeIdx].children.size()) {
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

} // namespace trm