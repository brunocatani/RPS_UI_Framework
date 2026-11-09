#pragma once

// Controller binding icons, drawn with Dear ImGui's draw list so they take any
// size and any theme colour with no texture: one icon per control, a mark for
// the gesture (tap, double tap, hold, release) and the hand's letter. chip()
// puts an icon beside its sentence (BindingText.h) on a theme surface.

#include <RPS_UI_Framework/BindingText.h>
#include <RPS_UI_Framework/ThemeStyle.h>

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <numbers>

namespace rpsui::binding
{
    namespace icon_detail
    {
        [[nodiscard]] inline ImVec2 at(ImVec2 center, float size, float x, float y) noexcept
        {
            return { center.x + x * size, center.y + y * size };
        }

        inline void centeredText(ImDrawList* draw, ImVec2 center, float pixels, ImU32 ink, const char* text) noexcept
        {
            auto* font = ImGui::GetFont();
            const auto extent = font->CalcTextSizeA(pixels, 10000.0f, 0.0f, text);
            draw->AddText(font, pixels, { center.x - extent.x * 0.5f, center.y - extent.y * 0.5f }, ink, text);
        }

        inline void arrow(ImDrawList* draw, ImVec2 tip, Direction direction, float length, ImU32 ink) noexcept
        {
            const float dx = direction == Direction::Left ? -1.0f : direction == Direction::Right ? 1.0f : 0.0f;
            const float dy = direction == Direction::Up ? -1.0f : direction == Direction::Down ? 1.0f : 0.0f;
            const ImVec2 base{ tip.x - dx * length, tip.y - dy * length };
            draw->AddTriangleFilled(tip, { base.x + dy * length * 0.7f, base.y - dx * length * 0.7f },
                { base.x - dy * length * 0.7f, base.y + dx * length * 0.7f }, ink);
        }
    }

    // The control itself, inside a size x size box around center.
    inline void drawControl(ImDrawList* draw, ImVec2 center, float size, const Input& input, ImU32 ink) noexcept
    {
        using icon_detail::at;
        const float line = (std::max)(1.5f, size * 0.07f);
        const float r = size * 0.32f;
        switch (input.control) {
        case Control::Trigger:
            // A curved trigger blade under its guard.
            draw->PathArcTo(at(center, size, -0.12f, 0.0f), size * 0.3f, -std::numbers::pi_v<float> * 0.5f,
                std::numbers::pi_v<float> * 0.5f, 16);
            draw->PathStroke(ink, line * 1.6f);
            draw->AddLine(at(center, size, -0.12f, -0.3f), at(center, size, -0.36f, -0.3f), ink, line);
            break;
        case Control::Grip:
        case Control::GripClick: {
            const ImVec2 lo = at(center, size, -0.16f, -0.36f), hi = at(center, size, 0.16f, 0.36f);
            if (input.control == Control::GripClick) draw->AddRectFilled(lo, hi, ink, size * 0.12f);
            else draw->AddRect(lo, hi, ink, size * 0.12f, line);
            const ImU32 ridge = input.control == Control::GripClick ? ImGui::GetColorU32(ImGuiCol_WindowBg) : ink;
            for (const float y : { -0.15f, 0.0f, 0.15f })
                draw->AddLine(at(center, size, -0.08f, y), at(center, size, 0.08f, y), ridge, line);
            break;
        }
        case Control::LowerFace:
        case Control::UpperFace:
            draw->AddCircle(center, r, ink, 32, line);
            icon_detail::centeredText(draw, center, size * 0.42f,
                ink, controlName(input.control, input.hand).data());
            break;
        case Control::StickClick:
            draw->AddCircle(center, r, ink, 32, line);
            draw->AddCircle(center, r * 0.6f, ink, 24, line);
            draw->AddCircleFilled(center, r * 0.3f, ink, 16);
            break;
        case Control::Stick: {
            draw->AddCircle(center, r, ink, 32, line);
            const float dx = input.direction == Direction::Left ? -1.0f : input.direction == Direction::Right ? 1.0f : 0.0f;
            const float dy = input.direction == Direction::Up ? -1.0f : input.direction == Direction::Down ? 1.0f : 0.0f;
            draw->AddCircleFilled({ center.x + dx * r * 0.35f, center.y + dy * r * 0.35f }, r * 0.35f, ink, 16);
            if (input.direction != Direction::None)
                icon_detail::arrow(draw, { center.x + dx * size * 0.47f, center.y + dy * size * 0.47f }, input.direction, size * 0.1f, ink);
            break;
        }
        case Control::Axis3Click:
            draw->AddRect(at(center, size, -0.34f, -0.13f), at(center, size, 0.34f, 0.13f), ink, size * 0.13f, line);
            break;
        case Control::Axis4Click:
            draw->AddCircle(center, r, ink, 32, line);
            icon_detail::centeredText(draw, center, size * 0.4f, ink, "4");
            break;
        case Control::PadUp:
        case Control::PadDown:
        case Control::PadLeft:
        case Control::PadRight: {
            draw->AddCircle(center, r, ink, 32, line);
            const Direction direction = input.control == Control::PadUp ? Direction::Up :
                input.control == Control::PadDown ? Direction::Down :
                input.control == Control::PadLeft ? Direction::Left : Direction::Right;
            const float dx = direction == Direction::Left ? -1.0f : direction == Direction::Right ? 1.0f : 0.0f;
            const float dy = direction == Direction::Up ? -1.0f : direction == Direction::Down ? 1.0f : 0.0f;
            icon_detail::arrow(draw, { center.x + dx * r * 0.85f, center.y + dy * r * 0.85f }, direction, r * 0.5f, ink);
            break;
        }
        case Control::PadCenter:
            draw->AddCircle(center, r, ink, 32, line);
            draw->AddCircleFilled(center, r * 0.32f, ink, 16);
            break;
        case Control::None:
            draw->AddCircle(center, r, ink, 32, line);
            draw->AddLine(at(center, size, -0.22f, 0.22f), at(center, size, 0.22f, -0.22f), ink, line);
            break;
        }
    }

