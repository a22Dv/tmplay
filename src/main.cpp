#include <exception>
#include <iostream>
#include <chrono>
#include <thread>

#include "maudio.hpp"
#include "utils.hpp"

int main() {
    std::ios_base::sync_with_stdio(false);
    try {
        trm::AudioDevice aud{};
        aud.start("");
        aud.play();
        std::this_thread::sleep_for(std::chrono::duration<float>(15.0));
        std::cout << "Hello World!\n";
    } catch (const std::exception &e) {
        trm::showError(e.what());
    }
}