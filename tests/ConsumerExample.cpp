#include <RPS_UI_Framework/Client.h>
#include <RPS_UI_Framework/WorldDraw.h>

// A mod that draws a marker in the world. Resolve the query with
// GetProcAddress(GetModuleHandleW(rpsui::api::kModuleName), rpsui::api::kQueryExportName);
// when the module is absent the mod keeps running without the display.
namespace consumer_example {
    class Marker {
        const rpsui::api::framework::Api* _framework{};
        const rpsui::api::world_draw::Api* _draw{};
        std::uint64_t _owner{};
    public:
        rpsui::api::Result connect(rpsui::api::QueryInterfaceV1 query) noexcept {
            auto result = rpsui::api::acquire(query, _framework);
            if (result == rpsui::api::Result::Ok) result = rpsui::api::acquire(query, _draw);
            if (result != rpsui::api::Result::Ok) return result;
            rpsui::api::framework::ConsumerRegistration registration;
            const char id[] = "example.marker";
            for (unsigned i = 0; i < sizeof(id); ++i) registration.consumerId[i] = registration.displayName[i] = id[i];
            rpsui::api::framework::ConsumerHandle handle;
            result = _framework->registerConsumer(&registration, &handle);
            if (result == rpsui::api::Result::Ok) _owner = handle.ownerToken;
            return result;
        }
        // Any game-thread tick; republish every frame for continuous display.
        rpsui::api::Result show(float x, float y, float z) noexcept {
            if (!_draw || !_owner) return rpsui::api::Result::FrameworkNotReady;
            rpsui::api::world_draw::Sphere sphere;
            sphere.center[0] = x;
            sphere.center[1] = y;
            sphere.center[2] = z;
            rpsui::api::world_draw::Publication publication;
            publication.sphereCount = 1;
            publication.spheres = &sphere;
            return _draw->submit(_owner, &publication);
        }
        rpsui::api::Result close() noexcept {
            if (!_framework || !_owner) return rpsui::api::Result::Ok;
            const auto result = _framework->unregisterConsumer(_owner);
            if (result == rpsui::api::Result::Ok) _owner = 0;
            return result;
        }
    };
}

int main() {}
