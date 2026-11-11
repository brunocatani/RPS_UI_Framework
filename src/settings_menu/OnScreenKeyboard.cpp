#include "PCH.h"

#include "OnScreenKeyboard.h"

#include "render/ConfigUi.h"

namespace rpsui::settings_menu
{
    namespace
    {
        using Font = devui::visual::Font;
        using devui::render::FontRole;

        enum class KeyAction : std::uint8_t
        {
            Text,
            Space,
            Backspace,
            Delete,
            Enter,
            Clear,
            Shift,
            Caps,
            Left,
            Right,
            Home,
            End,
            Previous,
            Next,
        };

        struct VirtualKey
        {
            const char* label;
            const char* shifted;
            float units;
            KeyAction action;
        };

        // Every row adds up to kRowUnits, so the edges line up. US layout: the characters
        // console commands use, whatever the physical keyboard is.
        constexpr float kRowUnits = 14.5f;
        constexpr std::size_t kRows = 5;
        using A = KeyAction;
        constexpr VirtualKey kRow0[]{ {"`","~",1,A::Text},{"1","!",1,A::Text},{"2","@",1,A::Text},{"3","#",1,A::Text},{"4","$",1,A::Text},{"5","%",1,A::Text},{"6","^",1,A::Text},{"7","&",1,A::Text},{"8","*",1,A::Text},{"9","(",1,A::Text},{"0",")",1,A::Text},{"-","_",1,A::Text},{"=","+",1,A::Text},{"Back","Back",1.5f,A::Backspace} };
        constexpr VirtualKey kRow1[]{ {"Caps","Caps",1.5f,A::Caps},{"q","Q",1,A::Text},{"w","W",1,A::Text},{"e","E",1,A::Text},{"r","R",1,A::Text},{"t","T",1,A::Text},{"y","Y",1,A::Text},{"u","U",1,A::Text},{"i","I",1,A::Text},{"o","O",1,A::Text},{"p","P",1,A::Text},{"[","{",1,A::Text},{"]","}",1,A::Text},{"\\","|",1,A::Text} };
        constexpr VirtualKey kRow2[]{ {"Clear","Clear",1.75f,A::Clear},{"a","A",1,A::Text},{"s","S",1,A::Text},{"d","D",1,A::Text},{"f","F",1,A::Text},{"g","G",1,A::Text},{"h","H",1,A::Text},{"j","J",1,A::Text},{"k","K",1,A::Text},{"l","L",1,A::Text},{";",":",1,A::Text},{"'","\"",1,A::Text},{"Enter","Enter",1.75f,A::Enter} };
        constexpr VirtualKey kRow3[]{ {"Shift","Shift",2.25f,A::Shift},{"z","Z",1,A::Text},{"x","X",1,A::Text},{"c","C",1,A::Text},{"v","V",1,A::Text},{"b","B",1,A::Text},{"n","N",1,A::Text},{"m","M",1,A::Text},{",","<",1,A::Text},{".",">",1,A::Text},{"/","?",1,A::Text},{"Shift","Shift",2.25f,A::Shift} };
        constexpr VirtualKey kRow4[]{ {"Prev","Prev",1.25f,A::Previous},{"Next","Next",1.25f,A::Next},{"Left","Left",1.25f,A::Left},{"Right","Right",1.25f,A::Right},{"Space","Space",6.5f,A::Space},{"Home","Home",1,A::Home},{"End","End",1,A::End},{"Del","Del",1,A::Delete} };
        struct Row { const VirtualKey* keys; std::size_t count; };
        constexpr Row kKeyRows[kRows]{ {kRow0,std::size(kRow0)},{kRow1,std::size(kRow1)},{kRow2,std::size(kRow2)},{kRow3,std::size(kRow3)},{kRow4,std::size(kRow4)} };

        [[nodiscard]] ImU32 packed(const ImVec4& color) noexcept { return ImGui::ColorConvertFloat4ToU32(color); }

        [[nodiscard]] bool isLetter(const VirtualKey& key) noexcept
        {
            return key.action == A::Text && key.label[0] >= 'a' && key.label[0] <= 'z';
        }

