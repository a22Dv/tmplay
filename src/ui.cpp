#include <algorithm>
#include <cstddef>
#include <iterator>
#include <vector>

#include <ftxui/component/component.hpp>
#include <ftxui/component/component_options.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/dom/elements.hpp>
#include <ftxui/dom/node.hpp>

#include <player.hpp>

namespace tml {

void InterfaceComponents::init() {
    UserInterface &ui{linkedInterface};
    std::vector<Entry> &entries{ui.player.data.fileEntries};

    // Set buttons.
    hBtn = ftxui::Button(
        ui.elements.homeText, [&ui] { ui.player.state.view = PlayerView::HOME; }, ftxui::ButtonOption::Ascii()
    );
    qBtn = ftxui::Button(ui.elements.quitText, [&ui] { ui.player.quit(); }, ftxui::ButtonOption::Ascii());
    qhContainer = ftxui::Container::Horizontal({hBtn, qBtn});

    // Set recently added.
    std::transform(entries.begin(), entries.end(), std::back_inserter(recAddedEntries), [&ui](const auto &e) {
        return std::format(
            "{} {}", ui.elements.songIcon,
            reinterpret_cast<const char *>(e.asPath().filename().replace_extension("").u8string().data())
        );
    });

    // Set playlists.
    constexpr std::size_t displayRowLimit{5};
    constexpr std::size_t displayHeightLimit{2};
    std::vector<PlaylistCompact> &playlistsC{ui.player.data.playlists};
    std::transform(playlistsC.begin(), playlistsC.end(), std::back_inserter(playlistsEntries), [&ui](const auto &e) {
        return std::format("{} {}", ui.elements.playlistIcon, e.playlistName);
    });

    // Menus.
    auto customTransform{[](const ftxui::EntryState &st) {
        ftxui::Element text{ftxui::text(st.label)};
        text |= ftxui::color(st.focused ? ftxui::Color::White : ftxui::Color::GrayDark);
        return text;
    }};
    ftxui::MenuOption entryOptRec{ftxui::MenuOption::Vertical()};
    ftxui::MenuOption entryOptList{ftxui::MenuOption::Horizontal()};
    entryOptList.entries_option.transform = customTransform;
    entryOptRec.entries_option.transform = customTransform;
    playlistsList = ftxui::Menu(playlistsEntries, &ui.player.state.homePlaylistSel, entryOptList);
    recAddedList = ftxui::Menu(recAddedEntries, &ui.player.state.homeRecentlyAddedSel, entryOptRec);
    rootContainer = ftxui::Container::Vertical({qhContainer, recAddedList, playlistsList});
}

void UserInterface::run() {
    components.init();
    scr.Loop(getRoot());
}

void UserInterface::quit() { scr.Exit(); }

ftxui::Component UserInterface::playRoot() { return ftxui::Component{}; }

ftxui::Component UserInterface::homeRoot() {
    // Header.
    auto header{[this] {
        return ftxui::hbox(elements.appIcon, ftxui::filler(), components.hBtn->Render(), components.qBtn->Render());
    }};

    // Recently added.
    auto recentlyAdded{[this] {
        // Get entries.
        int height{static_cast<int>(ftxui::Terminal::Size().dimy * 0.6)};
        return ftxui::vbox(
            elements.recAddedHeader, ftxui::separatorEmpty(),
            components.recAddedList->Render() | ftxui::yframe | ftxui::size(ftxui::HEIGHT, ftxui::LESS_THAN, height)
        );
    }};

    // Displayed playlists.
    auto playlists{[this] {
        return ftxui::vbox(
            elements.playlistHeader, ftxui::separatorEmpty(), components.playlistsList->Render() | ftxui::xframe
        );
    }};

    // Main content.
    auto content{[=] {
        return ftxui::vbox(header(), ftxui::separatorEmpty(), recentlyAdded(), ftxui::separatorEmpty(), playlists());
    }};

    // Padded content.
    auto padded{[content] {
        return ftxui::hbox(ftxui::separatorEmpty(), content() | ftxui::flex, ftxui::separatorEmpty());
    }};

    // Main root.
    auto root{[=, this] {
        ftxui::Element window{ftxui::window(elements.windowTitle, padded())};
        return window;
    }};
    return ftxui::Renderer(components.rootContainer, root);
}

bool UserInterface::onEvent(const ftxui::Event &event) {
    /// TODO: Temporary stub.
    if (event == ftxui::Event::Return) {
        player.aud.playEntry(player.data.fileEntries[player.state.homeRecentlyAddedSel]);
        return true;
    }
    return false;
}

ftxui::Component UserInterface::getRoot() {
    ftxui::Component root{[this] {
        switch (player.state.view) {
        case PlayerView::HOME: return homeRoot();
        case PlayerView::PLAY: return playRoot();
        }
    }()};
    return root | ftxui::CatchEvent([this](const ftxui::Event &e) { return this->onEvent(e); });
}

} // namespace tml