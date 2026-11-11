#include "SpawnBrowser.h"
#include "SpawnPluginIdentity.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <format>
#include <string_view>
#include <unordered_map>


namespace rpsui::settings_menu
{
    namespace
    {
        // Slot 0 is the "ALL" pseudo-category; item records store 1-based slots.
        constexpr std::array<const char*, 8> kCategoryNames{
            "ALL", "WEAPONS", "ARMOR", "AMMO", "AID", "BOOKS", "MISC", "KEYS"
        };

#ifndef RPSUI_SETTINGS_MENU_PREVIEW
        struct IndexedFormType
        {
            RE::ENUM_FORM_ID formType;
            std::uint8_t category;
        };

        // Spawnable inventory base-object types mirrored from F4Viewer's browser scope.
        constexpr std::array<IndexedFormType, 8> kIndexedFormTypes{ {
            { RE::ENUM_FORM_ID::kWEAP, 1 },
            { RE::ENUM_FORM_ID::kARMO, 2 },
            { RE::ENUM_FORM_ID::kAMMO, 3 },
            { RE::ENUM_FORM_ID::kALCH, 4 },
            { RE::ENUM_FORM_ID::kBOOK, 5 },
            { RE::ENUM_FORM_ID::kNOTE, 5 },
            { RE::ENUM_FORM_ID::kMISC, 6 },
            { RE::ENUM_FORM_ID::kKEYM, 7 },
        } };

#endif

        constexpr std::int32_t kAmmoSpawnCount = 100;
        constexpr std::uint8_t kAmmoCategory = 3;

        [[nodiscard]] bool nameLessCaseInsensitive(const std::string& lhs, const std::string& rhs)
        {
            const auto result = _strnicmp(lhs.c_str(), rhs.c_str(), (std::min)(lhs.size(), rhs.size()) + 1);
            return result < 0;
        }

        // A mod's rounds can live in several plugins: some are its own records, others override base game forms and stay
        // under the plugin that first defined them. This first source lists them all in one place. With Flying BRICKS
        // loaded it lists only that mod's rounds: it replaces the other ballistic ammunition, and rounds from other
        // mods with similar names, such as a 9mm beside its 9x19mm Parabellum, made the list confusing.
        void addAllAmmoSource(std::vector<SpawnBrowser::PluginRecord>& plugins)
        {
            const bool flyingBricks = std::ranges::any_of(plugins, [](const SpawnBrowser::PluginRecord& plugin) {
                return std::ranges::any_of(plugin.items, &SpawnBrowser::ItemRecord::flyingBricksRound);
            });
            SpawnBrowser::PluginRecord all{ .name = "All ammo" };
            for (const auto& plugin : plugins) {
                for (const auto& item : plugin.items) {
                    if (item.category == kAmmoCategory && (!flyingBricks || item.flyingBricksRound)) {
                        all.items.push_back(item);
                    }
                }
            }
            std::ranges::sort(all.items, [](const SpawnBrowser::ItemRecord& lhs, const SpawnBrowser::ItemRecord& rhs) {
                return nameLessCaseInsensitive(lhs.name, rhs.name);
            });
            plugins.insert(plugins.begin(), std::move(all));
        }

        [[nodiscard]] bool containsIgnoringCase(std::string_view text, std::string_view word) noexcept
        {
            const auto lower = [](unsigned char letter) { return std::tolower(letter); };
            return !std::ranges::search(text, word, {}, lower, lower).empty();
        }

        [[nodiscard]] bool matchesSearch(std::string_view name, std::string_view search) noexcept
        {
            std::size_t start = 0;
            while (start < search.size()) {
                const auto end = (std::min)(search.find(' ', start), search.size());
                if (end > start && !containsIgnoringCase(name, search.substr(start, end - start))) {
                    return false;
                }
                start = end + 1;
            }
            return true;
        }

