#include <iostream>

#include "player.hpp"
#include "data.hpp"

namespace trm {

void Player::run() {
    PrefixTree tree{};
    tree.insertWord(u8"abc");
    tree.insertWord(u8"acb");
    tree.insertWord(u8"abcd");
    tree.insertWord(u8"abdc");
    for (const auto& u8Str : tree.traverse(u8"ab")) {
        std::cout << asString(u8Str) << '\n';
    }
}

} // namespace trm
