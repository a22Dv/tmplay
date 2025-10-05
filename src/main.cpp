#ifdef _WIN32
#include <Windows.h>
#endif

#include <chrono>
#include <exception>
#include <iostream>
#include <thread>

#include "maudio.hpp"
#include "utils.hpp"

int main() {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
#endif
    std::ios_base::sync_with_stdio(false);
    try {
        trm::AudioDevice aud{};
        aud.start(
            u8"C:\\Users\\A22\\Music\\Dreycruz ft. Bert Symoun - Wag Ipagsabi (Official Music Video) [hA-fTsi0eyg].m4a"
        );
        aud.play();
        aud.setVol(1.0f);
        aud.seekTo(100.0f);
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        while (!aud.isEof()) {
            std::cout << std::format("{:.2f} s | {}", aud.getTimestamp(), aud.isEof() ? "EOF " : "NEOF") << '\r';
            std::this_thread::sleep_for(std::chrono::milliseconds(150));
        }
        aud.start(u8"C:\\Users\\A22\\Music\\AOA - 짧은 치마 (Miniskirt) M⧸V [q6f-LLM1H6U].m4a");
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        while (!aud.isEof()) {
            std::cout << std::format("{:.2f} s | {}", aud.getTimestamp(), aud.isEof() ? "EOF " : "NEOF") << '\r';
            std::this_thread::sleep_for(std::chrono::milliseconds(150));
        }
        std::cout << "Hello World!\n";
    } catch (const std::exception &e) {
        trm::showError(e.what());
    }
}