        [[nodiscard]] std::size_t stepCursor(std::size_t cursor, int direction, std::size_t size)
        {
            if (size == 0) {
                return 0;
            }
            if (direction < 0) {
                return cursor == 0 ? 0 : cursor - 1;
            }
            return (std::min)(cursor + 1, size - 1);
        }

#ifndef RPSUI_SETTINGS_MENU_PREVIEW
        // The form's own file and every file that overrides it. GetFile(index) is no help here:
        // FO4VR's 0x14015A2C0 returns the last file for any index past the end.
        [[nodiscard]] bool definedBy(const RE::TESForm& form, std::string_view plugin) noexcept
        {
            const auto* files = form.sourceFiles.array;
            if (!files) {
                return false;
            }
            for (std::uint32_t index = 0; index < files->size(); ++index) {
                const auto* file = (*files)[index];
                if (file && _stricmp(file->filename, plugin.data()) == 0) {
                    return true;
                }
            }
            return false;
        }

        // Flying_BRICKS.esp also overrides vanilla's companion and workshop-teammate copies of its
        // rounds, such as CompanionAmmo308Caliber. Those carry the AMMO Non-Playable flag (0x2) and
        // would list a second ".308 Round"; the player's own rounds never do.
        [[nodiscard]] bool playableAmmo(const RE::TESForm& form) noexcept
        {
            return form.GetFormType() == RE::ENUM_FORM_ID::kAMMO &&
                   (static_cast<const RE::TESAmmo&>(form).data.flags & 0x2) == 0;
        }

        [[nodiscard]] RE::TESBoundObject* resolveSpawnableObject(std::uint32_t formId)
        {
            auto* form = RE::TESForm::GetFormByID(formId);
            return form ? form->As<RE::TESBoundObject>() : nullptr;
        }

#endif

        // Convert game-sourced Windows-1252 names before feeding ImGui's UTF-8 text API.
        [[nodiscard]] bool isLikelyValidUtf8(std::string_view text) noexcept
        {
            std::size_t i = 0;
            while (i < text.size()) {
                const auto byte = static_cast<unsigned char>(text[i]);
                std::size_t continuation = 0;
                if (byte < 0x80u) {
                    ++i;
                    continue;
                } else if ((byte & 0xE0u) == 0xC0u) {
                    if (byte < 0xC2u) {
                        return false;
                    }
                    continuation = 1;
                } else if ((byte & 0xF0u) == 0xE0u) {
                    continuation = 2;
                } else if ((byte & 0xF8u) == 0xF0u) {
                    if (byte > 0xF4u) {
                        return false;
                    }
                    continuation = 3;
                } else {
                    return false;
                }
                if (i + continuation >= text.size()) {
                    return false;
                }
                for (std::size_t k = 1; k <= continuation; ++k) {
                    if ((static_cast<unsigned char>(text[i + k]) & 0xC0u) != 0x80u) {
                        return false;
                    }
                }
                i += continuation + 1;
            }
            return true;
        }

        [[nodiscard]] std::string windows1252ToUtf8(std::string_view text)
        {
            // Windows-1252 0x80-0x9F block; undefined slots map to U+FFFD.
            static constexpr std::array<char16_t, 32> kCp1252High{
                0x20AC, 0xFFFD, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021,
                0x02C6, 0x2030, 0x0160, 0x2039, 0x0152, 0xFFFD, 0x017D, 0xFFFD,
                0xFFFD, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014,
                0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0xFFFD, 0x017E, 0x0178,
            };
            std::string out;
            out.reserve(text.size() + text.size() / 2);
            for (const char ch : text) {
                const auto byte = static_cast<unsigned char>(ch);
                if (byte < 0x80u) {
                    out.push_back(ch);
                    continue;
                }
                const char32_t cp = byte < 0xA0u ? kCp1252High[byte - 0x80u] : byte;
                if (cp < 0x800u) {
                    out.push_back(static_cast<char>(0xC0u | (cp >> 6)));
                    out.push_back(static_cast<char>(0x80u | (cp & 0x3Fu)));
                } else {
                    out.push_back(static_cast<char>(0xE0u | (cp >> 12)));
                    out.push_back(static_cast<char>(0x80u | ((cp >> 6) & 0x3Fu)));
                    out.push_back(static_cast<char>(0x80u | (cp & 0x3Fu)));
                }
            }
            return out;
        }

