#pragma once

#include <DirectXMath.h>

#include <cstdint>
#include <array>

struct ImFont;

namespace devui::render
{
    inline constexpr std::uint32_t kPanelLayoutWidth = 1440;
    inline constexpr std::uint32_t kPanelLayoutHeight = 900;
    inline constexpr float kPanelAspectRatio =
        static_cast<float>(kPanelLayoutWidth) / static_cast<float>(kPanelLayoutHeight);
    inline constexpr float kDefaultPanelPhysicalWidth = 96.0f;
    inline constexpr float kDefaultPanelPhysicalHeight =
        kDefaultPanelPhysicalWidth / kPanelAspectRatio;
    inline constexpr float kMinimumPanelPhysicalWidth = 72.0f;
    inline constexpr float kMaximumPanelPhysicalWidth = 180.0f;
    // Panel layouts in ImGui units; RPS UI picks the textures' resolution. The keyboard
    // panel sits under the Config panel at the same physical width.
    inline constexpr std::uint32_t kKeyboardPanelLayoutWidth = 2080;
    inline constexpr std::uint32_t kKeyboardPanelLayoutHeight = 480;

    enum class FontRole : std::uint8_t
    {
        Body,
        Medium,
        Heading,
        Display,
        Mono,
        Count,
    };
    inline constexpr std::array<float,static_cast<std::size_t>(FontRole::Count)> kFontSizes{20,21,27,32,18};

    struct PanelPose
    {
        DirectX::XMFLOAT3 center{};
        DirectX::XMFLOAT3 right{};
        DirectX::XMFLOAT3 up{};
        DirectX::XMFLOAT3 front{};
        float physicalWidth{ kDefaultPanelPhysicalWidth };
        float physicalHeight{ kDefaultPanelPhysicalHeight };
    };

    // The desktop preview loads its own faces; the game takes RPS UI's theme faces.
    void PrepareFonts() noexcept;
    [[nodiscard]] ImFont* GetFont(FontRole role) noexcept;
    // A role's size, scaled by the theme's body size.
    [[nodiscard]] float FontSize(FontRole role) noexcept;
    // Closing the Config panel closes the keyboard panel with it.
    bool SetPanelOpen(bool open, const PanelPose* pose = nullptr) noexcept;
    // Game thread, once per input frame: opens the keyboard panel under the Config panel while
    // wanted, moves it whenever the Config panel is resized, and closes it otherwise.
    void SyncKeyboardPanel(bool wanted) noexcept;
    void Shutdown() noexcept;
}
