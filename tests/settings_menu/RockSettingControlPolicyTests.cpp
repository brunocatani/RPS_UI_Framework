#include "RockSettingControlPolicy.h"

#include <cmath>
#include <iostream>

namespace
{
    int failures = 0;

    void expect(bool condition, const char* message)
    {
        if (!condition) {
            std::cerr << "FAIL: " << message << '\n';
            ++failures;
        }
    }

    [[nodiscard]] bool near(double left, double right) noexcept
    {
        return std::fabs(left - right) < 0.00001;
    }
}

int main()
{
    using namespace rpsui::settings_menu::setting_control;

    for (const auto value : {"1", "2", "3"}) {
        const auto grabMode = build(ValueType::Integer, "WeaponGrabMode", value, "");
        expect(grabMode.kind == Kind::Dropdown && grabMode.options.size() == 3,
            "weapon grab mode has three selectable modes");
        if (grabMode.options.size() == 3) {
            expect(grabMode.options[0].value == "1" && grabMode.options[1].value == "2" && grabMode.options[2].value == "3",
                "weapon grab mode dropdown writes the correct ROCK values");
        }
    }

    const auto boolean = build(ValueType::Boolean, "GrabEnabled", "true", "");
    expect(boolean.kind == Kind::Checkbox,
        "every ROCK boolean is presented as a checkbox");

    const auto bounded = build(
        ValueType::Float, "ManualScopeHoldSeconds", "0.30", "");
    expect(bounded.kind == Kind::Numeric && bounded.bounded &&
           near(bounded.minimum, 0.05) && near(bounded.maximum, 2.0),
        "audited ROCK bounds produce a native slider");

    const auto unbounded = build(
        ValueType::Float, "ExperimentalRecovery", "2.5", "");
    expect(unbounded.kind == Kind::Numeric && !unbounded.bounded &&
           unbounded.step > 0.0,
        "numeric values without authoritative bounds remain draggable and fine-stepped");

    const auto normalized = build(
        ValueType::Float, "ContactStrength", "0.75", "");
    expect(normalized.kind == Kind::Numeric && normalized.bounded &&
           near(normalized.minimum, 0.0) && near(normalized.maximum, 1.0),
        "normalized strength values use a zero-to-one slider");

    const auto angle = build(
        ValueType::Float, "CalibrationYawDegrees", "0.0", "");
    expect(angle.bounded && near(angle.minimum, -180.0) && near(angle.maximum, 180.0),
        "signed angle controls expose the full rotation range");

    const auto described = build(
        ValueType::Integer,
        "GrabContactQualityMode",
        "1",
        "0 = permissive, 1 = hybrid patch and finger evidence, 2 = strict grouped-finger debug.");
    expect(described.kind == Kind::Dropdown && described.options.size() == 3 &&
           described.options[1].value == "1" &&
           described.options[1].label.find("hybrid") != std::string::npos,
        "complete integer enumerations in ROCK comments become labeled dropdowns");

    const auto incomplete = build(
        ValueType::Integer,
        "FutureMode",
        "3",
        "0 = minimal, 1 = standard, higher modes may be supported.");
    expect(incomplete.kind == Kind::Numeric,
        "an incomplete description cannot hide the current numeric mode in a dropdown");

    const auto color = build(
        ValueType::String, "HighlightColor", "blue", "");
    expect(color.kind == Kind::Dropdown && color.options.size() == 4,
        "the finite ROCK highlight palette is a dropdown");

    const auto freeText = build(
        ValueType::String, "LogPattern", "%Y [%l] %v", "");
    expect(freeText.kind == Kind::Text,
        "genuinely free-form ROCK strings remain text controls");

    // ROCK's button layout lives in the ControllerInput.Left/Right tables.
    using rpsui::settings_menu::RpsMod;
    const auto leftButton = build(ValueType::String, "Trigger", "trigger", "", RpsMod::Rock, "ControllerInput.Left");
    const auto rightButton = build(ValueType::String, "A", "a", "", RpsMod::Rock, "ControllerInput.Right");
    expect(leftButton.kind == Kind::Dropdown && rightButton.kind == Kind::Dropdown &&
           leftButton.options.size() > 2 && leftButton.options[2].value == "x" && rightButton.options[2].value == "a",
        "button layout rows are per-hand dropdowns");
    expect(build(ValueType::String, "Trigger", "trigger", "", RpsMod::Rock, "Grabbing").kind == Kind::Text,
        "only the ControllerInput tables are button layouts");

    expect(near(snapNumeric(bounded, 0.333), 0.33),
        "slider commits snap to their fine-tuning step");
    expect(near(snapNumeric(bounded, 50.0), 2.0),
        "bounded slider commits clamp to the audited maximum");

    if (failures != 0) {
        std::cerr << failures << " ROCK-setting control policy assertion(s) failed.\n";
        return 1;
    }
    std::cout << "DevUI ROCK-setting control policy tests passed.\n";
    return 0;
}