        [[nodiscard]] std::string utf8SafeName(std::string_view text)
        {
            return isLikelyValidUtf8(text) ? std::string{ text } : windows1252ToUtf8(text);
        }
    }

    bool SpawnBrowser::ensureIndexBuilt()
    {
#ifdef RPSUI_SETTINGS_MENU_PREVIEW
        if (!_indexBuilt) {
            _plugins = {{"Fallout4.esm", {{1,1,"10mm Pistol"},{2,1,"Combat Rifle"},{3,2,"Leather Chest Piece"},{4,3,"10mm Round"},{5,4,"Stimpak"},{6,4,"Purified Water"},{7,6,"Duct Tape"}},0},
                        {"Example Workshop.esp", {{8,1,"Custom Service Rifle"},{9,4,"Field Rations"}},1}};
            addAllAmmoSource(_plugins);
            _indexBuilt = true;
            rebuildVisiblePlugins();
            rebuildFilteredItems();
            _lastResult = "PREVIEW ONLY — actions do not affect the game";
        }
        return true;
#else
        if (_indexBuilt) {
            return true;
        }

        auto* dataHandler = RE::TESDataHandler::GetSingleton();
        if (!dataHandler) {
            _lastResult = "TESDataHandler unavailable; spawn index not built";
            logger::warn("Spawner: TESDataHandler unavailable; index build skipped.");
            return false;
        }

        const auto start = std::chrono::steady_clock::now();

        // FO4VR SortAndLoadPluginsFromPluginsFile (0x14011FAA0) orders files;
        // CompileFiles (0x14011E8E0) consumes that list with activeFile last.
        // Daytripper's ConstructObjectListForFiles preserves this same sequence.
        // Copy filename ranks here; never retain the engine's file pointers.
        std::unordered_map<std::string, std::size_t> loadOrderByName;
        const auto* activeFile = dataHandler->activeFile;
        for (const auto* file : dataHandler->files) {
            if (file && file != activeFile && file->filename[0] != '\0') {
                loadOrderByName.try_emplace(file->filename, loadOrderByName.size());
            }
        }
        if (activeFile && activeFile->filename[0] != '\0') {
            loadOrderByName.insert_or_assign(activeFile->filename, loadOrderByName.size());
        }

        // Build only the buckets represented by actual spawnable forms. The form's
        // original owning file supplies its name and engine position. Runtime
        // FormIDs supply identity only: their full/light indices cannot encode
        // the combined load order. No loader-specific compiled collections are used.
        std::unordered_map<std::uint32_t, std::size_t> slotByKey;
        _plugins.clear();

        std::size_t itemCount = 0;
        std::size_t skippedUnowned = 0;
        for (const auto& indexed : kIndexedFormTypes) {
            const auto& forms = dataHandler->formArrays[std::to_underlying(indexed.formType)];
            for (const auto* form : forms) {
                if (!form) {
                    continue;
                }
                const auto formId = form->formID;
                if (spawn_plugin_identity::isRuntimeCreatedFormId(formId)) {
                    continue;  // runtime-created forms have no owning plugin
                }
                const auto name = RE::TESFullName::GetFullName(*form, false);
                if (name.empty()) {
                    continue;  // unnamed forms are engine plumbing, not spawnable pickups
                }

                const auto* owner = form->GetFile(0);
                if (!owner || owner->filename[0] == '\0') {
                    ++skippedUnowned;
                    continue;
                }

                const auto key = spawn_plugin_identity::pluginKeyFromFormId(formId);
                auto [slot, inserted] = slotByKey.try_emplace(key, _plugins.size());
                if (inserted) {
                    const auto order = loadOrderByName.find(owner->filename);
                    if (order == loadOrderByName.end()) {
                        _lastResult = std::format("Engine load order unavailable for {}; spawn index not built", utf8SafeName(owner->filename));
                        logger::warn("Spawner: owner '{}' (key {:08X}) missing from engine file order; index build skipped.",
                            owner->filename, key);
                        _plugins.clear();
                        return false;
                    }
                    _plugins.push_back(PluginRecord{
                        .name = utf8SafeName(owner->filename),
                        .items = {},
                        .loadOrder = order->second,
                    });
                }
                _plugins[slot->second].items.push_back(ItemRecord{
                    .formId = formId,
                    .category = indexed.category,
                    .name = utf8SafeName(name),
                    .flyingBricksRound = indexed.category == kAmmoCategory && playableAmmo(*form) && definedBy(*form, "Flying_BRICKS.esp"),
                });
                ++itemCount;
            }
        }

        std::ranges::sort(_plugins, {}, &PluginRecord::loadOrder);
        for (auto& plugin : _plugins) {
            std::ranges::sort(plugin.items, [](const ItemRecord& lhs, const ItemRecord& rhs) {
                return nameLessCaseInsensitive(lhs.name, rhs.name);
            });
        }
        const auto pluginCount = _plugins.size();
        addAllAmmoSource(_plugins);

        _indexBuilt = true;
        _pluginCursor = 0;
        _categoryCursor = 0;
        _itemCursor = 0;
        _view = SpawnView::Browse;
        rebuildVisiblePlugins();
        rebuildFilteredItems();

        const auto elapsedMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
        _lastResult = std::format("Indexed {} items from {} plugins", itemCount, pluginCount);
        logger::info(
            "Spawner: indexed {} named items, {} of them under All ammo, across {} plugins in engine load order in {:.1f} ms ({} forms skipped without an owning plugin file).",
            itemCount, _plugins.front().items.size(), pluginCount, elapsedMs, skippedUnowned);
        return true;
#endif
    }

