#include "IniSettingsStore.h"
#include <array>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace
{
    using namespace rock::api::configuration;
    using rock::api::Status;
    std::uint64_t revision = 1;
    std::string configured = "false";
    std::string offset = "[0.5, -1.0, 0.0]";
    std::string written;
    std::string writtenKey;
    bool refuseWrite = false;
    Status visitStatus = Status::Ok;
    Status currentRevision(rock::api::OwnerToken owner,std::uint64_t* out) noexcept { if(owner!=1)return Status::OwnerNotRegistered; *out=revision; return Status::Ok; }
    Status visit(rock::api::OwnerToken owner,Group group, Visitor callback, void* context) noexcept
    {
        if(owner!=1)return Status::OwnerNotRegistered;
        if(visitStatus!=Status::Ok)return visitStatus;
        if (group == Group::Consumer) {
            const Setting logging{"Logging", "LogLevel", "6", "6",
                "01. Logging", "Log detail", ValueType::Integer, 0};
            const Setting weapon{"WeaponHandling", "BipodMode", "true", "true",
                "04. Weapon Handling", "Surface latch", ValueType::Boolean, 0};
            callback(&logging, context);
            callback(&weapon, context);
            return Status::Ok;
        }
        const Setting toggle{"DeveloperControls", "DeveloperModeEnabled", configured.c_str(), "false",
            "Developer", "Enables developer controls", ValueType::Boolean, configured == "false" ? 0u : 1u};
        const Setting number{"SkeletonDebug", "DebugSkeletonBoneAxisLength", "4.0", "4.0",
            "Developer", "Axis length", ValueType::Float, 0};
        const Setting vector{"PalmAndGrabOffsets", "RightGrabAuthorityProxyOffsetGameUnits", offset.c_str(),
            "[0.5, -1.0, 0.0]", "Developer", "Proxy offset", ValueType::Float3, 0};
        callback(&toggle, context);
        callback(&number, context);
        callback(&vector, context);
        return Status::Ok;
    }
    Status set(rock::api::OwnerToken owner,Group group, const char* section, const char* key, const char* value, char* error, std::uint32_t capacity) noexcept
    {
        if(owner!=1)return Status::OwnerNotRegistered;
        if (refuseWrite) {
            if (capacity > 14) std::strcpy(error, "write refused");
            return Status::NotReady;
        }
        if (group != Group::Developer) return Status::NotReady;
        const std::string path = std::string(section) + "." + key;
        if (path != "DeveloperControls.DeveloperModeEnabled" &&
            path != "PalmAndGrabOffsets.RightGrabAuthorityProxyOffsetGameUnits") return Status::NotReady;
        written = value;
        writtenKey = path;
        return Status::Ok;
    }
    void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
}

int main()
{
    using namespace rpsui::settings_menu;
    try {
        const rock::api::configuration::Api api{currentRevision, visit, set};
        const auto absent = std::filesystem::temp_directory_path() / "ROCK-absent-developer-bridge.toml";
        require(!std::filesystem::exists(absent), "fixture path must not exist");
        IniSettingsStore store(absent, RpsMod::RockDeveloper, &api, {}, 1);
        require(store.load(), "developer controls unavailable without a file");
        require(store.settings().size() == 5, "compiled defaults were not exposed");
        require(store.settings()[1].type == SettingType::Float, "whole-number float default changed control type");
        require(!store.settings()[0].overridden && store.settings()[0].defaultValue == "false", "default metadata missing");
        require(!store.needsReload(), "new snapshot is unexpectedly stale");

        // A Float3 setting is three numeric rows, X/Y/Z, that write the whole list.
        for (std::size_t component = 0; component < 3; ++component) {
            const auto& row = store.settings()[2 + component];
            require(row.type == SettingType::Float && row.vectorComponent == component &&
                    row.key == "RightGrabAuthorityProxyOffsetGameUnits" && !row.overridden,
                "a Float3 setting was not split into component rows");
        }
        require(store.settings()[2].componentLabel == "X" && store.settings()[4].componentLabel == "Z" &&
                store.settings()[3].value == "-1.0" && store.settings()[3].id != store.settings()[4].id,
            "Float3 rows lost their component identity or value");
        auto result = store.setNumericByIndex(3, 2.5);
        require(result.saved && writtenKey == "PalmAndGrabOffsets.RightGrabAuthorityProxyOffsetGameUnits" &&
                written.starts_with("[0.5, 2.5") && written.ends_with(", 0.0]"),
            "a Float3 row did not write the whole list with its siblings");
        offset = written; ++revision;
        require(store.reload() && store.settings()[3].value == "2.5" && store.settings()[3].overridden &&
                !store.settings()[2].overridden,
            "a Float3 change did not refresh per component");

        result = store.setBooleanByIndex(0, true);
        require(result.saved && written == "true", "menu did not route developer change to ROCK");
        require(!std::filesystem::exists(absent), "menu wrote a second independent settings copy");
        configured = "true"; ++revision;
        require(store.needsReload(), "provider revision did not request refresh");
        require(store.reload(), "provider refresh failed");
        require(store.settings()[0].value == "true" && store.settings()[0].overridden, "effective override did not refresh");
        refuseWrite = true;
        result = store.setBooleanByIndex(0, false);
        require(!result.saved && store.settings()[0].value == "true", "failed write changed the menu state");
        require(store.lastProviderStatus()==Status::NotReady, "write failure discarded provider status");
        refuseWrite = false;
        result = store.setBooleanByIndex(0, false);
        require(result.saved && written == "false", "restoring default was not delegated to ROCK");
        configured = "false"; ++revision;
        require(store.needsReload() && store.reload(), "external removal did not refresh");
        require(!store.settings()[0].overridden, "default still displayed as overridden");
        for(const auto failure:{Status::WrongThread,Status::OwnerNotRegistered,Status::NotReady}) {
            visitStatus=failure;
            require(!store.reload() && store.settings().empty(), "rejected catalog kept editable stale settings");
            require(store.lastProviderStatus()==failure, "catalog failure discarded provider status");
        }
        visitStatus=Status::Ok;
        require(store.reload() && store.lastProviderStatus()==Status::Ok, "catalog did not recover after provider rejection");
        struct Cleanup { std::filesystem::path path; ~Cleanup() { std::error_code error; std::filesystem::remove(path, error); } } cleanup{absent};
        { std::ofstream file(absent); file << "[WeaponHandling]\n## 99. Old weapon section\nBipodMode = true\n[Logging]\n## Old help\nLogLevel = 6\n"; }
        IniSettingsStore consumer(absent, RpsMod::Rock, &api, {}, 1);
        require(consumer.load(), "consumer catalog could not load");
        require(consumer.settings()[0].key == "LogLevel" && consumer.settings()[1].key == "BipodMode",
            "consumer menu used disk order instead of the provider catalog");
        require(consumer.settings()[0].category == "01. Logging" && consumer.settings()[1].category == "04. Weapon Handling" &&
            consumer.settings()[0].description == "Log detail", "old file comments replaced compiled section labels or help");
        std::cout << "Settings menu ROCK configuration bridge checks passed.\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
