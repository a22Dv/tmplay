#include <algorithm>
#include <ftxui/dom/direction.hpp>
#include <ftxui/screen/box.hpp>
#include <iterator>
#include <vector>

#include <ftxui/component/component.hpp>
#include <ftxui/component/component_options.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/dom/elements.hpp>
#include <ftxui/dom/node.hpp>

#include <player.hpp>

namespace tml {

void InterfaceComponents::initHome() {
    UserInterface &ui{linkedInterface};

    // We need to sort the entries, and their index mapping accordingly
    // so the corresponding index plays the correct file.
    std::vector<std::pair<Entry, EntryId>> sortedEntries{};
    EntryId idx{};
    std::transform(
        ui.player.data.fileEntries.begin(), ui.player.data.fileEntries.end(), std::back_inserter(sortedEntries),
        [&idx](const auto &e) { return std::pair<Entry, EntryId>(e, idx++); }
    );
    std::sort(sortedEntries.begin(), sortedEntries.end(), [](const auto &a, const auto &b) {
        return a.first.timeModified > b.first.timeModified;
    });

    std::vector<EntryId> &recAddedMapping{ui.player.state.selectorRecentlyAddedMapping};
    std::transform(sortedEntries.begin(), sortedEntries.end(), std::back_inserter(recAddedMapping), [](const auto &e) {
        return e.second;
    });
    recAddedPlaylist = PlaylistCompact{};
    recAddedPlaylist.playlistName = "Recently Added";
    recAddedPlaylist.playlistEntries = recAddedMapping;

    // Set recently added.
    std::transform(
        sortedEntries.begin(), sortedEntries.end(), std::back_inserter(recAddedEntries), [&ui](const auto &e) {
            return std::format(
                "{} {}", ui.elements.songIcon,
                reinterpret_cast<const char *>(e.first.asPath().filename().replace_extension("").u8string().data())
            );
        }
    );

    // Set playlists.
    std::vector<PlaylistCompact> &playlistsC{ui.player.data.playlists};
    std::transform(playlistsC.begin(), playlistsC.end(), std::back_inserter(playlistsEntries), [&ui](const auto &e) {
        return std::format("{} {}", ui.elements.playlistIcon, e.playlistName);
    });

    // Menus.
    auto customTransform{[](const ftxui::EntryState &st) {
        ftxui::Element text{ftxui::text(std::format("{} {}", st.focused ? ">" : " ", st.label))};
        text |= ftxui::color(st.focused ? ftxui::Color::White : ftxui::Color::GrayDark);
        return text;
    }};
    ftxui::MenuOption entryOptRec{ftxui::MenuOption::Vertical()};
    ftxui::MenuOption entryOptList{ftxui::MenuOption::Horizontal()};
    entryOptList.entries_option.transform = customTransform;
    entryOptRec.entries_option.transform = customTransform;

    // Redirects.
    entryOptList.on_enter = [&ui] {
        ui.player.state.view = static_cast<int>(PlayerView::PLAY);
        ui.player.state.cPlaylist = ui.player.data.playlists[ui.player.state.homePlaylistSel];
        ui.player.state.playPlaylistSel = 0;
        ui.serial++;
    };
    entryOptRec.on_enter = [&ui, this] {
        ui.player.state.view = static_cast<int>(PlayerView::PLAY);
        ui.player.state.cPlaylist = recAddedPlaylist;
        ui.player.state.playPlaylistSel = ui.player.state.homeRecentlyAddedSel;
        ui.serial++;
    };
    playlistsList = ftxui::Menu(playlistsEntries, &ui.player.state.homePlaylistSel, entryOptList);
    recAddedList = ftxui::Menu(recAddedEntries, &ui.player.state.homeRecentlyAddedSel, entryOptRec);
    homeContainer =
        ftxui::Container::Vertical({ftxui::Container::Horizontal({hHomeBtn, qHomeBtn}), playlistsList, recAddedList});
}

void InterfaceComponents::initPlay() {
    UserInterface &ui{linkedInterface};
    ftxui::ButtonOption playbackBtnOpt{ftxui::ButtonOption::Ascii()};
    playbackBtnOpt.transform = [&](const ftxui::EntryState &state) {
        return !ui.player.state.isPlaying ? ui.elements.play : ui.elements.pause;
    };
    playbackBtnOpt.on_click = [&]() { ui.player.state.isPlaying = !ui.player.state.isPlaying; };
    playTrackBtn = ftxui::Button(playbackBtnOpt);
    playNextBtn = ftxui::Button(
        ui.elements.playNext,
        [&] {
            if (ui.player.state.playPlaylistSel < ui.player.state.cPlaylist.playlistEntries.size() - 1) {
                ui.player.state.playPlaylistSel++;
            }
        },
        ftxui::ButtonOption::Ascii()
    );
    playPrevBtn = ftxui::Button(
        ui.elements.playPrev,
        [&] {
            if (ui.player.state.playPlaylistSel > 0) {
                ui.player.state.playPlaylistSel--;
            }
        },
        ftxui::ButtonOption::Ascii()
    );

    ftxui::ButtonOption loopBtnOpt{ftxui::ButtonOption::Ascii()};
    loopBtnOpt.on_click = [&] {
        ui.player.state.isLooping = !ui.player.state.isLooping;
        ui.player.aud.toggleLooping();
    };
    loopBtnOpt.label = ui.elements.loop;
    loopBtnOpt.transform = [&](const ftxui::EntryState &e) {
        return ftxui::text(e.label) |
               ftxui::color((ui.player.state.isLooping ? ftxui::Color::White : ftxui::Color::GrayDark));
    };
    loopBtn = ftxui::Button(loopBtnOpt);
    ftxui::ButtonOption muteBtnOpt{ftxui::ButtonOption::Ascii()};
    muteBtnOpt.on_click = [&] {
        ui.player.state.isMuted = !ui.player.state.isMuted;
        ui.player.aud.toggleMute();
    };
    muteBtnOpt.transform = [&](const ftxui::EntryState &e) {
        if (ui.player.state.isMuted)
            return ui.elements.mute;
        if (ui.player.state.volume < 0.3f) {
            return ui.elements.volOff;
        } else if (ui.player.state.volume < 0.6f) {
            return ui.elements.volMed;
        } else {
            return ui.elements.volHigh;
        }
    };
    muteBtn = ftxui::Button(muteBtnOpt);
    ftxui::ButtonOption visBtnOpt{ftxui::ButtonOption::Ascii()};
    visBtnOpt.on_click = [&] { ui.player.state.visualization = !ui.player.state.visualization; };
    visBtnOpt.label = ui.elements.waveform;
    visBtnOpt.transform = [&](const ftxui::EntryState &e) {
        return ftxui::text(e.label) |
               ftxui::color((ui.player.state.visualization ? ftxui::Color::White : ftxui::Color::GrayDark));
    };
    visualBtn = ftxui::Button(visBtnOpt);

    // Volume slider.
    ftxui::SliderOption<float> sliderOpt{};
    sliderOpt.increment = 0.01;
    sliderOpt.max = 1.0;
    sliderOpt.min = 0.0;
    sliderOpt.value = ui.player.state.volume;
    sliderOpt.on_change = [&] { ui.player.aud.volSet(ui.player.state.volume); };
    sliderOpt.direction = ftxui::Direction::Up;
    sliderOpt.color_inactive = ftxui::Color::White;
    sliderOpt.color_active = ftxui::Color::White;
    volumeSlider = ftxui::Slider(sliderOpt);

    // Seek slider.
    ftxui::SliderOption<float> seekOpt{};
    seekOpt.increment = 0.01;
    seekOpt.max = 1.0;
    seekOpt.min = 0.0;
    seekOpt.value = ui.player.state.sliderSeekPos;
    seekOpt.on_change = [&] { ui.player.aud.seekTo(ui.player.state.sliderSeekPos * ui.player.aud.getDuration()); };
    seekOpt.direction = ftxui::Direction::Right;
    seekSlider = ftxui::Slider(seekOpt);

    // Sidebar.
    auto customTransform{[](const ftxui::EntryState &st) {
        ftxui::Element text{ftxui::text(std::format("{} {}", st.focused ? ">" : " ", st.label))};
        text |= ftxui::color(st.focused ? ftxui::Color::White : ftxui::Color::GrayDark);
        return text;
    }};
    ftxui::MenuOption sidebarOpt{ftxui::MenuOption::Vertical()};
    sidebarOpt.entries_option.transform = customTransform;
    sidebar = ftxui::Menu(&pListsSidebarEntries, &ui.player.state.playPlaylistSel, sidebarOpt);

    /// TODO: Audio view. Visualization.
    audioView = ftxui::Container::Vertical({});
    playContainer = ftxui::Container::Vertical(
        {ftxui::Container::Horizontal({hPlayBtn, qPlayBtn}), ftxui::Container::Horizontal({audioView, sidebar}),
         ftxui::Container::Horizontal(
             {visualBtn, playPrevBtn, playTrackBtn, playNextBtn, loopBtn, muteBtn, volumeSlider}
         ),
         seekSlider}
    );
}

void InterfaceComponents::init() {
    // Header-specific, across components.
    UserInterface &ui{linkedInterface};
    hHomeBtn = ftxui::Button(
        ui.elements.homeText, [&ui] { ui.player.state.view = static_cast<int>(PlayerView::HOME); },
        ftxui::ButtonOption::Ascii()
    );
    qHomeBtn = ftxui::Button(ui.elements.quitText, [&ui] { ui.player.quit(); }, ftxui::ButtonOption::Ascii());
    hPlayBtn = ftxui::Button(
        ui.elements.homeText, [&ui] { ui.player.state.view = static_cast<int>(PlayerView::HOME); },
        ftxui::ButtonOption::Ascii()
    );
    qPlayBtn = ftxui::Button(ui.elements.quitText, [&ui] { ui.player.quit(); }, ftxui::ButtonOption::Ascii());
    initHome();
    initPlay();
    rootContainer = ftxui::Container::Tab({playContainer, homeContainer}, &ui.player.state.view);
}

void UserInterface::run() {
    components.init();
    scr.Loop(getRoot());
}

void UserInterface::quit() { scr.Exit(); }

ftxui::Element UserInterface::playRoot() {
    static std::size_t cachedSerial{};
    if (cachedSerial != serial) {
        std::transform(
            player.state.cPlaylist.playlistEntries.begin(), player.state.cPlaylist.playlistEntries.end(),
            std::back_inserter(components.pListsSidebarEntries), [&](const auto &e) {
                return std::format(
                    "{} {}", elements.songIcon,
                    reinterpret_cast<const char *>(player.data.fileEntries[static_cast<std::size_t>(e)]
                                                       .asPath()
                                                       .filename()
                                                       .replace_extension("")
                                                       .u8string()
                                                       .data())
                );
            }
        );
        cachedSerial = serial;
    }
    // Header.
    auto header{[this] {
        return ftxui::hbox(
            elements.appIcon, ftxui::filler(), components.hPlayBtn->Render(), components.qPlayBtn->Render()
        );
    }};
    auto controls{[this] {
        return ftxui::vbox(
            components.seekSlider->Render(),
            ftxui::hbox(
                components.visualBtn->Render(), ftxui::separatorEmpty(), components.playPrevBtn->Render(),
                ftxui::separatorEmpty(), components.playTrackBtn->Render(), ftxui::separatorEmpty(),
                components.playNextBtn->Render(), ftxui::separatorEmpty(), components.loopBtn->Render(),
                ftxui::separatorEmpty(), components.muteBtn->Render(), ftxui::separatorEmpty(),
                components.volumeSlider->Render()
            )
        );
    }};
    auto audioView{[this] {
        int viewWidth{static_cast<int>(ftxui::Terminal::Size().dimx * 0.6)};
        return components.audioView->Render() | ftxui::size(ftxui::WIDTH, ftxui::EQUAL, viewWidth);
    }};
    auto sidebar{[this] {
        int sidebarWidth{static_cast<int>(ftxui::Terminal::Size().dimx * 0.4)};
        return components.sidebar->Render() | ftxui::yframe | ftxui::size(ftxui::WIDTH, ftxui::EQUAL, sidebarWidth);
    }};
    // Main content.
    auto content{[=] {
        int bodyHeight{static_cast<int>(ftxui::Terminal::Size().dimy * 0.7)};
        return ftxui::vbox(
            header(), ftxui::separatorEmpty(), ftxui::hbox(audioView(), sidebar()) | ftxui::size(ftxui::HEIGHT, ftxui::EQUAL, bodyHeight),
            controls() | ftxui::center
        );
    }};

    // Padded content.
    auto padded{[content] {
        return ftxui::hbox(ftxui::separatorEmpty(), content() | ftxui::flex, ftxui::separatorEmpty());
    }};
    // Main root.
    auto root{[=, this] {
        ftxui::Element window{ftxui::window(elements.windowTitle, padded(), ftxui::BorderStyle::EMPTY)};
        return window;
    }};
    return root();
}

ftxui::Element UserInterface::homeRoot() {
    // Header.
    auto header{[this] {
        return ftxui::hbox(
            elements.appIcon, ftxui::filler(), components.hHomeBtn->Render(), components.qHomeBtn->Render()
        );
    }};

    // Recently added.
    auto recentlyAdded{[this] {
        // Get entries.
        int height{static_cast<int>(ftxui::Terminal::Size().dimy * 0.5)};
        return ftxui::vbox(
                   elements.recAddedHeader, ftxui::separatorEmpty(), components.recAddedList->Render() | ftxui::yframe
               ) |
               ftxui::yflex;
    }};

    // Displayed playlists.
    auto playlists{[this] {
        return ftxui::vbox(
            elements.playlistHeader, ftxui::separatorEmpty(), components.playlistsList->Render() | ftxui::xframe
        );
    }};

    // Main content.
    auto content{[=] {
        return ftxui::vbox(header(), ftxui::separatorEmpty(), playlists(), ftxui::separatorEmpty(), recentlyAdded());
    }};

    // Padded content.
    auto padded{[content] {
        return ftxui::hbox(ftxui::separatorEmpty(), content() | ftxui::flex, ftxui::separatorEmpty());
    }};

    // Main root.
    auto root{[=, this] {
        ftxui::Element window{ftxui::window(elements.windowTitle, padded(), ftxui::BorderStyle::EMPTY)};
        return window;
    }};
    return root();
}

/// TODO: Temporary stub.
bool UserInterface::onHomeEvent(const ftxui::Event &event) { return false; }
bool UserInterface::onPlayEvent(const ftxui::Event &event) {
    if (event == ftxui::Event::Character(' ')) {
        player.state.isPlaying = !player.state.isPlaying;
        return true;
    }
    if (event == ftxui::Event::Return) {
        player.aud.playEntry(player.data.fileEntries[player.state.cPlaylist.playlistEntries[player.state.playPlaylistSel]]);
        return true;
    }
    return false;
}

/// TODO: Refactor to TAB.
ftxui::Component UserInterface::getRoot() {
    return ftxui::Renderer(
               components.rootContainer,
               [this] {
                   switch (PlayerView{player.state.view}) {
                   case PlayerView::HOME: return homeRoot();
                   case PlayerView::PLAY: return playRoot();
                   case PlayerView::NONE: throw std::runtime_error("UNHANDLED EXCEPTION: No view specified.");
                   }
               }
           ) |
           ftxui::CatchEvent([&](const auto &event) {
               switch (PlayerView{player.state.view}) {
               case PlayerView::HOME: return onHomeEvent(event);
               case PlayerView::PLAY: return onPlayEvent(event);
               case PlayerView::NONE: throw std::runtime_error("UNHANDLED EXCEPTION: No view specified.");
               }
           });
}

} // namespace tml