    const SpawnBrowser::PluginRecord* SpawnBrowser::currentPlugin() const
    {
        if (_pluginCursor >= _plugins.size()) {
            return nullptr;
        }
        return &_plugins[_pluginCursor];
    }

    const SpawnBrowser::ItemRecord* SpawnBrowser::cursorItem() const
    {
        const auto* plugin = currentPlugin();
        if (!plugin || _itemCursor >= _filteredItems.size()) {
            return nullptr;
        }
        const auto itemIndex = _filteredItems[_itemCursor];
        if (itemIndex >= plugin->items.size()) {
            return nullptr;
        }
        return &plugin->items[itemIndex];
    }

    void SpawnBrowser::rebuildFilteredItems()
    {
        _filteredItems.clear();
        _itemCursor = 0;
        const auto* plugin = currentPlugin();
        if (!plugin) {
            return;
        }
        for (std::size_t i = 0; i < plugin->items.size(); ++i) {
            const auto& item = plugin->items[i];
            if ((_categoryCursor == 0 || item.category == _categoryCursor) && matchesSearch(item.name, _itemSearch)) {
                _filteredItems.push_back(i);
            }
        }
    }

    void SpawnBrowser::rebuildVisiblePlugins()
    {
        _visiblePlugins.clear();
        for (std::size_t i = 0; i < _plugins.size(); ++i) {
            if (matchesSearch(_plugins[i].name, _pluginSearch)) {
                _visiblePlugins.push_back(i);
            }
        }
    }

    void SpawnBrowser::setPluginSearch(std::string_view text)
    {
        if (text != _pluginSearch) {
            _pluginSearch = text;
            rebuildVisiblePlugins();
        }
    }

    void SpawnBrowser::setItemSearch(std::string_view text)
    {
        if (text != _itemSearch) {
            _itemSearch = text;
            rebuildFilteredItems();
        }
    }

