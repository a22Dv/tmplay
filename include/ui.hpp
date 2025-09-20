#pragma once

#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/component/component.hpp>

#include "utils.hpp"

namespace tml {

struct InterfaceText {
    static constexpr const char *windowTitle{""};
    static constexpr const char *playlistHeader{"Playlists"};
    static constexpr const char *recAddedHeader{"Recently Added"};
    static constexpr const char *appIcon{"󰬁󰫺󰫽󰫹󰫮󰬆  "};
    static constexpr const char *play{"   "};
    static constexpr const char *pause{"   "};
    static constexpr const char *mute{"   "};
    static constexpr const char *volOff{"  "};
    static constexpr const char *volMed{"  "};
    static constexpr const char *volHigh{"   "};
    static constexpr const char *vis{"   "};
    static constexpr const char *loop{" 󰑖  "};
    static constexpr const char *noLoop{" 󰑗  "};
    static constexpr const char *playNext{"   "};
    static constexpr const char *playPrev{"   "};
    static constexpr const char *homeText{"Home  "};
    static constexpr const char *quitText{"Quit 󰈆 "};
    static constexpr const char *songIcon{"󰎇"};
    static constexpr const char *playlistIcon{"󰼄 "};
    static constexpr const char *selectedMarker{">"};
    static constexpr const char *noVis{"   "};
    static constexpr const char *seekNext{"   "};
    static constexpr const char *seekPrev{"   "};
};

class Player;

struct InterfaceState {
    int currentView{};
};

struct HomeState {
    int rAddedSelected{};
    int playlistSelected{};
    std::vector<std::string> dspPlaylists{};
    std::vector<std::string> dspRecentlyAdded{};
    std::vector<EntryId> recAddedMap{};
};

struct PlayState {
    ftxui::Component sidebarWrapper{ftxui::Container::Horizontal({})};
    ftxui::Component playbackBtn{ftxui::Container::Horizontal({})};
    std::vector<std::string> dspTracks{};
    std::vector<EntryId> trackMap{};
    int trackSelected{};
    bool playback{};
    bool loop{};
    bool vis{};
    bool muted{};
};

class Interface {
    Player &player;
    InterfaceState uiState{};
    HomeState hState{};
    PlayState pState{};
    ftxui::ScreenInteractive scr{ftxui::ScreenInteractive::Fullscreen()};
    ftxui::Component root();
    ftxui::Component home();
    ftxui::Component play();
    ftxui::Component header();
    void populateRecentlyPlayed();
    void populatePlaylists();
    void newPlayState(const std::vector<EntryId>& tracks, const int initialTrack = 0);
  public:
    void run();
    void quit();
    Interface(Player &pl) : player{pl} {};
};

} // namespace tml