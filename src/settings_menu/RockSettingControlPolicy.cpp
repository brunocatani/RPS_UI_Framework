#include "RockSettingControlPolicy.h"
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
        struct NumericRange
        {
            std::string_view key;
            double minimum;
            double maximum;
        };

        constexpr auto kNumericRanges = std::to_array<NumericRange>({
            { "FiringGripProximitySupportRadius", 0.25, 30.0 },
            { "ForceGrabAttachSettleSeconds", 0.0, 1.0 },
            { "GrabEffectiveMotorMassFloor", 0.0, 100.0 },
            { "GrabHandLerpMinDistance", 0.0, 80.0 },
            { "GrabHandLerpTimeMin", 0.0, 1.0 },
            { "GrabHandReturnMinAngleDegrees", 0.0, 180.0 },
            { "GrabHandReturnMinDistance", 0.0, 80.0 },
            { "GrabHandReturnTimeMin", 0.0, 1.0 },
            { "GrabHapticBaseIntensity", 0.0, 1.0 },
            { "GrabHapticDurationSeconds", 0.0, 0.2 },
            { "GrabHapticMassExponent", 0.0, 2.0 },
            { "GrabHapticMassScale", 0.0, 1.0 },
            { "GrabHeldMassMovementFadeOutSeconds", 0.0, 60.0 },
            { "GrabHeldMassMovementMassExponent", 0.0, 4.0 },
            { "GrabHeldMassMovementMassProportion", 0.0, 10.0 },
            { "GrabHeldMassMovementMaxReduction", 0.0, 99.0 },
            { "GrabLooseWeaponSharedConstraintAngularDampingMultiplier", 0.05, 4.0 },
            { "GrabLooseWeaponSharedConstraintAngularForceMultiplier", 0.05, 8.0 },
            { "GrabLooseWeaponSharedConstraintAngularRecoveryMultiplier", 0.05, 4.0 },
            { "GrabLooseWeaponSharedConstraintAngularTauMultiplier", 0.05, 4.0 },
            { "GrabLooseWeaponSharedConstraintCollisionTauMultiplier", 0.05, 4.0 },
            { "GrabLooseWeaponSharedConstraintLinearDampingMultiplier", 0.05, 4.0 },
            { "GrabLooseWeaponSharedConstraintLinearRecoveryMultiplier", 0.05, 4.0 },
            { "GrabLooseWeaponSharedConstraintLinearTauMultiplier", 0.05, 4.0 },
            { "GrabLooseWeaponSharedConstraintMaxForceMultiplier", 0.05, 8.0 },
            { "fGrabLowContactSupportAngularScale", 0.05, 1.0 },
            { "fGrabMinAngularAuthorityScale", 0.05, 1.0 },
            { "GrabMinInertia", 0.0001, 100.0 },
            { "fGrabPhysicsRateForceScaleExponent", 0.0, 2.0 },
            { "fGrabPhysicsRateMinForceScale", 0.1, 2.0 },
            { "fGrabPhysicsRateReferenceHz", 1.0, 240.0 },
            { "GrabPinchDetectionAxisBlend", 0.0, 1.0 },
            { "GrabPinchMaxFingerGapGameUnits", 0.1, 80.0 },
            { "GrabPinchMaxPocketDistanceGameUnits", 0.1, 80.0 },
            { "GrabPinchMaxVolumeCubicGameUnits", 0.001, 1000000.0 },
            { "GrabPinchMinFingerGapGameUnits", 0.0, 40.0 },
            { "GrabPinchOtherFingerCurlValue", 0.0, 1.0 },
            { "GrabPinchThumbIndexMaxOpenValue", 0.0, 1.0 },
            { "fGrabPositionOnlyAngularScale", 0.05, 1.0 },
            { "fGrabSmallObjectAngularScale", 0.05, 1.0 },
            { "fGrabSmallObjectReferenceLeverGameUnits", 1.0, 120.0 },
            { "fGrabWeakPivotTwistScale", 0.0, 1.0 },
            { "HeldImpactHapticBaseIntensity", 0.0, 1.0 },
            { "HeldImpactHapticCooldownSeconds", 0.0, 1.0 },
            { "HeldImpactHapticDampedMultiplier", 0.0, 1.0 },
            { "HeldImpactHapticDurationSeconds", 0.0, 0.2 },
            { "HeldImpactHapticMassExponent", 0.0, 2.0 },
            { "HeldImpactHapticMassScale", 0.0, 1.0 },
            { "HeldImpactHapticMinSpeedGameUnits", 0.0, 1000.0 },
            { "HeldImpactHapticSpeedScale", 0.0, 1.0 },
            { "ManualScopeHoldSeconds", 0.05, 2.0 },
            { "MouthConsumeCandidateHapticIntervalSeconds", 0.0, 2.0 },
            { "MouthConsumeCommitHapticDurationSeconds", 0.0, 0.2 },
            { "MouthConsumeCommitHapticIntensity", 0.0, 1.0 },
            { "MouthConsumeEnterPaddingGameUnits", 0.0, 40.0 },
            { "MouthConsumeExitPaddingGameUnits", 0.0, 60.0 },
            { "MouthConsumeMaxSpeedGameUnitsPerSecond", 0.0, 1000.0 },
            { "MouthConsumeMinDwellSeconds", 0.0, 1.0 },
            { "MouthConsumeRadiusGameUnits", 1.0, 80.0 },
            { "NativeScopeFiringGripFallbackOffsetGameUnits", -100.0, 100.0 },
            { "NativeScopeFiringGripFallbackRotationDegrees", -180.0, 180.0 },
            { "NativeScopeOverlayOffsetGameUnits", -100.0, 100.0 },
            { "NativeScopeOverlayRotationDegrees", -180.0, 180.0 },
            { "PullCatchHapticIntensity", 0.0, 1.0 },
            { "PullStartHapticIntensity", 0.0, 1.0 },
            { "RealisticGrenadeFuseSeconds", 0.0, 30.0 },
            { "SelectionLockHapticIntensity", 0.0, 1.0 },
            { "SelectionLockReleaseHapticDurationSeconds", 0.0, 0.2 },
            { "SelectionLockReleaseHapticIntensity", 0.0, 1.0 },
            { "ShoulderStashCandidateHapticIntervalSeconds", 0.0, 2.0 },
            { "ShoulderStashCommitHapticIntensity", 0.0, 1.0 },
            { "ShoulderStashEnterPaddingGameUnits", 0.0, 40.0 },
            { "ShoulderStashExitPaddingGameUnits", 0.0, 60.0 },
            { "ShoulderStashHmdBackRadiusGameUnits", 1.0, 80.0 },
            { "fShoulderStashMaxSpeedGameUnitsPerSecond", 0.0, 1000.0 },
            { "fShoulderStashMinDwellSeconds", 0.0, 1.0 },
            { "SurfaceGrabHapticDurationSeconds", 0.0, 0.2 },
            { "SurfaceGrabHapticIntensity", 0.0, 1.0 },
            { "WeaponInteractionTouchRadius", 0.25, 6.0 },
            { "WeaponSupportGripHandLerpMinDistance", 0.0, 80.0 },
            { "WeaponSupportGripHandLerpTimeMin", 0.0, 1.0 },
        });

        constexpr auto kIntegerRanges = std::to_array<NumericRange>({
            { "HavokTimingFixMaxSubsteps", 1.0, 6.0 },
            { "SurfaceMeshGrabMaxTriangles", 256.0, 100000.0 },
            { "iSurfaceMeshGrabMaxPatchTriangles", 64.0, 2048.0 },
            { "iGrabMinFingerContactGroups", 1.0, 5.0 },
            { "iGrabContactPatchProbeCount", 1.0, 9.0 },
            { "DebugMaxHandBoneBodiesDrawn", 0.0, 48.0 },
            { "DebugMaxBodyBoneBodiesDrawn", 0.0, 64.0 },
            { "DebugMaxShapeCapturesPerFrame", 0.0, 32.0 },
            { "DebugMaxConvexSupportVertices", 1.0, 32.0 },
            { "DebugMaxCompoundChildren", 1.0, 1024.0 },
            { "DebugMaxCompoundDepth", 1.0, 8.0 },
            { "DebugMaxShapeQueuedJobs", 0.0, 256.0 },
            { "DebugMaxShapeCompletedJobs", 0.0, 256.0 },
            { "DebugMaxShapeUploadsPerFrame", 0.0, 16.0 },
            { "DebugMaxShapeCacheEntries", 16.0, 4096.0 },
            { "DebugMaxShapeCacheBytes", 1048576.0, 536870912.0 },
            { "DebugMaxBodyInstances", 0.0, 171.0 },
            { "DebugMaxLineVertices", 0.0, 65536.0 },
            { "iDebugMaxTextVertices", 0.0, 131072.0 },
            { "DebugMaxSkeletonBonesDrawn", 0.0, 768.0 },
            { "DebugMaxSkeletonBoneAxesDrawn", 0.0, 768.0 },
            { "LogSampleMilliseconds", 0.0, 60000.0 },
            { "PerformanceProfilerLogIntervalFrames", 30.0, 54000.0 },
            { "PerformanceProfilerWarmupFrames", 0.0, 54000.0 },
            { "iRightWeaponReadyButtonID", 0.0, 64.0 },
            { "iWeaponCollisionGroupingMode", 0.0, 3.0 },
            { "iWeaponCollisionVisualStabilizationFrames", 0.0, 60.0 },
            { "iWeaponCollisionSupportFitTargetPoints", 4.0, 252.0 },
            { "iSoftContactVisualPriority", 0.0, 255.0 },
        });

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

        [[nodiscard]] Spec dropdown(std::initializer_list<Option> options)
        {
            Spec spec{};
            spec.kind = Kind::Dropdown;
            spec.options.assign(options.begin(), options.end());
            return spec;
        }

        [[nodiscard]] std::vector<Option> describedIntegerOptions(
            std::string_view description,
            std::string_view currentValue)
        {
            std::vector<Option> options;
            std::size_t cursor = 0;
            while (cursor < description.size()) {
                const bool negative = description[cursor] == '-' &&
                    cursor + 1 < description.size() &&
                    std::isdigit(static_cast<unsigned char>(description[cursor + 1]));
                const bool digit = std::isdigit(static_cast<unsigned char>(description[cursor]));
                const bool boundary = cursor == 0 ||
                    std::isspace(static_cast<unsigned char>(description[cursor - 1])) ||
                    description[cursor - 1] == ',' || description[cursor - 1] == ';' ||
                    description[cursor - 1] == ':' || description[cursor - 1] == '.';
                if ((!digit && !negative) || !boundary) {
                    ++cursor;
                    continue;
                }

                const std::size_t valueBegin = cursor;
                cursor += negative ? 1 : 0;
                while (cursor < description.size() &&
                       std::isdigit(static_cast<unsigned char>(description[cursor]))) {
                    ++cursor;
                }
                const std::size_t valueEnd = cursor;
                while (cursor < description.size() &&
                       std::isspace(static_cast<unsigned char>(description[cursor]))) {
                    ++cursor;
                }
                if (cursor >= description.size() || description[cursor] != '=') {
                    continue;
                }
                ++cursor;
                while (cursor < description.size() &&
                       std::isspace(static_cast<unsigned char>(description[cursor]))) {
                    ++cursor;
                }
                const std::size_t labelBegin = cursor;
                while (cursor < description.size() && description[cursor] != ',' &&
                       description[cursor] != ';' && description[cursor] != '.') {
                    ++cursor;
                }
                std::string value(description.substr(valueBegin, valueEnd - valueBegin));
                std::string label(description.substr(labelBegin, cursor - labelBegin));
                while (!label.empty() &&
                       std::isspace(static_cast<unsigned char>(label.back()))) {
                    label.pop_back();
                }
                if (!label.empty() && std::none_of(
                        options.begin(), options.end(), [&](const Option& option) {
                            return option.value == value;
                        })) {
                    options.push_back({ value, value + " — " + label });
                }
            }
            if (options.size() < 2 || std::none_of(
                    options.begin(), options.end(), [&](const Option& option) {
                        return option.value == currentValue;
                    })) {
                options.clear();
            }
            return options;
        }

        [[nodiscard]] std::optional<Spec> exactDropdown(
            std::string_view section,
            std::string_view key,
            std::string_view currentValue,
            std::string_view description)
        {
            if (key == "LogLevel") {
                return dropdown({
                    { "0", "0 — Trace" },
                    { "1", "1 — Debug" },
                    { "2", "2 — Info" },
                    { "3", "3 — Warn" },
                    { "4", "4 — Error" },
                    { "5", "5 — Critical" },
                    { "6", "6 — Off" },
                });
            }
            if (key == "WeaponGrabMode") {
                return dropdown({
                    { "1", "1 — Toggle both grips" },
                    { "2", "2 — Toggle firing grip only" },
                    { "3", "3 — Hold both grips (full immersive)" },
                });
            }
            if (key == "HighlightIntensityMode") {
                return dropdown({
                    { "1", "1 — 40%" },
                    { "2", "2 — 60%" },
                    { "3", "3 — 80%" },
                    { "4", "4 — 100%" },
                });
            }
            if (key == "HighlightColor") {
                return dropdown({
                    { "red", "Red" },
                    { "blue", "Blue" },
                    { "orange", "Orange" },
                    { "white", "White" },
                });
            }
            if (key == "DefaultHand") {
                return dropdown({
                    { "right", "Right" },
                    { "left", "Left" },
                });
            }
            // ROCK button layout: the physical input behind each ROCK button.
            if (section == "ControllerInput.Left" || section == "ControllerInput.Right") {
                const bool left = section == "ControllerInput.Left";
                return dropdown({
                    { "trigger", "Trigger" },
                    { "grip", "Grip" },
                    { left ? "x" : "a", left ? "X (lower face)" : "A (lower face)" },
                    { left ? "y" : "b", left ? "Y (upper face)" : "B (upper face)" },
                    { "thumbstick", "Stick / trackpad click" },
                    { "grip_click", "Grip click (Axis2)" },
                    { "axis3", "Axis3 click (bumper)" },
                    { "axis4", "Axis4 click" },
                    { "pad_up", "Trackpad up" },
                    { "pad_down", "Trackpad down" },
                    { "pad_left", "Trackpad left" },
                    { "pad_right", "Trackpad right" },
                    { "pad_center", "Trackpad centre" },
                    { "none", "None" },
                });
            }
            if (key == "DebugSkeletonBoneMode") {
                return dropdown({
                    { "0", "Mode 0" },
                    { "1", "Mode 1" },
                    { "2", "Mode 2" },
                    { "3", "Mode 3" },
                });
            }
            if (key == "DebugSkeletonBoneSource") {
                return dropdown({
                    { "1", "Source 1" },
                    { "2", "Source 2" },
                });
            }
            auto described = describedIntegerOptions(description, currentValue);
            if (!described.empty()) {
                Spec spec{};
                spec.kind = Kind::Dropdown;
                spec.options = std::move(described);
                return spec;
            }
            return std::nullopt;
        }

        [[nodiscard]] std::optional<NumericRange> exactRange(
            ValueType type,
            std::string_view key) noexcept
        {
            if (type == ValueType::Integer) {
                const auto found = std::find_if(
                    kIntegerRanges.begin(), kIntegerRanges.end(), [&](const auto& range) {
                        return range.key == key;
                    });
                return found == kIntegerRanges.end() ?
                    std::nullopt : std::optional<NumericRange>(*found);
            }
            const auto found = std::find_if(
                kNumericRanges.begin(), kNumericRanges.end(), [&](const auto& range) {
                    return range.key == key;
                });
            return found == kNumericRanges.end() ?
                std::nullopt : std::optional<NumericRange>(*found);
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
        std::string_view description,
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
        if(mod==RpsMod::Diamond)if(const auto* setting=diamond::configuration::find(key)) {
            if(setting->boolean)return {.kind=Kind::Checkbox};
            return {.kind=Kind::Numeric,.bounded=true,.minimum=setting->minimum,.maximum=setting->maximum,.step=setting->step};
        }
        if (mod != RpsMod::Rock && mod != RpsMod::RockDeveloper) {
            if (type == ValueType::Boolean) return { .kind = Kind::Checkbox };
            if (mod == RpsMod::Paper && key == "MaximumMode")
                return { .kind = Kind::Dropdown, .options = {{"User","User"},{"Observe","Observe"},{"Harvest","Harvest"},{"Capture","Capture"}} };
            if (mod == RpsMod::Paper && key == "Access")
                return { .kind = Kind::Dropdown, .options = {{"Off","Off"},{"ReadOnly","Read only"},{"ReadWrite","Read and write"}} };
            if (type == ValueType::String) return { .kind = Kind::Text };
            // ROCK's keyed ranges and naming heuristics are not other mods' contracts.
            return { .kind = Kind::Numeric, .step = fineStep(type, key, parseNumber(value), 0.0) };
        }
        if (type == ValueType::Boolean) {
            return { .kind = Kind::Checkbox };
        }
        if (type == ValueType::String) {
            if (const auto exact = exactDropdown(section, key, value, description)) {
                return *exact;
            }
            return { .kind = Kind::Text };
        }
        if (type == ValueType::Integer) {
            if (const auto exact = exactDropdown(section, key, value, description)) {
                return *exact;
            }
        }

        const double current = parseNumber(value);
        if (const auto range = exactRange(type, key)) {
            const double span = range->maximum - range->minimum;
            return {
                .kind = Kind::Numeric,
                .bounded = true,
                .minimum = range->minimum,
                .maximum = range->maximum,
                .step = fineStep(type, key, current, span),
            };
        }

        if (type == ValueType::Float &&
            (contains(key, "Intensity") || contains(key, "Alpha") ||
             contains(key, "Blend") || contains(key, "Weight") ||
             contains(key, "Strength")) &&
            current >= 0.0 && current <= 1.0) {
            return {
                .kind = Kind::Numeric,
                .bounded = true,
                .minimum = 0.0,
                .maximum = 1.0,
                .step = 0.01,
            };
        }
        if (type == ValueType::Float && contains(key, "MaxOpenValue")) {
            return {
                .kind = Kind::Numeric,
                .bounded = true,
                .minimum = 1.0,
                .maximum = 2.0,
                .step = 0.01,
            };
        }
        if (type == ValueType::Float && contains(key, "Degrees")) {
            const bool signedAngle = current < 0.0 || contains(key, "Yaw") ||
                contains(key, "Pitch") || contains(key, "Roll") || contains(key, "Offset");
            return {
                .kind = Kind::Numeric,
                .bounded = true,
                .minimum = signedAngle ? -180.0 : 0.0,
                .maximum = 180.0,
                .step = 0.5,
            };
        }

        return {
            .kind = Kind::Numeric,
            .bounded = false,
            .step = fineStep(type, key, current, 0.0),
        };
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