    void SpawnBrowser::movePlugin(int direction)
    {
        if (_view != SpawnView::Browse || _plugins.empty()) {
            return;
        }
        const auto next = stepCursor(_pluginCursor, direction, _plugins.size());
        if (next == _pluginCursor) {
            return;
        }
        _pluginCursor = next;
        rebuildFilteredItems();
    }

    void SpawnBrowser::moveCategory(int direction)
    {
        if (_view != SpawnView::Browse) {
            return;
        }
        // Categories wrap: they are a small fixed ring, unlike the bounded lists.
        const auto count = kCategoryNames.size();
        _categoryCursor = (_categoryCursor + count + (direction < 0 ? count - 1 : 1)) % count;
        rebuildFilteredItems();
    }

    void SpawnBrowser::moveCursor(int direction)
    {
        if (_view == SpawnView::ItemMenu) {
            _menuCursor = stepCursor(_menuCursor, direction, _menuActions.size());
            return;
        }
        _itemCursor = stepCursor(_itemCursor, direction, _filteredItems.size());
    }

    void SpawnBrowser::openItemMenu()
    {
        const auto* item = cursorItem();
        if (!item) {
            _lastResult = "No item selected";
            return;
        }

        _menuActions.clear();
        _menuCursor = 0;
        _menuFormId = item->formId;
        constexpr std::array<std::int32_t, 3> kItemCounts{ 1, 5, 25 };
        constexpr std::array<std::int32_t, 4> kAmmoCounts{ 1, 25, 100, 1000 };
        const auto addCounts = [&](const auto& counts) {
            for (const std::int32_t count : counts) {
                _menuActions.push_back(MenuAction{
                    .label = std::format("ADD ×{}", count),
                    .formId = item->formId,
                    .count = count,
                });
            }
        };
        if (item->category == kAmmoCategory) {
            addCounts(kAmmoCounts);
        } else {
            addCounts(kItemCounts);
        }

        // Weapons offer their base ammo as a convenience action. The ammo pointer
        // comes from the base form's weaponData and is gated on actually being an
        // AMMO form so a bad read degrades into a missing menu entry, not a crash.
#ifndef RPSUI_SETTINGS_MENU_PREVIEW
        if (const auto* weapon = RE::TESForm::GetFormByID<RE::TESObjectWEAP>(item->formId)) {
            const auto* ammo = weapon->weaponData.ammo;
            if (ammo && ammo->Is(RE::ENUM_FORM_ID::kAMMO)) {
                const auto ammoName = utf8SafeName(RE::TESFullName::GetFullName(*ammo, false));
                _menuActions.push_back(MenuAction{
                    .label = std::format("ADD AMMO ×{}{}{}", kAmmoSpawnCount, ammoName.empty() ? "" : " — ", ammoName),
                    .formId = ammo->formID,
                    .count = kAmmoSpawnCount,
                });
            }
        }

#else
        if (item->category == 1) _menuActions.push_back({"ADD AMMO ×100",4,100});
#endif
        _view = SpawnView::ItemMenu;
    }

    void SpawnBrowser::executeMenuAction(const MenuAction& action)
    {
#ifdef RPSUI_SETTINGS_MENU_PREVIEW
        _lastResult = std::format("PREVIEW: {} item(s) added — no game changes", action.count);
        return;
#else
        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player) {
            _lastResult = "Player unavailable; nothing added";
            logger::warn("Spawner: player singleton unavailable; add aborted.");
            return;
        }
        auto* object = resolveSpawnableObject(action.formId);
        if (!object) {
            _lastResult = std::format("Form {:08X} is not a spawnable object", action.formId);
            logger::warn("Spawner: form {:08X} did not resolve to a TESBoundObject; add aborted.", action.formId);
            return;
        }
        if (action.count <= 0) {
            _lastResult = "Invalid count";
            return;
        }

