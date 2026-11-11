#pragma once
#include "NativeRenderer.h"
#include "UiVisualStyle.h"
#include "../TextFit.h"
#include <algorithm>
#include <array>
#include <cctype>
#include <cfloat>
#include <string_view>
#include <cstring>

namespace devui::visual {
inline float railWidth(float width) { return std::clamp(width * 0.17f, 280.0f, 320.0f); }
struct ReadableLabel {
    std::array<char, 192> text{};
    explicit ReadableLabel(std::string_view key, bool stripTypePrefix = false) {
        std::size_t out = 0;
        const std::size_t begin = stripTypePrefix && key.size() > 1 &&
            (key[0] == 'b' || key[0] == 'f' || key[0] == 'i' || key[0] == 's') && std::isupper(static_cast<unsigned char>(key[1])) ? 1 : 0;
        for (std::size_t i = begin; i < key.size() && out + 2 < text.size(); ++i) {
            if (i > begin && std::isupper(static_cast<unsigned char>(key[i])) &&
                (std::islower(static_cast<unsigned char>(key[i-1])) ||
                (std::isupper(static_cast<unsigned char>(key[i-1])) && i+1 < key.size() && std::islower(static_cast<unsigned char>(key[i+1]))))) text[out++] = ' ';
            text[out++] = key[i] == '_' ? ' ' : key[i];
        }
    }
};
inline ImU32 color(ImVec4 value) { return ImGui::ColorConvertFloat4ToU32(value); }
// A hand-drawn control's box, the way every RPS UI panel shows a chosen choice
// (rpsui::imgui_host::pushChosen): an accent tint, a thick accent outline and, for its
// label, accent ink (inkFor). Hovered gets a lighter tint.
inline void chosenBox(ImDrawList* draw,ImVec2 minimum,ImVec2 maximum,bool chosen,bool hovered) {
    const auto& theme=rpsui::settings_menu::theme::current();
    if(chosen || hovered)draw->AddRectFilled(minimum,maximum,color(accent(chosen?0.20f:0.12f)),theme.rounding);
    draw->AddRect(minimum,maximum,color(chosen?accent():rpsui::settings_menu::theme::border()),theme.rounding,
        chosen?theme.borderWidth*2.f:theme.borderWidth);
}
inline ImVec4 inkFor(bool chosen,bool hovered) { return chosen || hovered?accent():muted(); }
inline bool button(const char* label,ImVec2 size={},bool selected=false) {
    // Keep native button input/IDs; the label is drawn below, fitted to the button.
    if(selected)rpsui::imgui_host::pushChosen(rpsui::settings_menu::theme::current());
    ImGui::PushStyleColor(ImGuiCol_Text,ImVec4(0,0,0,0));
    const bool clicked=ImGui::Button(label,size);
    ImGui::PopStyleColor();
    if(selected)rpsui::imgui_host::popChosen();
    const auto minimum=ImGui::GetItemRectMin(),maximum=ImGui::GetItemRectMax();
    const char* end=std::strstr(label,"##");
    auto* font=ImGui::GetFont();float fontSize=ImGui::GetFontSize();
    auto extent=font->CalcTextSizeA(fontSize,FLT_MAX,0,label,end);
    const float available=(std::max)(1.f,maximum.x-minimum.x-2*ImGui::GetStyle().FramePadding.x);
    if(extent.x>available){fontSize*=available/extent.x;extent=font->CalcTextSizeA(fontSize,FLT_MAX,0,label,end);}
    const auto align=ImGui::GetStyle().ButtonTextAlign;
    auto* draw=ImGui::GetWindowDrawList();draw->PushClipRect(minimum,maximum,true);
    draw->AddText(font,fontSize,{minimum.x+(maximum.x-minimum.x-extent.x)*align.x,minimum.y+(maximum.y-minimum.y-extent.y)*align.y},
        ImGui::GetColorU32(selected || ImGui::IsItemHovered()?accent():text()),label,end);
    draw->PopClipRect();return clicked;
}
inline bool checkbox(const char* label,bool* value) { return ImGui::Checkbox(label,value); }
class Font {
public:
    Font(render::FontRole role, float size) { ImGui::PushFont(render::GetFont(role) ? render::GetFont(role) : ImGui::GetFont(), size); }
    ~Font() { ImGui::PopFont(); }
    Font(const Font&) = delete;
    Font& operator=(const Font&) = delete;
};
inline void caption(const char* label) {
    Font font(render::FontRole::Medium, 18);
    ImGui::TextColored(muted(), "%s", label);
}
inline void heading(const char* title, const char* subtitle) {
    { Font font(render::FontRole::Heading, 40); ImGui::TextUnformatted(title); }
    {
        // Always one line of one height: a status prepended after a click must not move
        // the rows under the pointer.
        Font font(render::FontRole::Body, 24);
        const float lineHeight = ImGui::GetTextLineHeightWithSpacing();
        const float natural = ImGui::CalcTextSize(subtitle).x;
        const float available = ImGui::GetContentRegionAvail().x;
        const float top = ImGui::GetCursorPosY();
        { Font fitted(render::FontRole::Body, natural > available ? 24 * available / natural : 24);
          ImGui::TextColored(muted(), "%s", subtitle); }
        ImGui::SetCursorPosY(top + lineHeight);
    }
    ImGui::Dummy({0, 12});
}
inline bool navigation(const char* label, bool selected, const char* badge = nullptr) {
    const auto p = ImGui::GetCursorScreenPos();
    const float width = ImGui::GetContentRegionAvail().x;
    constexpr float height = 58;
    const bool clicked = ImGui::InvisibleButton(label, {width, height});
    const bool hovered = ImGui::IsItemHovered();
    auto* draw = ImGui::GetWindowDrawList();
    chosenBox(draw, p, {p.x + width, p.y + height}, selected, hovered);

    const float badgeWidth = badge ? ImGui::GetFont()->CalcTextSizeA(16, FLT_MAX, 0, badge).x : 0;
    const float right = p.x + width - 12 - (badge ? badgeWidth + 8 : 0);
    auto* font = render::GetFont(render::FontRole::Medium) ? render::GetFont(render::FontRole::Medium) : ImGui::GetFont();
    const float wrap = right - p.x - 16;
    const ReadableLabel readable(label);
    const float size = rpsui::settings_menu::fitWrappedSize(font, readable.text.data(), wrap, height - 6, badge ? 20.0f : 26.0f, 14.0f);
    const auto extent = font->CalcTextSizeA(size, FLT_MAX, wrap, readable.text.data());
    draw->PushClipRect({p.x + 16, p.y}, {right, p.y + height}, true);
    draw->AddText(font, size, {p.x + 16, p.y + (std::max)(5.0f, (height - extent.y) * 0.5f)},
        color(inkFor(selected, hovered)), readable.text.data(), nullptr, wrap);
    draw->PopClipRect();
    if (badge) draw->AddText(ImGui::GetFont(), 16, {right + 8, p.y + 21}, color(inkFor(selected, hovered)), badge);
    if (hovered) ImGui::SetTooltip("%s", label);
    return clicked;
}
inline void tableHeader(const char* first, const char* second, const char* third = nullptr) {
    Font font(render::FontRole::Medium, 18);
    ImGui::TableSetupScrollFreeze(0, 1);
    ImGui::TableNextRow(ImGuiTableRowFlags_Headers, 34);
    ImGui::TableSetColumnIndex(0); ImGui::TableHeader(first);
    ImGui::TableSetColumnIndex(1); ImGui::TableHeader(second);
    if (third) { ImGui::TableSetColumnIndex(2); ImGui::TableHeader(third); }
}
}
