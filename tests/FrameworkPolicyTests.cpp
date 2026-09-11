#include "ContextualScrollPolicy.h"
#include "InputCapturePolicy.h"
#include "NativeWandPose.h"
#include "PanelResizePolicy.h"
#include "PointerClickGate.h"
#include "PointerHandSelection.h"
#include "PointerPanelIntersection.h"
#include "RPSUIFrameworkApi.h"
#include "render/EngineStereoSubmissionPolicy.h"

#include <chrono>
#include <array>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>

namespace
{
    void require(bool condition, const std::string& message)
    {
        if (!condition) {
            throw std::runtime_error(message);
        }
    }

    void testNativeWandPointer()
    {
        using namespace rpsui::pointer_panel_intersection;
        struct Transform {
            std::array<std::array<float, 4>, 3> rotate{};
            std::array<float, 3> translate{ -79000, 90000, 7950 };
            float scale{ 1 };
        } transform;
        for (float yaw : { -2.1f, 0.0f, 1.2f }) {
            for (float pitch : { -0.4f, 0.0f, 0.5f }) {
                const float sy = std::sin(yaw), cy = std::cos(yaw);
                const float sp = std::sin(pitch), cp = std::cos(pitch);
                transform.rotate = {{{cy, -sy, 0, 0}, {sy * cp, cy * cp, sp, 0}, {-sy * sp, -cy * sp, cp, 0}}};
                rpsui::sdk::HandInputV1 hand;
                require(rpsui::input_policy::nativeWandPose(transform, hand), "valid native wand pose rejected");
                const Vector origin{hand.position[0], hand.position[1], hand.position[2]};
                const Vector direction{hand.forward[0], hand.forward[1], hand.forward[2]};
                const Vector forward{sy * cp, cy * cp, sp};
                const Panel panel{
                    .center = addScaled(origin, forward, 100),
                    .right = {cy, -sy, 0}, .up = {-sy * sp, -cy * sp, cp},
                    .front = {-forward.x, -forward.y, -forward.z}, .width = 95, .height = 95,
                };
                Hit hit;
                require(intersect({origin, direction, 200}, panel, hit) && hit.inside &&
                    std::fabs(hit.u - .5f) < .001f && std::fabs(hit.v - .5f) < .001f,
                    "native pointer missed aimed-at panel after controller yaw/pitch");
            }
        }
        rpsui::sdk::HandInputV1 hand;
        transform.scale = 0;
        require(!rpsui::input_policy::nativeWandPose(transform, hand), "zero-scale wand accepted");
        transform.scale = 1;
        transform.rotate[1] = {};
        require(!rpsui::input_policy::nativeWandPose(transform, hand), "degenerate wand direction accepted");
    }

    void testApiContract()
    {
        using namespace rpsui::sdk;
        static_assert(sizeof(PanelRegistrationV1) == 248);
        static_assert(sizeof(PanelRenderFrameV1) == 144);
        static_assert(offsetof(PanelRenderFrameV1, backDown) == 106);
        static_assert(offsetof(PanelRegistrationV1, flags) == 192);
        static_assert(offsetof(PanelRegistrationV1, renderCallback) == 200);
        require(RPSUI_API_VERSION == 1, "API version changed");
        const auto circle = static_cast<std::uint32_t>(PanelFlagV1::CircularInput);
        require(panelContains(0, 0.01f, 0.01f), "existing rectangle lost its corner");
        require(!panelContains(circle, 0.01f, 0.01f), "circular panel captured a transparent corner");
        require(panelContains(circle, 0.5f, 0.5f), "circular panel lost its center");
        require(panelContains(circle, 0.5f, 0.0f), "circular panel lost its edge");
        require(sizeof(PanelRenderFrameV1) >= 120, "render frame ABI shrank");
        require(
            featureMask(FeatureV1::MultipleWorldPanels) != 0,
            "multi-panel feature missing");
        require(
            static_cast<std::uint32_t>(ResizeHandleV1::BottomRight) ==
                static_cast<std::uint32_t>(
                    rpsui::panel_resize::Handle::BottomRight),
            "resize handle ABI disagrees with host policy");
    }

    void testPointerSelection()
    {
        using namespace rpsui::pointer_hand_selection;
        State state{};
        const Candidate left{
            .valid = true,
            .hitsPanel = true,
            .primaryDown = false,
        };
        const Candidate right{
            .valid = true,
            .hitsPanel = false,
            .primaryDown = false,
        };
        auto decision = choose(state, left, right, Hand::Right);
        require(decision.hand == Hand::Left, "nearest hitting hand was not selected");
        commit(state, decision, left);

        const Candidate pressedLeft{
            .valid = true,
            .hitsPanel = true,
            .primaryDown = true,
        };
        decision = choose(state, pressedLeft, right, Hand::Right);
        commit(state, decision, pressedLeft);
        require(state.pressBeganOnPanel, "panel press was not latched");

        const Candidate draggedLeft{
            .valid = true,
            .hitsPanel = false,
            .primaryDown = true,
        };
        decision = choose(state, draggedLeft, right, Hand::Right);
        require(
            controlsPanelInteraction(decision, draggedLeft),
            "drag lost panel ownership outside the edge");
    }

