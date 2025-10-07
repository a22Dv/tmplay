#include <algorithm>
#include <array>
#include <bitset>
#include <climits>
#include <cstddef>
#include <stack>
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

void PrefixTree::deleteNode(const std::size_t idx) {
    PrefixNode &node{data[idx]};
    for (const auto pair : node.children) {
        deleteNode(pair.second);
    }
    node.reset();
    freeStack.push(idx);
}

void PrefixTree::insertWord(const std::u8string &str) {
    std::vector<char32_t> codepoints{convert(str)};
    std::size_t cIdx{};
    PrefixNode &cNode{data[cIdx]};
    for (auto iter{codepoints.begin()}; iter != codepoints.end(); ++iter) {
        const char32_t sChar{*iter};
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
    std::vector<char32_t> codepoints{convert(str)};
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
        deleteNode(path.top());
        path.pop();
        if (!path.empty()) {
            data[path.top()].removeChild(*codepointsIter);
            codepointsIter = codepointsIter != codepoints.begin() ? codepointsIter - 1 : codepointsIter;
        }
    }
}

std::vector<char32_t> convert(const std::u8string &str) {
    constexpr std::size_t markerTypeCount{5};
    constexpr std::size_t contBytePayloadBitSize{6};
    constexpr std::size_t utf8Bits{sizeof(char8_t) * CHAR_BIT};
    constexpr std::array<char8_t, markerTypeCount> mask{0b01111111, 0b00111111, 0b00011111, 0b00001111, 0b00000111};
    std::vector<char32_t> vec{};
    vec.reserve(static_cast<std::size_t>(str.length() * 0.9));
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
                vec.push_back(cPoint);
            }
            offset = marker == 1 ? 0 : marker - 2;
            cPoint = U'\0';
        }
        cPoint |= (ch & mask[marker - 1]) << (offset * contBytePayloadBitSize);
        --offset;
    }
    if (cPoint) {
        vec.push_back(cPoint);
    }
    return vec;
}

} // namespace trm