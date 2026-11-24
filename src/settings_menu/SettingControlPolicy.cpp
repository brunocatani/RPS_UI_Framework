#include "SettingControlPolicy.h"
#include <DIAMOND/Configuration.h>
#include <SCISSORS/Configuration.h>
#include "EmitterSettings.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <iomanip>
#include <initializer_list>
#include <optional>
#include <sstream>

namespace rpsui::settings_menu::setting_control
{
    namespace
    {
        [[nodiscard]] bool contains(std::string_view text, std::string_view token) noexcept
        {
            return text.find(token) != std::string_view::npos;
        }

        [[nodiscard]] double parseNumber(std::string_view value) noexcept
        {
            std::string owned(value);
            char* end = nullptr;
            const double parsed = std::strtod(owned.c_str(), &end);
            return end && *end == '\0' && std::isfinite(parsed) ? parsed : 0.0;
        }

        [[nodiscard]] double fineStep(
            ValueType type,
            std::string_view key,
            double current,
            double span) noexcept
        {
            if (type == ValueType::Integer) {
                if (contains(key, "CacheBytes")) {
                    return 1048576.0;
                }
                if (contains(key, "Triangles") && span > 10000.0) {
                    return 64.0;
                }
                return 1.0;
            }
            if (contains(key, "Seconds") || contains(key, "Intensity") ||
                contains(key, "Alpha") || contains(key, "Blend") ||
                contains(key, "Weight") || contains(key, "Strength") ||
                contains(key, "Multiplier") || contains(key, "Scale") ||
                contains(key, "Exponent") || std::fabs(current) < 0.1) {
                return std::fabs(current) < 0.01 ? 0.001 : 0.01;
            }
            if (contains(key, "Degrees")) {
                return 0.5;
            }
            if (contains(key, "GameUnits")) {
                return span <= 20.0 ? 0.1 : 0.25;
            }
            if (span <= 5.0 || std::fabs(current) < 10.0) {
                return 0.05;
            }
            if (span <= 100.0 || std::fabs(current) < 100.0) {
                return 0.5;
            }
            if (span <= 1000.0 || std::fabs(current) < 1000.0) {
                return 1.0;
            }
            return 10.0;
        }

    }

    bool equalsIgnoreCase(std::string_view left, std::string_view right) noexcept
    {
        if (left.size() != right.size()) {
            return false;
        }
        for (std::size_t index = 0; index < left.size(); ++index) {
            char a = left[index];
            char b = right[index];
            if (a >= 'A' && a <= 'Z') {
                a = static_cast<char>(a - 'A' + 'a');
            }
            if (b >= 'A' && b <= 'Z') {
                b = static_cast<char>(b - 'A' + 'a');
            }
            if (a != b) {
                return false;
            }
        }
        return true;
    }

    std::string formatFloat(double value, double step)
    {
        std::ostringstream stream;
        stream << std::fixed << std::setprecision(step < 0.05 ? 3 : (step < 1.0 ? 2 : 1)) << value;
        auto result = stream.str();
        while (result.size() > 3 && result.back() == '0') {
            result.pop_back();
        }
        if (!result.empty() && result.back() == '.') {
            result.push_back('0');
        }
        return result;
    }

    Spec build(
        ValueType type,
        std::string_view key,
        std::string_view value,
        RpsMod mod,
        std::string_view section)
    {
        if(isScissorsConfiguration(mod))for(const auto& number:scissors::configuration::kNumbers)if(key==number.key)
            return {.kind=Kind::Numeric,.bounded=true,.minimum=number.minimum,.maximum=number.maximum,.step=number.step};
        if(mod==RpsMod::Emitters)if(const auto* setting=emitter_settings::find(key)) {
            if(setting->type==emitter_settings::Type::Toggle)return {.kind=Kind::Checkbox};
            if(setting->type==emitter_settings::Type::Color)
                return {.kind=Kind::Dropdown,.options={{"Red","Red"},{"Green","Green"},{"Purple","Purple"}}};
            return {.kind=Kind::Numeric,.bounded=true,.minimum=setting->minimum,.maximum=setting->maximum,.step=setting->step};
        }
        // RPS_UI.toml: the range RPS UI clamps Sharpness to.
        if(mod==RpsMod::RpsUi && key=="Sharpness")
            return {.kind=Kind::Numeric,.bounded=true,.minimum=1.0,.maximum=3.0,.step=0.25};
        if(mod==RpsMod::RpsUi && key=="HapticStrength")
            return {.kind=Kind::Numeric,.bounded=true,.minimum=0.0,.maximum=1.0,.step=0.1};
        if(mod==RpsMod::RpsUi && key=="GripMoveSeconds")
            return {.kind=Kind::Numeric,.bounded=true,.minimum=0.2,.maximum=1.5,.step=0.05};
        if(mod==RpsMod::Diamond)if(const auto* setting=diamond::configuration::find(section,key)) {
            using DiamondType=diamond::configuration::Type;
            if(setting->type==DiamondType::Toggle)return {.kind=Kind::Checkbox};
            if(setting->type==DiamondType::Choice) {
                Spec spec{.kind=Kind::Dropdown};
                for(std::uint32_t i=0;i<setting->choiceCount;++i)spec.options.push_back({setting->choices[i],setting->choices[i]});
                return spec;
            }
            return {.kind=Kind::Numeric,.bounded=true,.minimum=setting->minimum,.maximum=setting->maximum,.step=setting->step};
        }
        if (type == ValueType::Boolean) return { .kind = Kind::Checkbox };
        if (mod == RpsMod::Paper && key == "MaximumMode")
            return { .kind = Kind::Dropdown, .options = {{"User","User"},{"Observe","Observe"},{"Harvest","Harvest"},{"Capture","Capture"}} };
        if (mod == RpsMod::Paper && key == "Access")
            return { .kind = Kind::Dropdown, .options = {{"Off","Off"},{"ReadOnly","Read only"},{"ReadWrite","Read and write"}} };
        if (type == ValueType::String) return { .kind = Kind::Text };
        return { .kind = Kind::Numeric, .step = fineStep(type, key, parseNumber(value), 0.0) };
    }

    double snapNumeric(const Spec& spec, double value) noexcept
    {
        if (!std::isfinite(value)) {
            return spec.bounded ? spec.minimum : 0.0;
        }
        const double step = std::isfinite(spec.step) && spec.step > 0.0 ? spec.step : 1.0;
        value = std::round(value / step) * step;
        if (spec.bounded && std::isfinite(spec.minimum) && std::isfinite(spec.maximum) &&
            spec.minimum <= spec.maximum) {
            value = std::clamp(value, spec.minimum, spec.maximum);
        }
        return value;
    }
}