    // The gesture's mark in the box's lower right corner; none for a plain press.
    inline void drawGesture(ImDrawList* draw, ImVec2 center, float size, Gesture gesture, ImU32 ink) noexcept
    {
        const ImVec2 corner = icon_detail::at(center, size, 0.38f, 0.38f);
        const float dot = size * 0.06f;
        switch (gesture) {
        case Gesture::Press:
            break;
        case Gesture::Tap:
            draw->AddCircleFilled(corner, dot, ink, 12);
            break;
        case Gesture::DoubleTap:
            draw->AddCircleFilled({ corner.x - dot * 1.6f, corner.y }, dot, ink, 12);
            draw->AddCircleFilled({ corner.x + dot * 1.6f, corner.y }, dot, ink, 12);
            break;
        case Gesture::Hold:
            // A clock face, three quarters round.
            draw->PathArcTo(corner, size * 0.1f, -std::numbers::pi_v<float> * 0.5f, std::numbers::pi_v<float>, 12);
            draw->PathStroke(ink, (std::max)(1.2f, size * 0.045f));
            break;
        case Gesture::Release:
            icon_detail::arrow(draw, { corner.x, corner.y - size * 0.1f }, Direction::Up, size * 0.09f, ink);
            break;
        }
    }

    // A full icon: the control, its gesture, and L or R in the upper left corner.
    inline void drawIcon(ImDrawList* draw, ImVec2 center, float size, const Binding& binding, ImU32 ink) noexcept
    {
        drawControl(draw, center, size, binding.input, ink);
        drawGesture(draw, center, size, binding.gesture, ink);
        if (binding.input.control != Control::None)
            icon_detail::centeredText(draw, icon_detail::at(center, size, -0.38f, -0.38f), size * 0.26f, ink,
                binding.input.hand == Hand::Left ? "L" : "R");
    }

    // The icon and its sentence on a theme surface, as one ImGui item of the
    // given height at the cursor; width 0 fits the text.
    inline void chip(const Binding& binding, const api::framework::Theme& theme, float height, float width = 0.0f)
    {
        const auto text = sentence(binding);
        const float pixels = height * 0.5f;
        auto* font = ImGui::GetFont();
        const float textWidth = font->CalcTextSizeA(pixels, 10000.0f, 0.0f, text.c_str()).x;
        const float pad = height * 0.25f;
        const ImVec2 size{ width > 0.0f ? width : height + textWidth + pad * 2.0f, height };
        const ImVec2 lo = ImGui::GetCursorScreenPos(), hi{ lo.x + size.x, lo.y + size.y };
        auto* draw = ImGui::GetWindowDrawList();
        draw->AddRectFilled(lo, hi, ImGui::GetColorU32(imgui_host::color(theme.surface)), theme.rounding);
        draw->AddRect(lo, hi, ImGui::GetColorU32(imgui_host::color(theme.border)), theme.rounding, theme.borderWidth);
        const ImU32 ink = ImGui::GetColorU32(imgui_host::color(binding.input.control == Control::None ? theme.textDisabled : theme.text));
        drawIcon(draw, { lo.x + height * 0.5f, lo.y + height * 0.5f }, height * 0.82f, binding, ink);
        draw->PushClipRect({ lo.x + height, lo.y }, { hi.x - pad * 0.5f, hi.y }, true);
        draw->AddText(font, pixels, { lo.x + height + pad * 0.5f, lo.y + (height - pixels) * 0.5f }, ink, text.c_str());
        draw->PopClipRect();
        ImGui::Dummy(size);
    }
}