        const auto name = utf8SafeName(RE::TESFullName::GetFullName(*object, false));
        RE::BSTSmartPointer<RE::ExtraDataList> extra{};
        player->AddInventoryItem(object, extra, static_cast<std::uint32_t>(action.count), nullptr, nullptr, nullptr);
        const auto owned = player->GetInventoryObjectCount(object);
        _lastResult = std::format("Added {}× {} (inventory: {})", action.count, name.empty() ? "item" : name, owned);
        logger::info("Spawner: added {}x {:08X} ('{}') to the player inventory (now {}).",
            action.count, action.formId, name, owned);
#endif
    }

    void SpawnBrowser::activate()
    {
        if (_view == SpawnView::Browse) {
            openItemMenu();
            return;
        }
        if (_menuCursor < _menuActions.size()) {
            executeMenuAction(_menuActions[_menuCursor]);
        }
    }

    bool SpawnBrowser::selectPlugin(std::size_t index)
    {
        if (_view != SpawnView::Browse || index >= _plugins.size() || index == _pluginCursor) {
            return false;
        }
        _pluginCursor = index;
        rebuildFilteredItems();
        return true;
    }

    bool SpawnBrowser::selectCategory(std::size_t index)
    {
        if (_view != SpawnView::Browse || index >= kCategoryNames.size() || index == _categoryCursor) {
            return false;
        }
        _categoryCursor = index;
        rebuildFilteredItems();
        return true;
    }

    bool SpawnBrowser::openItem(std::uint32_t formId)
    {
        const auto* plugin = currentPlugin();
        if (_view != SpawnView::Browse || !plugin || formId == 0) {
            return false;
        }

        for (std::size_t cursor = 0; cursor < _filteredItems.size(); ++cursor) {
            const auto itemIndex = _filteredItems[cursor];
            if (itemIndex < plugin->items.size() && plugin->items[itemIndex].formId == formId) {
                _itemCursor = cursor;
                openItemMenu();
                return _view == SpawnView::ItemMenu && _menuFormId == formId;
            }
        }
        return false;
    }

    bool SpawnBrowser::activateMenuAction(std::size_t index, std::uint32_t expectedFormId)
    {
        if (_view != SpawnView::ItemMenu || expectedFormId == 0 ||
            expectedFormId != _menuFormId || index >= _menuActions.size()) {
            return false;
        }
        _menuCursor = index;
        executeMenuAction(_menuActions[index]);
        return true;
    }

    bool SpawnBrowser::back()
    {
        if (_view == SpawnView::ItemMenu) {
            _view = SpawnView::Browse;
            _menuActions.clear();
            _menuCursor = 0;
            _menuFormId = 0;
            return true;
        }
        return false;
    }

    void SpawnBrowser::returnToBrowse()
    {
        while (back()) {
        }
    }

    std::size_t SpawnBrowser::categoryCount() const noexcept
    {
        return kCategoryNames.size();
    }

    std::string_view SpawnBrowser::categoryName(std::size_t index) const noexcept
    {
        return index < kCategoryNames.size() ? kCategoryNames[index] : std::string_view{};
    }

    const SpawnBrowser::PluginRecord* SpawnBrowser::pluginAt(std::size_t index) const noexcept
    {
        return index < _plugins.size() ? &_plugins[index] : nullptr;
    }

    const SpawnBrowser::ItemRecord* SpawnBrowser::filteredItemAt(std::size_t index) const noexcept
    {
        const auto* plugin = currentPlugin();
        if (!plugin || index >= _filteredItems.size()) {
            return nullptr;
        }
        const auto itemIndex = _filteredItems[index];
        return itemIndex < plugin->items.size() ? &plugin->items[itemIndex] : nullptr;
    }

    const SpawnBrowser::MenuAction* SpawnBrowser::menuActionAt(std::size_t index) const noexcept
    {
        return index < _menuActions.size() ? &_menuActions[index] : nullptr;
    }

}
