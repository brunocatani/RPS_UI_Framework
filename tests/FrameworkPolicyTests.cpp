#include "ContextualScrollPolicy.h"
#include "PanelResizePolicy.h"
#include "PointerClickGate.h"
#include "PointerHandSelection.h"
#include "PointerPanelIntersection.h"
#include "RPSUIFrameworkApi.h"
#include "render/EngineStereoSubmissionPolicy.h"

#include <chrono>
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