        // Draws the label itself: ImGui's "##" ID syntax cannot show a '#' key.
        [[nodiscard]] bool keyButton(const char* label, ImVec2 size, bool selected, bool repeats)
        {
            if (repeats) {
                ImGui::PushItemFlag(ImGuiItemFlags_ButtonRepeat, true);
            }
            const ImVec2 minimum = ImGui::GetCursorScreenPos();
            const bool clicked = ImGui::InvisibleButton("key", size);
            if (repeats) {
                ImGui::PopItemFlag();
            }
            const bool hovered = ImGui::IsItemHovered();
            const ImVec2 maximum{ minimum.x + size.x, minimum.y + size.y };
            auto* draw = ImGui::GetWindowDrawList();
            devui::visual::chosenBox(draw, minimum, maximum, selected, hovered);

            auto* font = ImGui::GetFont();
            const float fontSize = ImGui::GetFontSize();
            const auto extent = font->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, label);
            draw->AddText(font, fontSize,
                { minimum.x + (size.x - extent.x) * 0.5f, minimum.y + (size.y - extent.y) * 0.5f },
                packed(selected || hovered ? devui::visual::accent() : devui::visual::text()), label);
            return clicked;
        }
    }

    void typeCharacter(ConsoleLine& line, std::uint32_t utf16Unit)
    {
        const auto unit = static_cast<wchar_t>(utf16Unit);
        std::array<char, 4> utf8{};
        const auto length = WideCharToMultiByte(CP_UTF8, 0, &unit, 1, utf8.data(), static_cast<int>(utf8.size()), nullptr, nullptr);
        if (length > 0) {
            line.insert({ utf8.data(), static_cast<std::size_t>(length) });
        }
    }

    bool applyEditKey(ConsoleLine& line, std::uint32_t virtualKey)
    {
        switch (virtualKey) {
        case VK_BACK: line.backspace(); return true;
        case VK_DELETE: line.erase(); return true;
        case VK_ESCAPE: line.clear(); return true;
        case VK_LEFT: line.moveLeft(); return true;
        case VK_RIGHT: line.moveRight(); return true;
        case VK_HOME: line.moveHome(); return true;
        case VK_END: line.moveEnd(); return true;
        case VK_UP: line.historyPrevious(); return true;
        case VK_DOWN: line.historyNext(); return true;
        default: return false;
        }
    }

    void drawLineField(const ConsoleLine& line, float width, float height, float fontSize,
        const char* placeholder, float& scroll, bool editing)
    {
        const ImVec2 minimum = ImGui::GetCursorScreenPos();
        const ImVec2 maximum{ minimum.x + width, minimum.y + height };
        auto* draw = ImGui::GetWindowDrawList();
        draw->AddRect(minimum, maximum, packed(devui::visual::accent(editing ? 1.0f : 0.7f)), 0.0f, editing ? 2.0f : 1.0f);
        ImGui::Dummy({ width, height });

        Font font(FontRole::Mono, fontSize);
        auto* face = ImGui::GetFont();
        const float size = ImGui::GetFontSize();
        const float inset = fontSize * 0.46f;
        const float textWidth = width - inset * 2.0f;
        const float textY = minimum.y + (height - size) * 0.5f;
        const auto& text = line.text();
        if (text.empty()) {
            draw->AddText(face, size, { minimum.x + inset, textY }, packed(devui::visual::muted()), placeholder);
        }
        const float caretOffset = face->CalcTextSizeA(size, FLT_MAX, 0.0f, text.data(), text.data() + line.caret()).x;
        if (caretOffset - scroll > textWidth) {
            scroll = caretOffset - textWidth;
        } else if (caretOffset < scroll) {
            scroll = caretOffset;
        }
        draw->PushClipRect({ minimum.x + inset, minimum.y }, { maximum.x - inset, maximum.y }, true);
        const float originX = minimum.x + inset - scroll;
        draw->AddText(face, size, { originX, textY }, packed(devui::visual::text()), text.data(), text.data() + text.size());
        if (editing && std::fmod(ImGui::GetTime(), 1.0) < 0.6) {
            draw->AddRectFilled({ originX + caretOffset, textY }, { originX + caretOffset + 2.0f, textY + size }, packed(devui::visual::accent()));
        }
        draw->PopClipRect();
    }

    bool OnScreenKeyboard::draw(ConsoleLine& line)
    {
        // Proportions of the 2080 x 480 panel, so the same layout fits any window it is drawn in.
        const float width = ImGui::GetWindowWidth();
        const float height = ImGui::GetWindowHeight();
        const float padding = height * 0.05f;
        const float gap = height / 60.0f;
        const float keyHeight = (height - padding * 2.0f - gap * static_cast<float>(kRows - 1)) / static_cast<float>(kRows);
        const float rowWidth = width - padding * 2.0f;

        bool enter = false;
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, { gap, gap });
        ImGui::SetCursorPos({ padding, padding });
        ImGui::BeginGroup();
        Font font(FontRole::Medium, keyHeight * 0.48f);
        ImGui::PushID("on-screen-keyboard");
        int id = 0;
        for (const auto& row : kKeyRows) {
            const float unit = (rowWidth - gap * static_cast<float>(row.count - 1)) / kRowUnits;
            for (std::size_t index = 0; index < row.count; ++index) {
                const auto& key = row.keys[index];
                if (index > 0) {
                    ImGui::SameLine();
                }
                const bool upper = isLetter(key) ? _shift != _caps : _shift && key.action == A::Text;
                const bool repeats = key.action == A::Backspace || key.action == A::Delete || key.action == A::Left || key.action == A::Right;
                const bool selected = (key.action == A::Shift && _shift) || (key.action == A::Caps && _caps);
                ImGui::PushID(id++);
                if (keyButton(upper ? key.shifted : key.label, { unit * key.units, keyHeight }, selected, repeats)) {
                    switch (key.action) {
                    case A::Text: line.insert(upper ? key.shifted : key.label); _shift = false; break;
                    case A::Space: line.insert(" "); _shift = false; break;
                    case A::Backspace: line.backspace(); break;
                    case A::Delete: line.erase(); break;
                    case A::Enter: enter = true; break;
                    case A::Clear: line.clear(); break;
                    case A::Shift: _shift = !_shift; break;
                    case A::Caps: _caps = !_caps; break;
                    case A::Left: line.moveLeft(); break;
                    case A::Right: line.moveRight(); break;
                    case A::Home: line.moveHome(); break;
                    case A::End: line.moveEnd(); break;
                    case A::Previous: line.historyPrevious(); break;
                    case A::Next: line.historyNext(); break;
                    }
                }
                ImGui::PopID();
            }
        }
        ImGui::PopID();
        ImGui::EndGroup();
        ImGui::PopStyleVar();
        return enter;
    }
}
