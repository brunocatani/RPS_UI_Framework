#pragma once
#include <RPS_UI_Framework/Framework.h>
#include <imgui.h>

// The settings menu draws with the player's RPS UI theme. bind() makes the theme of the
// running setup or draw callback current (render thread); current() is RPS
// UI's built-in theme outside a callback, which the desktop preview uses.
namespace rpsui::settings_menu::theme {
void bind(const rpsui::api::framework::Theme* value) noexcept;
[[nodiscard]] const rpsui::api::framework::Theme& current() noexcept;
[[nodiscard]] inline ImVec4 role(const rpsui::api::framework::ThemeColor& value,float alpha=1.f) noexcept {return {value.r,value.g,value.b,value.a*alpha};}
// Fills of chosen things and the wheel's marks.
[[nodiscard]] inline ImVec4 accent(float alpha=1.f) noexcept {return role(current().accent,alpha);}
[[nodiscard]] inline ImVec4 text(float alpha=1.f) noexcept {return role(current().text,alpha);}
// A panel's body.
[[nodiscard]] inline ImVec4 ink(float alpha=1.f) noexcept {return role(current().background,alpha);}
// Text and marks on an accent fill.
[[nodiscard]] inline ImVec4 onAccent(float alpha=1.f) noexcept {return role(current().onAccent,alpha);}
[[nodiscard]] inline ImVec4 muted(float alpha=1.f) noexcept {return role(current().textMuted,alpha);}
[[nodiscard]] inline ImVec4 border(float alpha=1.f) noexcept {return role(current().border,alpha);}
// The theme's body size against the size the layout was drawn for.
[[nodiscard]] inline float textScale() noexcept {return current().fontSize/20.f;}
// Binds a callback's theme for its scope.
class Scope {
public:
    explicit Scope(const rpsui::api::framework::Theme* value) noexcept {bind(value);}
    ~Scope() {bind(nullptr);}
    Scope(const Scope&)=delete;
    Scope& operator=(const Scope&)=delete;
};
}
