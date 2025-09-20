#include <algorithm>
#include <chrono>
#include <functional>
#include <iterator>
#include <random>

#include <ftxui/component/component.hpp>
#include <ftxui/component/loop.hpp>
#include <ftxui/dom/elements.hpp>

#include "player.hpp"
#include "ui.hpp"

namespace tml {

namespace detail {

ftxui::Component toggleButton(
    const char *active, const char *inactive, bool &state, std::function<void()> onClick,
    ftxui::Color colorActive = ftxui::Color::White, ftxui::Color colorInactive = ftxui::Color::GrayDark
) {
    ftxui::ButtonOption opt{ftxui::ButtonOption::Ascii()};
    opt.on_click = onClick;
    opt.transform = [colorActive, colorInactive, active, inactive, &state](const auto &est) {
        ftxui::Element text{ftxui::text(state ? active : inactive)};
        text |= ftxui::color(est.focused ? colorActive : colorInactive);
        return text;
    };
    return ftxui::Button(opt);
}

ftxui::Component button(
    const char *dsp, std::function<void()> onClick, ftxui::Color colorFocused = ftxui::Color::GrayLight,
    ftxui::Color colorUnfocused = ftxui::Color::GrayDark
) {
    ftxui::ButtonOption opt{ftxui::ButtonOption::Ascii()};
    opt.label = dsp;
    opt.on_click = onClick;
    opt.transform = [colorFocused, colorUnfocused](const auto &est) {
        ftxui::Element text{ftxui::text(est.label)};
        text |= ftxui::color(est.focused ? colorFocused : colorUnfocused);
        return text;
    };
    return ftxui::Button(opt);
}

ftxui::Component menu(
    const char *entryIcon, const char *marker, int &selected, std::vector<std::string> &dspList,
    std::function<void()> onEnter, bool vertical = true, ftxui::Color colorFocused = ftxui::Color::GrayLight,
    ftxui::Color colorUnfocused = ftxui::Color::GrayDark,
    std::function<ftxui::Element(const ftxui::EntryState &est)> customTransform = nullptr
) {
    ftxui::MenuOption opt{vertical ? ftxui::MenuOption::Vertical() : ftxui::MenuOption::Horizontal()};
    opt.on_enter = onEnter;

    if (customTransform) {
        opt.entries_option.transform = customTransform;
    } else {
        opt.entries_option.transform = [colorFocused, colorUnfocused, entryIcon, marker](const auto &est) {
            ftxui::Element text{ftxui::text(
                std::format(
                    "{} {} {:.50}{}", est.focused ? marker : " ", entryIcon, est.label,
                    est.label.length() > 50 ? "..." : ""
                )
            )};
            text |= ftxui::color(est.focused ? colorFocused : colorUnfocused);
            text |= ftxui::bgcolor(ftxui::Color::RGBA(0, 0, 0, 0));
            return text;
        };
    }
    opt.entries = &dspList;
    opt.selected = &selected;
    return ftxui::Menu(opt);
}

std::string formatTimestamp(const float seconds) {
    int sec{static_cast<int>(seconds)};
    int hours{sec / (60 * 60)};
    int minutes{(sec % (60 * 60)) / 60};
    int secs{sec % 60};
    return std::format("{:02}:{:02}:{:02}", hours, minutes, secs);
}

} // namespace detail

ftxui::Component Interface::header() {
    ftxui::Component home{detail::button(InterfaceText::homeText, [this] {
        uiState.currentView = static_cast<int>(PlayerView::HOME);
    })};
    ftxui::Component quit{detail::button(InterfaceText::quitText, [this] { this->player.quit(); })};
    ftxui::Component header{ftxui::Container::Horizontal({home, quit})};
    return ftxui::Renderer(header, [this, home, quit] {
        auto main{ftxui::vbox(
            ftxui::hbox(
                ftxui::text(InterfaceText::appIcon), ftxui::filler(), home->Render(), ftxui::separatorEmpty(),
                quit->Render()
            ),
            ftxui::separatorEmpty()
        )};
        return main | ftxui::xflex;
    });
}

ftxui::Component Interface::home() {
    ftxui::Component playlists{detail::menu(
        InterfaceText::playlistIcon, InterfaceText::selectedMarker, hState.playlistSelected, hState.dspPlaylists,
        [this] {
            scr.Post([this] {
                uiState.currentView = static_cast<int>(PlayerView::PLAY);
                newPlayState(player.data.playlists[hState.playlistSelected].playlistEntries);
                pState.playbackBtn->TakeFocus();
                pState.playback = true;
                player.aud.playEntry(player.data.fileEntries[pState.trackMap[pState.trackSelected]]);
            });
        },
        false
    )};
    ftxui::Component rAdded{detail::menu(
        InterfaceText::songIcon, InterfaceText::selectedMarker, hState.rAddedSelected, hState.dspRecentlyAdded, [this] {
            scr.Post([this] {
                uiState.currentView = static_cast<int>(PlayerView::PLAY);
                newPlayState(hState.recAddedMap, hState.rAddedSelected);
                pState.playbackBtn->TakeFocus();
                pState.playback = true;
                player.aud.playEntry(player.data.fileEntries[pState.trackMap[pState.trackSelected]]);
            });
        }
    )};
    ftxui::Component main{ftxui::Container::Vertical({playlists, rAdded})};
    return ftxui::Renderer(main, [playlists, rAdded] {
        ftxui::Element container{ftxui::vbox(
            ftxui::text(InterfaceText::playlistHeader), ftxui::separatorEmpty(), playlists->Render() | ftxui::xframe,
            ftxui::separatorEmpty(), ftxui::text(InterfaceText::recAddedHeader), ftxui::separatorEmpty(),
            rAdded->Render() | ftxui::yframe
        )};
        return container | ftxui::flex;
    });
};

ftxui::Component Interface::play() {
    ftxui::Component nextBtn{detail::button(InterfaceText::playNext, [this] {
        int nVal{pState.trackSelected + 1};
        pState.trackSelected = std::clamp(nVal, 0, static_cast<int>(pState.dspTracks.size()));
        pState.playback = true;
        player.aud.playEntry(player.data.fileEntries[pState.trackMap[pState.trackSelected]]);
    })};
    ftxui::Component prevBtn{detail::button(InterfaceText::playPrev, [this] {
        int nVal{pState.trackSelected - 1};
        pState.trackSelected = std::clamp(nVal, 0, static_cast<int>(pState.dspTracks.size()));
        pState.playback = true;
        player.aud.playEntry(player.data.fileEntries[pState.trackMap[pState.trackSelected]]);
    })};
    ftxui::Component seekNextBtn{detail::button(InterfaceText::seekNext, [this] { player.aud.seekForward(5); })};
    ftxui::Component seekPrevBtn{detail::button(InterfaceText::seekPrev, [this] { player.aud.seekBackward(5); })};
    ftxui::Component loopBtn{detail::toggleButton(InterfaceText::loop, InterfaceText::noLoop, pState.loop, [this] {
        player.aud.toggleLooping();
        pState.loop = !pState.loop;
    })};
    ftxui::Component visBtn{detail::toggleButton(InterfaceText::vis, InterfaceText::noVis, pState.vis, [&] {
        pState.vis = !pState.vis;
    })};
    ftxui::Component autorunBtn{detail::toggleButton(
        InterfaceText::autorun, InterfaceText::noAutorun, pState.autorun, [this] { pState.autorun = !pState.autorun; }
    )};
    ftxui::Component renderFastBtn{
        detail::toggleButton(InterfaceText::renderFast, InterfaceText::renderSlow, uiState.renderFast, [this] {
            uiState.renderFast = !uiState.renderFast;
        })
    };
    ftxui::Component shuffleBtn{detail::button(InterfaceText::shuffle, [this] {
        std::random_device rd{};
        std::shuffle(pState.trackMap.begin(), pState.trackMap.end(), std::minstd_rand{rd()});
        std::transform(pState.trackMap.begin(), pState.trackMap.end(), pState.dspTracks.begin(), [this](const auto &t) {
            return std::string{reinterpret_cast<const char *>(
                player.data.fileEntries[t].asPath().filename().replace_extension("").u8string().data()
            )};
        });
        pState.trackSelected = 0;
        generateSidebar();
        if (pState.playback) {
            player.aud.playEntry(player.data.fileEntries[pState.trackMap[pState.trackSelected]]);
        }
    })};
    // Mute button has custom logic as it also reacts to the volume level of the application.
    ftxui::ButtonOption muteOpt{ftxui::ButtonOption::Ascii()};
    muteOpt.on_click = [this] {
        player.aud.toggleMute();
        pState.muted = !pState.muted;
    };
    muteOpt.transform = [this](const auto &est) {
        ftxui::Element label{ftxui::text([this] {
            float vol{player.aud.getState().volume.load()};
            if (pState.muted || vol == 0.0f)
                return InterfaceText::mute;
            else if (vol < 0.3)
                return InterfaceText::volOff;
            else if (vol < 0.6)
                return InterfaceText::volMed;
            else
                return InterfaceText::volHigh;
        }())};
        label |= ftxui::color(est.focused ? ftxui::Color::White : ftxui::Color::GrayDark);
        return label;
    };
    ftxui::Component muteBtn{ftxui::Button(muteOpt)};
    muteBtn |= ftxui::CatchEvent([this](const auto &e) {
        if (e == ftxui::Event::ArrowLeft) {
            player.aud.volDown(0.01f);
            return true;
        } else if (e == ftxui::Event::ArrowRight) {
            player.aud.volUp(0.01f);
            return true;
        }
        return false;
    });

    // Placeholder as the sidebar is generated programmatically.
    ftxui::Component sidebar{ftxui::Container::Horizontal({})};
    pState.sidebarWrapper = ftxui::Container::Horizontal({sidebar});
    pState.playbackBtn = detail::toggleButton(InterfaceText::pause, InterfaceText::play, pState.playback, [this] {
        player.aud.togglePlayback();
        pState.playback = !pState.playback;
    });
    ftxui::Component controls{ftxui::Container::Vertical(
        {ftxui::Container::Horizontal({prevBtn, pState.playbackBtn, nextBtn, muteBtn}),
         ftxui::Container::Horizontal(
             {visBtn, renderFastBtn, seekPrevBtn, seekNextBtn, loopBtn, autorunBtn, shuffleBtn}
         )}
    )};

    /// TODO: Finish canvas.
    /// ftxui::Component canvas{detail::button("", [this] {})};
    ftxui::Component mainContainer{
        ftxui::Container::Vertical({ftxui::Container::Horizontal({pState.sidebarWrapper}), controls})
    };
    return ftxui::Renderer(mainContainer, [=, this] {
        std::string dspName{pState.dspTracks[pState.trackSelected]};
        ftxui::Element controls{ftxui::vbox(
            ftxui::hbox(
                prevBtn->Render(), ftxui::separatorEmpty(), pState.playbackBtn->Render(), ftxui::separatorEmpty(),
                ftxui::text(std::format("{:.50}{}", dspName, dspName.length() > 50 ? "..." : "")),
                ftxui::separatorEmpty(), nextBtn->Render(), ftxui::separatorEmpty(), muteBtn->Render(),
                ftxui::separatorEmpty(),
                ftxui::text(std::format("{}", static_cast<int>(player.aud.getState().volume.load() * 100)))
            ) | ftxui::xflex |
                ftxui::center,
            ftxui::separatorEmpty(),
            ftxui::hbox(
                visBtn->Render(), ftxui::separatorEmpty(), renderFastBtn->Render(), ftxui::separatorEmpty(),
                seekPrevBtn->Render(), ftxui::separatorEmpty(),
                ftxui::text(detail::formatTimestamp(player.aud.getState().timestamp.load().count())),
                ftxui::separatorEmpty(), seekNextBtn->Render(), ftxui::separatorEmpty(), loopBtn->Render(),
                ftxui::separatorEmpty(), autorunBtn->Render(), ftxui::separatorEmpty(), shuffleBtn->Render()
            ) | ftxui::xflex |
                ftxui::center
        )};
        ftxui::Element body{
            ftxui::hbox(ftxui::filler(), pState.sidebarWrapper->Render() | ftxui::yframe) | ftxui::xflex
        };
        ftxui::Element main{
            ftxui::vbox(body | ftxui::yflex, ftxui::separatorEmpty(), controls | ftxui::xflex) | ftxui::flex
        };
        return main;
    });
};

ftxui::Component Interface::root() {
    ftxui::Component body{ftxui::Container::Tab({home(), play()}, &uiState.currentView)};
    ftxui::Component content{ftxui::Container::Vertical({header(), body})};
    return ftxui::Renderer(content, [content] {
        return ftxui::window(ftxui::text(InterfaceText::windowTitle), content->Render(), ftxui::BorderStyle::EMPTY);
    });
}

void Interface::run() {
    populatePlaylists();
    populateRecentlyPlayed();

    // Required so that pState.sidebar is not left uninitialized.
    newPlayState({0}, 0);
    uiState.currentView = static_cast<int>(PlayerView::HOME);

    // Render at 20 FPS.
    ftxui::Loop loop{ftxui::Loop{&scr, root()}};
    while (!loop.HasQuitted()) {
        uiState.renderFast ? loop.RunOnce() : loop.RunOnceBlocking();
        std::this_thread::sleep_for(std::chrono::milliseconds{33});
        uiState.renderFast ? scr.PostEvent(ftxui::Event::Custom) : (void)0;
        customEvents();
    }
}

void Interface::quit() { scr.Exit(); }

void Interface::customEvents() {
    if (pState.autorun && player.aud.getState().ended.load()) {
        pState.trackSelected = std::clamp(pState.trackSelected + 1, 0, static_cast<int>(pState.dspTracks.size() - 1));
        player.aud.playEntry(player.data.fileEntries[pState.trackMap[pState.trackSelected]]);
    }
}

void Interface::populatePlaylists() {
    std::transform(
        player.data.playlists.begin(), player.data.playlists.end(), std::back_inserter(hState.dspPlaylists),
        [](const auto &e) { return e.playlistName; }
    );
}

void Interface::populateRecentlyPlayed() {
    std::vector<std::pair<Entry, EntryId>> entries{};
    std::transform(
        player.data.fileEntries.begin(), player.data.fileEntries.end(), std::back_inserter(entries),
        [&](const auto &e) {
            static EntryId uid{};
            return std::pair<Entry, EntryId>(e, uid++);
        }
    );
    /**
        NOTE: For now, recently played means time last modified. This is because
        file creation times are OS-specific, and will be dealt with later.
    */
    std::sort(entries.begin(), entries.end(), [&](const auto &a, const auto &b) {
        return a.first.timeModified > b.first.timeModified;
    });
    std::transform(entries.begin(), entries.end(), std::back_inserter(hState.dspRecentlyAdded), [](const auto &e) {
        return std::string{
            reinterpret_cast<const char *>(e.first.asPath().filename().replace_extension("").u8string().data())
        };
    });
    std::transform(entries.begin(), entries.end(), std::back_inserter(hState.recAddedMap), [](const auto &e) {
        return e.second;
    });
}

void Interface::generateSidebar() {
    pState.sidebarWrapper->DetachAllChildren();
    pState.sidebarWrapper->Add(
        detail::menu(
            InterfaceText::songIcon, InterfaceText::selectedMarker, pState.trackSelected, pState.dspTracks,
            [this] {
                player.aud.playEntry(player.data.fileEntries[pState.trackMap[pState.trackSelected]]);
                pState.playbackBtn->TakeFocus();
            },
            true, ftxui::Color::GrayLight, ftxui::Color::GrayDark,
            [this](const ftxui::EntryState &est) {
                bool activeSelected{est.index == pState.trackSelected};
                std::string marker{std::format("     {}", InterfaceText::selectedMarker)};
                ftxui::Element text{ftxui::text(
                    std::format(
                        "{} {} {:.50}{}",
                        est.focused ? (activeSelected ? InterfaceText::activeSong : marker)
                                    : (activeSelected ? InterfaceText::activeSong  : "      "),
                        InterfaceText::songIcon, est.label, est.label.length() > 50 ? "..." : ""
                    )
                )};
                if (activeSelected) {
                    text |= ftxui::color(ftxui::Color::White);
                } else {
                    text |= ftxui::color(est.focused ? ftxui::Color::GrayLight : ftxui::Color::GrayDark);
                }
                text |= ftxui::bgcolor(ftxui::Color::RGBA(0, 0, 0, 0));
                return text;
                return ftxui::text("");
            }
        )
    );
}

void Interface::newPlayState(const std::vector<EntryId> &tracks, const int initialTrack) {
    pState.trackSelected = initialTrack;
    pState.dspTracks.clear();
    pState.trackMap.clear();
    pState.trackMap = tracks;
    for (const auto t : tracks) {
        pState.dspTracks.push_back(
            std::string{reinterpret_cast<const char *>(
                player.data.fileEntries[t].asPath().filename().replace_extension("").u8string().data()
            )}
        );
    }
    generateSidebar();
}

} // namespace tml