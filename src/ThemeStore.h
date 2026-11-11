#pragma once

// Theme files, Mods_Config\RPS_UI_Framework\Themes\<name>.toml. Each holds the
// colours, faces and sizes of support/RPS_UI_Framework/ThemeDefaults.h; a key
// it lacks takes the value of the built-in theme of that name, or of the default. Faces are read from
// Mods_Config\RPS_UI_Framework\Fonts, then from Windows' fonts.
//
// Used only by the settings watcher (UiSettings.cpp), never on a render or
// input thread: loading reads files.

#include <RPS_UI_Framework/Framework.h>

#include <array>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace rpsui::theme_store
{
    using FaceBytes = std::shared_ptr<const std::vector<char>>;

    // Immutable once published. Panel contexts keep it alive while their
    // atlases borrow its face bytes.
    struct ThemeData
    {
        api::framework::Theme theme{};
        std::array<FaceBytes, api::framework::kFontRoleCount> faces{};
        std::filesystem::path file;
    };

    // The default built-in theme with its default faces, read from disk.
    [[nodiscard]] std::shared_ptr<const ThemeData> builtIn(const std::filesystem::path& configDirectory, std::uint64_t revision);

    // Reads Themes\<name>.toml, writing it first when it is missing (every
    // built-in theme's file is always written). A bad name or an unreadable file
    // gives the built-in theme; every problem is logged with the file.
    [[nodiscard]] std::shared_ptr<const ThemeData> load(const std::filesystem::path& configDirectory, std::string_view name,
        std::uint64_t revision);

    // A name a theme file may have: letters, digits, space, '-', '_' and '.'.
    [[nodiscard]] bool validName(std::string_view name) noexcept;
}
