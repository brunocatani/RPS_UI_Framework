#pragma once
#include <array>
#include <string_view>
#include "RockSettingControlPolicy.h"

namespace rpsui::settings_menu::emitter_settings
{
    enum class Type { Toggle, Number, Color };
    struct Setting
    {
        const char* key;
        const char* label;
        Type type;
        double minimum{}, maximum{}, step{};
    };
    // Bounds match Emitters' Main-section parser; values always come from the
    // user's INI. Opening this page never replaces tuned appearance settings.
    inline constexpr std::array settings{
        Setting{"Enabled","Enable Emitters",Type::Toggle},
        Setting{"LaserEnabled","Enable laser",Type::Toggle},
        Setting{"WeaponEmitterControl","Attachment-controlled laser / flashlight",Type::Toggle},
        Setting{"FlashlightEnabled","Enable weapon flashlight",Type::Toggle},
        Setting{"FlashlightIntensity","Flashlight intensity",Type::Number,0.1,20,0.05},
        Setting{"CorrectedReticles","Correct sight aiming and parallax",Type::Toggle},
        Setting{"ReticleVisibilityAssist","Improve reticle visibility",Type::Toggle},
        Setting{"ReticleBrightness","Reticle brightness",Type::Number,0.25,16,0.05},
        Setting{"DotEnabled","Show laser impact dot",Type::Toggle},
        Setting{"HitLightEnabled","Illuminate laser impact surroundings",Type::Toggle},
        Setting{"DebugVisualizer","Show laser debug ray",Type::Toggle},
        Setting{"StereoDiagnostics","Log stereo sight diagnostics",Type::Toggle},
        Setting{"DotRadius","Laser dot radius",Type::Number,0.05,4,0.05},
        Setting{"DotEmissionScale","Laser dot emission at distance",Type::Number,1,128,0.25},
        Setting{"DotNearEmissionScale","Laser dot emission nearby",Type::Number,1,128,0.25},
        Setting{"DotSurfaceOffset","Laser dot surface offset",Type::Number,0.001,0.5,0.001},
        Setting{"HitLightIntensity","Laser spill intensity",Type::Number,0.05,10,0.05},
        Setting{"HitLightRadius","Laser spill radius",Type::Number,1,512,1},
        Setting{"LaserColor","Laser color",Type::Color},
        Setting{"HitLightSurfaceOffset","Laser spill surface offset",Type::Number,0.05,4,0.05}
    };
    inline const Setting* find(std::string_view key) noexcept
    {
        for (const auto& entry : settings)
            if (setting_control::equalsIgnoreCase(entry.key,key)) return &entry;
        return nullptr;
    }
}
