#include "PCH.h"

#include "ExternalPages.h"

#include <imgui_internal.h>
#include <RPS_UI_Framework/ImGuiBuild.h>

namespace rpsui::settings_menu::external_pages
{
    namespace
    {
        struct Page
        {
            std::uint64_t handle{ 0 };
            std::uint64_t owner{ 0 };
            std::string id;
            std::string title;
            std::int32_t sortOrder{ 0 };
            api::settings_menu::PageDrawCallback draw{ nullptr };
            void* userData{ nullptr };
        };

        struct State
        {
            std::mutex mutex;
            std::vector<Page> pages;
            std::uint64_t nextHandle{ 1 };
            const FrameContext* frame{ nullptr }; // Render thread, during the menu's frame.
        };

        State& state() noexcept
        {
            static State value;
            return value;
        }

        [[nodiscard]] std::string bounded(const char* text, std::size_t capacity)
        {
            return std::string(text, strnlen_s(text, capacity));
        }
    }

    api::Result add(std::uint64_t owner, const api::settings_menu::PageRegistration& registration,
        std::uint64_t& outHandle) noexcept
    {
        outHandle = 0;
        if (registration.size < sizeof(registration)) return api::Result::InvalidArgument;
        if (registration.version != api::kRecordVersion) return api::Result::VersionMismatch;
        if (!registration.drawCallback || !registration.pageId[0] || !registration.title[0]) return api::Result::InvalidArgument;
        const auto host = RPSUI_IMGUI_BUILD();
        const auto& build = registration.imgui;
        if (build.size < sizeof(build) || build.versionNum != host.versionNum || build.wcharSize != host.wcharSize ||
            build.ioSize != host.ioSize || build.styleSize != host.styleSize || build.contextSize != host.contextSize ||
            build.drawListSize != host.drawListSize || build.fontAtlasSize != host.fontAtlasSize ||
            build.fontSize != host.fontSize || build.textureDataSize != host.textureDataSize ||
            build.drawVertSize != host.drawVertSize || build.drawIdxSize != host.drawIdxSize)
            return api::Result::ImGuiBuildMismatch;
        try {
            auto& s = state();
            std::scoped_lock lock(s.mutex);
            const auto id = bounded(registration.pageId, sizeof(registration.pageId));
            if (std::ranges::any_of(s.pages, [&](const Page& page) { return page.id == id; }))
                return api::Result::ResourceConflict;
            if (s.pages.size() >= api::settings_menu::kMaxPages) return api::Result::CapacityReached;
            Page page;
            page.handle = s.nextHandle++;
            page.owner = owner;
            page.id = id;
            page.title = bounded(registration.title, sizeof(registration.title));
            page.sortOrder = registration.sortOrder;
            page.draw = registration.drawCallback;
            page.userData = registration.userData;
            outHandle = page.handle;
            s.pages.push_back(std::move(page));
            std::ranges::stable_sort(s.pages, {}, &Page::sortOrder);
            logger::info("Settings menu page '{}' added", id);
            return api::Result::Ok;
        } catch (...) {
            return api::Result::InternalError;
        }
    }

    api::Result remove(std::uint64_t owner, std::uint64_t handle) noexcept
    {
        auto& s = state();
        std::unique_lock lock(s.mutex, std::try_to_lock);
        if (!lock.owns_lock()) return api::Result::CallbackBusy;
        const auto found = std::ranges::find(s.pages, handle, &Page::handle);
        if (found == s.pages.end()) return api::Result::PanelNotRegistered;
        if (found->owner != owner) return api::Result::OwnershipMismatch;
        s.pages.erase(found);
        return api::Result::Ok;
    }

    void removeOwner(std::uint64_t owner) noexcept
    {
        auto& s = state();
        std::scoped_lock lock(s.mutex);
        std::erase_if(s.pages, [&](const Page& page) { return page.owner == owner; });
    }

    std::vector<Tab> tabs()
    {
        auto& s = state();
        std::scoped_lock lock(s.mutex);
        std::vector<Tab> result;
        result.reserve(s.pages.size());
        for (const auto& page : s.pages) result.push_back({ page.handle, page.title });
        return result;
    }

    void setFrameContext(const FrameContext* context) noexcept
    {
        state().frame = context;
    }

    bool draw(std::uint64_t handle, float width, float height, bool backPressed, bool& usedBack) noexcept
    {
        usedBack = false;
        auto& s = state();
        std::scoped_lock lock(s.mutex);
        const auto found = std::ranges::find(s.pages, handle, &Page::handle);
        if (found == s.pages.end()) return false;
        api::settings_menu::PageDrawFrame frame{};
        frame.pageHandle = handle;
        frame.width = width;
        frame.height = height;
        frame.backPressed = backPressed ? 1 : 0;
        if (const auto* context = s.frame) {
            frame.contextGeneration = context->contextGeneration;
            frame.d3dDevice = context->d3dDevice;
            frame.imgui = context->imgui;
            frame.theme = context->theme;
            for (std::size_t role = 0; role < api::framework::kFontRoleCount; ++role) frame.fonts[role] = context->fonts[role];
            frame.deltaSeconds = context->deltaSeconds;
        }
        auto* imguiContext = ImGui::GetCurrentContext();
        try {
            usedBack = found->draw(&frame, found->userData);
        } catch (...) {
            logger::error("Settings menu page '{}' threw while drawing", found->id);
        }
        // The page's copy of ImGui changed only its own current context.
        ImGui::SetCurrentContext(imguiContext);
        return true;
    }
}
