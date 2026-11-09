#pragma once

// Plain-language text for controller bindings: "Hold right grip", "Double-tap
// left X while holding left grip". Standard library only, so a mod's settings
// code and tests use it without Dear ImGui; BindingIcons.h draws the matching
// icons. Names follow the Touch layout that ROCK's settings use: A and B on the
// right controller, X and Y on the left. Text is lower case except button
// letters, so it reads inside a sentence; sentence() capitalizes its first word.

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace rpsui::binding
{
    enum class Hand : std::uint8_t
    {
        Left,
        Right,
    };

    enum class Control : std::uint8_t
    {
        None,
        Trigger,
        Grip,
        // A on the right controller, X on the left.
        LowerFace,
        // B on the right controller, Y on the left.
        UpperFace,
        StickClick,
        // The stick pushed in a direction.
        Stick,
        GripClick,
        Axis3Click,
        Axis4Click,
        PadUp,
        PadDown,
        PadLeft,
        PadRight,
        PadCenter,
    };

    enum class Gesture : std::uint8_t
    {
        Press,
        Tap,
        Hold,
        DoubleTap,
        Release,
    };

    enum class Direction : std::uint8_t
    {
        None,
        Up,
        Down,
        Left,
        Right,
    };

    struct Input
    {
        Hand hand{ Hand::Right };
        Control control{ Control::None };
        // For Control::Stick.
        Direction direction{ Direction::None };
    };

    struct Binding
    {
        Input input{};
        Gesture gesture{ Gesture::Press };
        // Held on while the input acts, for a chord.
        std::optional<Input> modifier{};
    };

    [[nodiscard]] constexpr std::string_view handName(Hand hand) noexcept
    {
        return hand == Hand::Left ? "left" : "right";
    }

    [[nodiscard]] constexpr std::string_view directionName(Direction direction) noexcept
    {
        switch (direction) {
        case Direction::Up: return "up";
        case Direction::Down: return "down";
        case Direction::Left: return "left";
        case Direction::Right: return "right";
        case Direction::None: break;
        }
        return "";
    }

    // The control as a player names it on that hand: "trigger", "A", "stick click".
    [[nodiscard]] constexpr std::string_view controlName(Control control, Hand hand) noexcept
    {
        const bool left = hand == Hand::Left;
        switch (control) {
        case Control::Trigger: return "trigger";
        case Control::Grip: return "grip";
        case Control::LowerFace: return left ? "X" : "A";
        case Control::UpperFace: return left ? "Y" : "B";
        case Control::StickClick: return "stick click";
        case Control::Stick: return "stick";
        case Control::GripClick: return "grip click";
        case Control::Axis3Click: return "bumper";
        case Control::Axis4Click: return "fourth button";
        case Control::PadUp: return "trackpad top";
        case Control::PadDown: return "trackpad bottom";
        case Control::PadLeft: return "trackpad left edge";
        case Control::PadRight: return "trackpad right edge";
        case Control::PadCenter: return "trackpad centre";
        case Control::None: break;
        }
        return "nothing";
    }

    // "right trigger", "left X", "right stick up".
    [[nodiscard]] inline std::string inputName(const Input& input)
    {
        if (input.control == Control::None) return "nothing";
        std::string text(handName(input.hand));
        text += ' ';
        text += controlName(input.control, input.hand);
        if (input.control == Control::Stick && input.direction != Direction::None) {
            text += ' ';
            text += directionName(input.direction);
        }
        return text;
    }

    // "Squeeze right grip", "Hold left Y", "Push right stick up", "Double-tap
    // right A while holding right grip", "Not bound".
    [[nodiscard]] inline std::string sentence(const Binding& binding)
    {
        if (binding.input.control == Control::None) return "Not bound";
        std::string_view verb;
        switch (binding.gesture) {
        case Gesture::Press:
            verb = binding.input.control == Control::Trigger ? "Pull" :
                binding.input.control == Control::Grip ? "Squeeze" :
                binding.input.control == Control::Stick ? "Push" : "Press";
            break;
        case Gesture::Tap: verb = "Tap"; break;
        case Gesture::Hold: verb = "Hold"; break;
        case Gesture::DoubleTap: verb = "Double-tap"; break;
        case Gesture::Release: verb = "Release"; break;
        }
        std::string text(verb);
        text += ' ';
        text += inputName(binding.input);
        if (binding.modifier && binding.modifier->control != Control::None) {
            text += " while holding ";
            text += inputName(*binding.modifier);
        }
        return text;
    }

    // ROCK's button-layout values: trigger, grip, a, b (x, y on the left),
    // thumbstick, grip_click, axis3, axis4, pad_up, pad_down, pad_left,
    // pad_right, pad_center, none. Case-insensitive; nullopt when unknown.
    [[nodiscard]] inline std::optional<Control> parseControl(std::string_view text) noexcept
    {
        const auto is = [&](std::string_view name) {
            if (text.size() != name.size()) return false;
            for (std::size_t i = 0; i < name.size(); ++i) {
                char c = text[i];
                if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
                if (c != name[i]) return false;
            }
            return true;
        };
        if (is("trigger")) return Control::Trigger;
        if (is("grip")) return Control::Grip;
        if (is("a") || is("x")) return Control::LowerFace;
        if (is("b") || is("y")) return Control::UpperFace;
        if (is("thumbstick")) return Control::StickClick;
        if (is("grip_click")) return Control::GripClick;
        if (is("axis3")) return Control::Axis3Click;
        if (is("axis4")) return Control::Axis4Click;
        if (is("pad_up")) return Control::PadUp;
        if (is("pad_down")) return Control::PadDown;
        if (is("pad_left")) return Control::PadLeft;
        if (is("pad_right")) return Control::PadRight;
        if (is("pad_center")) return Control::PadCenter;
        if (is("none")) return Control::None;
        return std::nullopt;
    }

    // "right trigger" -> "Right trigger": for a label standing on its own.
    [[nodiscard]] inline std::string capitalized(std::string text)
    {
        if (!text.empty() && text.front() >= 'a' && text.front() <= 'z')
            text.front() = static_cast<char>(text.front() - 'a' + 'A');
        return text;
    }
}
