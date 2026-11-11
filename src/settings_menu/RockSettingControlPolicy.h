#pragma once
#include "RpsMod.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace rpsui::settings_menu::setting_control
{
    enum class ValueType : std::uint8_t
    {
        Boolean,
        Integer,
        Float,
        String,
    };

    enum class Kind : std::uint8_t
    {
        Checkbox,
        Numeric,
        Dropdown,
        Text,
    };

    struct Option
    {
        std::string value;
        std::string label;
    };

    struct Spec
    {
        Kind kind{ Kind::Text };
        bool bounded{ false };
        double minimum{ 0.0 };
        double maximum{ 0.0 };
        double step{ 1.0 };
        std::vector<Option> options;
    };

    [[nodiscard]] Spec build(
        ValueType type,
        std::string_view key,
        std::string_view value,
        std::string_view description,
        RpsMod mod = RpsMod::Rock,
        std::string_view section = {});

    [[nodiscard]] double snapNumeric(const Spec& spec, double value) noexcept;
    [[nodiscard]] std::string formatFloat(double value, double step);
    [[nodiscard]] bool equalsIgnoreCase(std::string_view left, std::string_view right) noexcept;
}