    void testClickGate()
    {
        // Stick/back intent can transfer a hovering Config cursor, but never a drag.
        {
            using namespace rpsui::pointer_hand_selection;
            State pointer{ .active = Hand::Right };
            Candidate left{ .valid = true, .hitsPanel = true, .navigationIntent = true };
            Candidate right{ .valid = true, .hitsPanel = true };
            require(choose(pointer, left, right, Hand::Right).hand == Hand::Left, "left stick could not take the pointed Config panel");
            left.hitsPanel = false;
            require(choose(pointer, left, right, Hand::Right).hand == Hand::Right, "off-panel stick stole the cursor");
            left.hitsPanel = true;
            pointer.submittedPrimaryDown = true;
            pointer.pressBeganOnPanel = true;
            require(choose(pointer, left, right, Hand::Right).hand == Hand::Right, "stick stole a held slider drag");
            pointer.submittedPrimaryDown = false;
            left.navigationIntent = false;
            require(choose(pointer, left, right, Hand::Right).hand == Hand::Right, "non-config hover changed ownership");
            pointer.active = Hand::Left;
            right.navigationIntent = true;
            require(choose(pointer, left, right, Hand::Left).hand == Hand::Right, "right stick could not take the pointed Config panel");
        }
        using namespace rpsui::pointer_click_gate;
        State state{};
        require(!advance(state, 10, true, false, true), "lease activated immediately");
        require(!advance(state, 11, true, false, true), "neutral frame clicked");
        require(advance(state, 12, true, true, true), "mature leased click was rejected");
        require(!advance(state, 13, true, true, false), "rejected lease forwarded input");
    }

    void testIntersection()
    {
        using namespace rpsui::pointer_panel_intersection;
        const Panel panel{
            .center = { 0.0f, 2.0f, 0.0f },
            .right = { 1.0f, 0.0f, 0.0f },
            .up = { 0.0f, 0.0f, 1.0f },
            .front = { 0.0f, -1.0f, 0.0f },
            .width = 2.0f,
            .height = 1.0f,
        };
        Hit hit{};
        require(
            intersect(
                {
                    .origin = { 0.0f, 0.0f, 0.0f },
                    .direction = { 0.0f, 1.0f, 0.0f },
                    .maxDistance = 10.0f,
                },
                panel,
                hit) &&
                hit.inside &&
                std::fabs(hit.u - 0.5f) < 0.001f,
            "center ray missed panel");
    }

    void testResize()
    {
        using namespace rpsui::panel_resize;
        const Constraints constraints{
            .aspectRatio = 1.6f,
            .minimumWidth = 72.0f,
            .maximumWidth = 180.0f,
        };
        const auto drag = begin(Handle::Right, 112.0f, constraints);
        const auto grown = update(drag, 100.0f, 0.0f, constraints);
        require(
            grown.width > 112.0f && grown.width <= 180.0f,
            "right-edge drag did not grow panel");
        require(
            std::fabs(grown.width / grown.height - 1.6f) < 0.001f,
            "resize distorted aspect ratio");
    }

    void testContextualScroll()
    {
        using namespace rpsui::contextual_scroll;
        State state{};
        const auto routed = update(
            state,
            true,
            true,
            { 0.75f, -0.80f });
        require(routed.routesWheel, "owned pointer did not route wheel");
        require(
            routed.wheelX != 0.0f && routed.wheelY != 0.0f,
            "two-axis scroll was lost");
        const auto released = update(state, false, true, {});
        require(!released.routesWheel, "off-panel stick still routed wheel");
    }

    void testStereoPolicy()
    {
        using namespace rpsui::render::EngineStereoSubmissionPolicy;
        require(kSubmitStereoTextureRva == 0x1BABE90, "submit RVA changed");
        require(kFullSubmitCallsiteRva == 0x1D8DB22, "full callsite changed");
        require(kSpecialSubmitCallsiteRva == 0x1D8DC25, "special callsite changed");
        require(kFullSubmitBoundary.size() == 22, "full guard size changed");
    }
}

int main()
{
    try {
        using namespace rpsui::input_policy;
        constexpr auto trigger=1ull<<33, grip=1ull<<2, face=1ull<<7;
        require(capturedMask(0, face, grip, trigger, 0, trigger) == face, "partial chord captured an unowned grip");
        require(capturedMask(0, 0, grip, trigger, grip, trigger) == grip, "cross-hand capture lost left grip");
        require(capturedMask(1, 0, grip, trigger, grip, trigger) == trigger, "cross-hand capture lost right trigger");
        require(capturedMask(1, face, 0, trigger | grip, 0, 0) == face, "held capture disappeared on physical release");
        require(capturedMask(2, face, grip, trigger, grip, trigger) == 0, "invalid hand acquired capture");
        require((capturedMask(1, 0, grip, trigger, 0, trigger) |
            capturedMask(1, 0, 0, face, 0, trigger)) == 0, "separate incomplete chords combined into capture");
        require(chordHeld(grip,trigger,grip,trigger),"cross-hand chord did not capture both members");
        require(!chordHeld(grip,0,grip,trigger),"partial cross-hand chord captured input");
        require(chordHeld(0,trigger|grip|face,0,trigger|grip),"same-hand chord rejected additional buttons");
        require(!chordHeld(0,trigger,0,trigger|grip),"trigger alone was reserved by a chord");
        require(!chordHeld(grip,trigger,0,0),"empty capture reserved input");
        require(axesForButtons(trigger|grip)==6,"captured trigger/grip left analog input exposed");
        require(axesForButtons(face)==0,"face-button capture changed an unrelated analog axis");
        require(axesForButtons(1ull<<32)==1,"thumbstick capture missed its analog axis");
        testNativeWandPointer();
        testApiContract();
        testPointerSelection();
        testClickGate();
        testIntersection();
        testResize();
        testContextualScroll();
        testStereoPolicy();
        std::cout << "RPS UI Framework policy tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "RPS UI Framework policy failure: "
                  << error.what() << '\n';
        return 1;
    }
}
