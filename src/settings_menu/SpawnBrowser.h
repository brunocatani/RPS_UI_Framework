#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>


namespace rpsui::settings_menu
{
    enum class SpawnView : std::uint8_t
    {
        Browse,
        ItemMenu,
    };

    /**
     * In-game item browser and spawner backing the configurator Spawn tab.
     *
     * Ownership and threading: every method must run on the F4SE game thread
     * (the configurator runtime calls in from its drained action queue while
     * holding the runtime mutex). The index holds plugin names and item names
     * by value plus raw FormIDs only, never engine pointers, so nothing here
     * can dangle across frames; live forms are re-resolved by FormID at the
     * moment an action executes and every hop fails closed. The search setters
     * touch only presentation state and engine data never, so the keyboard panel
     * may call them from the render thread, still under the runtime mutex.
     */
    class SpawnBrowser
    {
    public:
        struct ItemRecord
        {
            std::uint32_t formId{ 0 };
            std::uint8_t category{ 0 };
            std::string name;
            bool flyingBricksRound{ false };  // A playable round Flying_BRICKS.esp defines or overrides.
        };

        struct PluginRecord
        {
            std::string name;
            std::vector<ItemRecord> items;
            std::size_t loadOrder{ 0 };
        };

        struct MenuAction
        {
            std::string label;
            std::uint32_t formId{ 0 };
            std::int32_t count{ 0 };
        };

        /** Builds the plugin/item index once. Safe to call repeatedly. Game thread only. */
        bool ensureIndexBuilt();

        void movePlugin(int direction);
        void moveCategory(int direction);
        void moveCursor(int direction);
        /** Right trigger: Browse opens the item menu, ItemMenu executes the highlighted action. */
        void activate();
        /** Direct UI navigation. Each operation validates the current view and
         *  stable identity before mutating state, so delayed DOM callbacks fail closed. */
        [[nodiscard]] bool selectPlugin(std::size_t index);
        [[nodiscard]] bool selectCategory(std::size_t index);
        [[nodiscard]] bool openItem(std::uint32_t formId);
        [[nodiscard]] bool activateMenuAction(std::size_t index, std::uint32_t expectedFormId);
        /** Right grip: back one level inside the section. Returns false when already at Browse. */
        [[nodiscard]] bool back();
        /** Every space-separated word must appear in the name, ignoring case. The source
         *  search narrows the source list; the item search narrows the current source and tab. */
        void setPluginSearch(std::string_view text);
        void setItemSearch(std::string_view text);
        /** Left grip: back to the browse/side-menu level, closing the item menu from any depth. */
        void returnToBrowse();

        [[nodiscard]] SpawnView view() const noexcept { return _view; }
        [[nodiscard]] bool indexBuilt() const noexcept { return _indexBuilt; }
        [[nodiscard]] const std::string& lastResult() const noexcept { return _lastResult; }

        // Read-only presentation access. Callers hold the configurator runtime
        // mutex; the game-thread index and menu state remain the sole owner.
        [[nodiscard]] std::size_t pluginCount() const noexcept { return _plugins.size(); }
        /** Rows of the source list after the source search; each names a pluginAt() index. */
        [[nodiscard]] std::size_t visiblePluginCount() const noexcept { return _visiblePlugins.size(); }
        [[nodiscard]] std::size_t visiblePluginAt(std::size_t row) const noexcept
        {
            return row < _visiblePlugins.size() ? _visiblePlugins[row] : _plugins.size();
        }
        [[nodiscard]] std::size_t pluginCursor() const noexcept { return _pluginCursor; }
        [[nodiscard]] std::size_t categoryCount() const noexcept;
        [[nodiscard]] std::size_t categoryCursor() const noexcept { return _categoryCursor; }
        [[nodiscard]] std::string_view categoryName(std::size_t index) const noexcept;
        [[nodiscard]] const PluginRecord* pluginAt(std::size_t index) const noexcept;
        [[nodiscard]] std::size_t filteredItemCount() const noexcept { return _filteredItems.size(); }
        [[nodiscard]] std::size_t itemCursor() const noexcept { return _itemCursor; }
        [[nodiscard]] const ItemRecord* filteredItemAt(std::size_t index) const noexcept;
        [[nodiscard]] std::size_t menuActionCount() const noexcept { return _menuActions.size(); }
        [[nodiscard]] std::size_t menuCursor() const noexcept { return _menuCursor; }
        [[nodiscard]] std::uint32_t menuFormId() const noexcept { return _menuFormId; }
        [[nodiscard]] const MenuAction* menuActionAt(std::size_t index) const noexcept;
        [[nodiscard]] const ItemRecord* currentItem() const noexcept { return cursorItem(); }

    private:
        void rebuildFilteredItems();
        void rebuildVisiblePlugins();
        void openItemMenu();
        void executeMenuAction(const MenuAction& action);
        [[nodiscard]] const PluginRecord* currentPlugin() const;
        [[nodiscard]] const ItemRecord* cursorItem() const;

        bool _indexBuilt = false;
        std::vector<PluginRecord> _plugins;
        std::size_t _pluginCursor = 0;
        std::size_t _categoryCursor = 0;
        std::vector<std::size_t> _filteredItems;  // indices into the current plugin's items
        std::vector<std::size_t> _visiblePlugins;  // indices into _plugins
        std::string _pluginSearch;
        std::string _itemSearch;
        std::size_t _itemCursor = 0;

        SpawnView _view = SpawnView::Browse;
        std::vector<MenuAction> _menuActions;
        std::size_t _menuCursor = 0;
        std::uint32_t _menuFormId = 0;

        std::string _lastResult = "Select an item and pull the trigger";
    };
}
