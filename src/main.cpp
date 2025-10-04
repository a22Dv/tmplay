#include <chrono>
#include <exception>
#include <iostream>
#include <thread>

#include "maudio.hpp"
#include "utils.hpp"

int main() {
    std::ios_base::sync_with_stdio(false);
    try {
        trm::AudioDevice aud{};
        aud.start(
            reinterpret_cast<const char8_t *>(
                "C:\\Users\\A22\\Music\\Bibi – Scott And Zelda (책방오빠 문학소녀) [RomIEng Lyric] [smRz2IW9qP0].m4a"
            )
        );
        aud.play();
        std::this_thread::sleep_for(std::chrono::duration<float>(5.0));
        aud.seekTo(60.0f);
        std::this_thread::sleep_for(std::chrono::duration<float>(30.0));
        std::cout << "Hello World!\n";
    } catch (const std::exception &e) {
        trm::showError(e.what());
    }
}