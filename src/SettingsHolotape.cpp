#include "PCH.h"

#include "SettingsHolotape.h"

#include "Logger.h"

namespace rpsui::settings_holotape
{
    namespace
    {
        constexpr std::string_view kPluginFile = "RPS_UI_Framework.esp";
        constexpr RE::TESFormID kHolotapeLocalId = 0x800;

        // FO4VR 1.2.72: push rbx; sub rsp,20h; mov rbx,rcx; call <equip event source>.
        // The source it returns is the one ActorEquipManager's sender (RVA 0x41E0F0) fills
        // with the TESEquipEvent record (actor, base form, original ref, unique ID, equipped).
        constexpr std::uintptr_t kRegisterEquipSinkRva = 0x41E090;
        constexpr std::array<std::uint8_t, 10> kRegisterEquipSink{ 0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B, 0xD9, 0xE8 };

        std::atomic<RE::TESFormID> s_holotape{ 0 };
        std::atomic_bool s_played{ false };

        [[nodiscard]] bool bytesMatch(std::uintptr_t rva, const std::uint8_t* expected, std::size_t count) noexcept
        {
            __try {
                return std::memcmp(reinterpret_cast<const void*>(REL::Offset(rva).address()), expected, count) == 0;
            } __except (EXCEPTION_EXECUTE_HANDLER) {
                return false;
            }
        }

        // Any thread the engine equips on: only atomics here.
        struct EquipSink final : RE::BSTEventSink<RE::TESEquipEvent>
        {
            RE::BSEventNotifyControl ProcessEvent(const RE::TESEquipEvent& event, RE::BSTEventSource<RE::TESEquipEvent>*) override
            {
                const auto holotape = s_holotape.load(std::memory_order_acquire);
                if (holotape != 0 && event.equipped && event.baseObject == holotape &&
                    event.actor.get() == RE::PlayerCharacter::GetSingleton()) {
                    s_played.store(true, std::memory_order_release);
                }
                return RE::BSEventNotifyControl::kContinue;
            }
        };

        EquipSink& sink() noexcept
        {
            static EquipSink value;
            return value;
        }

        [[nodiscard]] RE::TESBoundObject* holotape() noexcept
        {
            const auto id = s_holotape.load(std::memory_order_acquire);
            auto* form = id ? RE::TESForm::GetFormByID(id) : nullptr;
            return form ? form->As<RE::TESBoundObject>() : nullptr;
        }
    }

    void onGameDataReady() noexcept
    {
        static bool registered = false;
        if (registered) {
            return;
        }
        registered = true;
        auto* data = RE::TESDataHandler::GetSingleton();
        auto* form = data ? data->LookupForm(kHolotapeLocalId, kPluginFile) : nullptr;
        if (!form || form->GetFormType() != RE::ENUM_FORM_ID::kNOTE) {
            log::warn("Settings holotape unavailable: {} is not loaded or has no holotape {:03X}", kPluginFile, kHolotapeLocalId);
            return;
        }
        if (!bytesMatch(kRegisterEquipSinkRva, kRegisterEquipSink.data(), kRegisterEquipSink.size())) {
            log::error("Settings holotape inert: equip sink registration bytes at RVA {:X} differ from FO4VR 1.2.72", kRegisterEquipSinkRva);
            return;
        }
        using Register = void (*)(RE::BSTEventSink<RE::TESEquipEvent>*);
        reinterpret_cast<Register>(REL::Offset(kRegisterEquipSinkRva).address())(&sink());
        s_holotape.store(form->GetFormID(), std::memory_order_release);
        log::info("Settings holotape {:08X} ready", form->GetFormID());
    }

    void onSessionReady() noexcept
    {
        auto* player = RE::PlayerCharacter::GetSingleton();
        auto* object = holotape();
        if (!player || !object) {
            return;
        }
        s_played.store(false, std::memory_order_release);
        if (player->GetInventoryObjectCount(object) > 0) {
            return;
        }
        RE::BSTSmartPointer<RE::ExtraDataList> extra{};
        player->AddInventoryItem(object, extra, 1, nullptr, nullptr, nullptr);
        log::info("Settings holotape given to the player");
    }

    bool takePlayed() noexcept
    {
        return s_played.exchange(false, std::memory_order_acq_rel);
    }
}
