#include <cstdint>
#include <iostream>

#include "SpawnPluginIdentity.h"

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

    void testFullPluginIdentity()
    {
        using namespace rpsui::settings_menu::spawn_plugin_identity;
        expect(pluginKeyFromFormId(0x69000F9Au) == 0x69u,
            "full-plugin keys come from the runtime FormID high byte");
        expect(pluginKeyFromFormId(0x00000007u) == 0x00u,
            "the base game remains load-order slot zero");
        expect(!isLightPluginKey(pluginKeyFromFormId(0xFD123456u)),
            "the last valid full-plugin slot is not classified as light");
    }

    void testLightPluginIdentity()
    {
        using namespace rpsui::settings_menu::spawn_plugin_identity;
        expect(pluginKeyFromFormId(0xFE0AB123u) == 0xFE0000ABu,
            "light-plugin keys retain the 12-bit small-file index");
        expect(pluginKeyFromFormId(0xFE0ABFFFu) == 0xFE0000ABu,
            "light-plugin local FormID bits do not alter plugin identity");
        expect(pluginKeyFromFormId(0xFEFFF001u) == 0xFE000FFFu,
            "the maximum light-plugin index remains representable");
        expect(isLightPluginKey(pluginKeyFromFormId(0xFE001001u)),
            "FE runtime FormIDs are classified as light-plugin keys");
    }

    void testRuntimeFormBoundaries()
    {
        using namespace rpsui::settings_menu::spawn_plugin_identity;
        expect(isRuntimeCreatedFormId(0xFF001234u),
            "FF runtime-created forms are excluded from plugin ownership");
        expect(!isRuntimeCreatedFormId(0xFEFFF123u),
            "light-plugin forms are not mistaken for runtime-created forms");
    }
}

int main()
{
    testFullPluginIdentity();
    testLightPluginIdentity();
    testRuntimeFormBoundaries();
    if (failures != 0) {
        std::cerr << failures << " spawn plugin identity assertion(s) failed.\n";
        return 1;
    }
    std::cout << "Configurator spawn plugin identity tests passed.\n";
    return 0;
}
