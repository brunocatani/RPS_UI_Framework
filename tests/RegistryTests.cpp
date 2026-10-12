#include "PCH.h"
#include "FrameworkRuntime.h"
#include "RPSUIPassivePanelApi.h"
#include "RPSUIFocusedPanelApi.h"
// Compile the real input service here so the test can exercise poll publication
// and game-tick dispatch without installing hooks or reading native game state.
#include "InputService.cpp"
#include <iostream>

using namespace rpsui::sdk;
// CommonLib has process-global relocation records even in unused native code.
// Give those inert zero addresses; any address lookup during the test is a bug.
static bool testRunning{};
namespace REL {
    Module::Module() {}
    IDDB::IDDB() {}
    std::size_t IDDB::id2offset(std::uint64_t) const {
        if (testRunning) throw std::runtime_error("test attempted native address resolution");
        return 0;
    }
}
extern "C" const ApiV1* RPSUI_CALL RPSUI_RequestApi(std::uint32_t) noexcept;
extern "C" const CooperationApiV1* RPSUI_CALL RPSUI_RequestCooperationApi(std::uint32_t) noexcept;
extern "C" const RegistryApiV1* RPSUI_CALL RPSUI_RequestRegistryApi(std::uint32_t) noexcept;
extern "C" const PassivePanelApiV1* RPSUI_CALL RPSUI_RequestPassivePanelApi(std::uint32_t) noexcept;
extern "C" const NumberOverlayApiV1* RPSUI_CALL RPSUI_RequestNumberOverlayApi(std::uint32_t) noexcept;
extern "C" const InputApiV1* RPSUI_CALL RPSUI_RequestInputApi() noexcept;
extern "C" const FocusedPanelApiV1* RPSUI_CALL RPSUI_RequestFocusedPanelApi(std::uint32_t, std::uint32_t) noexcept;
namespace rpsui::render::SceneDepthCapture {
    bool requested{};
    void SetCaptureRequested(bool value) noexcept { requested = value; }
}

// These numbers are the released x64 layouts, independent of runtime budgets.
static_assert(sizeof(ApiV1) == 152 && offsetof(ApiV1, reserved) == 88);
static_assert(sizeof(CooperationApiV1) == 48);
static_assert(sizeof(CooperationSnapshotV1) == 5104);
static_assert(offsetof(CooperationSnapshotV1, consumers) == 112);
static_assert(offsetof(CooperationSnapshotV1, panels) == 2928);
static_assert(RPSUI_MAX_CONSUMERS == 16 && RPSUI_MAX_PANELS == 8);

namespace {
    void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
    void RPSUI_CALL render(const PanelRenderFrameV1*, void*) noexcept {}
    unsigned inputCallbacks{};
    std::array<unsigned, rpsui::kInputSubscriberCapacity> inputOrder{};
    void RPSUI_CALL onInput(const InputFrameV1*, void* context) noexcept {
        if (inputCallbacks < inputOrder.size()) inputOrder[inputCallbacks] = *static_cast<const unsigned*>(context);
        ++inputCallbacks;
    }
    PanelRegistrationV1 registration(unsigned id)
    {
        PanelRegistrationV1 value;
        std::snprintf(value.panelId, sizeof(value.panelId), "test.panel.%u", id);
        std::snprintf(value.displayName, sizeof(value.displayName), "Panel %u", id);
        value.flags = unsigned(PanelFlagV1::Transparent) | unsigned(PanelFlagV1::FixedSize);
        value.renderCallback = &render;
        value.sortOrder = -static_cast<int>(id);
        return value;
    }

    void testFocusedPanels()
    {
        const auto* focused = RPSUI_RequestFocusedPanelApi(1,0);
        const auto* api = RPSUI_RequestApi(1);
        const auto* coop = RPSUI_RequestCooperationApi(1);
        const auto* input = RPSUI_RequestInputApi();
        require(focused && focused->major==1 && focused->minor==0 && focused->structSize==24,
            "focused service negotiation failed");
        require(!RPSUI_RequestFocusedPanelApi(0,0) && !RPSUI_RequestFocusedPanelApi(2,0) &&
            !RPSUI_RequestFocusedPanelApi(1,1), "focused service accepted unsupported version");
        ConsumerRegistrationV1 consumer;
        std::strcpy(consumer.consumerId,"focused-test"); std::strcpy(consumer.displayName,"Focused test");
        ConsumerHandleV1 owner;
        require(api->registerConsumer(&consumer,&owner)==ResultV1::Ok,"focused owner failed");
        auto reg=registration(2000);
        std::uint64_t a{},b{};
        require(focused->registerPanel(owner.ownerToken,nullptr,&a)==ResultV1::InvalidArgument,
            "focused null registration accepted");
        reg.flags=0;
        require(focused->registerPanel(owner.ownerToken,&reg,&a)==ResultV1::InvalidArgument,
            "focused resizable panel accepted");
        reg.flags=unsigned(PanelFlagV1::FixedSize)|unsigned(PanelFlagV1::Transparent);
        reg.defaultPhysicalWidth=80; reg.minimumPhysicalWidth=80; reg.maximumPhysicalWidth=80;
        require(focused->registerPanel(owner.ownerToken,&reg,&a)==ResultV1::Ok,"focused A failed");
        std::strcpy(reg.panelId,"focused-B");
        require(focused->registerPanel(owner.ownerToken,&reg,&b)==ResultV1::Ok,"focused B failed");
        const auto present=[&](std::uint64_t handle,float x) {
            PanelPresentationV1 p; p.open=1; p.pose.center[0]=x;
            p.pose.physicalWidth=80; p.pose.physicalHeight=50;
            require(api->submitPanelPresentation(owner.ownerToken,handle,&p)==ResultV1::Ok,"focused pose failed");
            PanelStateV1 state;
            require(api->getPanelState(owner.ownerToken,handle,&state)==ResultV1::Ok && state.pose.center[0]==x,
                "focused panel moved from its anchor");
        };
        present(a,-100); present(b,100);
        auto& runtime=rpsui::FrameworkRuntime::get();
        auto& inputState=rpsui::input::state();
        InputFrameV1 frame; frame.ready=true; frame.sequence=100000;
        for(unsigned side=0;side<2;++side) {
            frame.hands[side].valid=true;
            frame.hands[side].position[0]=side?100.f:-100.f;
            frame.hands[side].position[1]=-100;
            frame.hands[side].forward[1]=1;
        }
        const auto tick=[&] { ++frame.sequence; inputState.frame=frame; inputState.allowPollCapture=true;
            runtime.handleInputFrame(frame); };
        const auto get=[&](std::uint64_t handle) {
            PanelStateV1 state;
            require(api->getPanelState(owner.ownerToken,handle,&state)==ResultV1::Ok,"focused state missing");
            return state;
        };
        // An invalid frame resets prior legacy test focus without accessing game state.
        runtime.handleInputFrame({}); tick(); tick();
        require(!get(a).pointerValid && get(b).pointerValid,"two focused cursors were routed");
        require(input->capturedButtons(0,0,0)==0 && (input->capturedButtons(1,0,0)&(1ull<<32)),
            "unselected hand captured locomotion");
        frame.hands[1].pressed=1ull<<33; tick();
        require(get(b).primaryDown,"focused neutral-to-click transition missing");
        frame.hands[1].forward[0]=1; frame.hands[1].forward[1]=0; tick();
        require(!get(b).pointerValid && !get(b).primaryDown,"focused drag survived cursor exit");
        require(!(input->capturedButtons(1,0,1ull<<33)&(1ull<<32)) &&
            (input->capturedButtons(1,0,1ull<<33)&(1ull<<33)),
            "cursor exit either retained the stick or leaked a held click");
        frame.hands[1].pressed=0; tick();
        require(!input->capturedButtons(1,0,0),"released focused click remained captured");
        frame.hands[0].pressed=1ull<<33; tick();
        require(get(a).primaryDown,"left-hand focused click missing");
        frame.hands[0].position[0]=100; tick();
        require(get(b).pointerValid && !get(b).primaryDown,"held click transferred to another conversation");
        frame.hands[0].pressed=0; tick(); frame.hands[0].pressed=1ull<<33; tick();
        require(get(b).primaryDown,"new panel did not rearm after neutral");
        auto legacy=registration(2001);std::uint64_t legacyHandle{};
        require(api->registerPanel(owner.ownerToken,&legacy,&legacyHandle)==ResultV1::Ok,"legacy coexistence registration failed");
        PanelPresentationV1 legacyPose;legacyPose.open=1;legacyPose.pose.center[0]=400;
        require(api->submitPanelPresentation(owner.ownerToken,legacyHandle,&legacyPose)==ResultV1::Ok,"legacy coexistence pose failed");
        runtime.handleInputFrame({});
        frame.hands[0].pressed=0;frame.hands[0].forward[0]=1;frame.hands[0].forward[1]=0;
        frame.hands[1].position[0]=400;frame.hands[1].forward[0]=0;frame.hands[1].forward[1]=1;
        tick();tick();
        require(get(legacyHandle).pointerValid,"legacy pointer ownership changed");
        frame.hands[0].position[0]=-100;frame.hands[0].forward[0]=0;frame.hands[0].forward[1]=1;
        frame.hands[0].pressed=1ull<<33;tick();
        require(get(legacyHandle).pointerValid && !get(a).pointerValid && !input->capturedButtons(0,1ull<<33,0),
            "provisional focused hand captured input while the visible cursor stayed on a legacy panel");
        require(coop->unregisterConsumerSafely(owner.ownerToken)==ResultV1::Ok,"focused cleanup failed");
        frame.hands[0].pressed=0; tick();
        require(!input->capturedButtons(0,0,0) && !input->capturedButtons(1,0,0),"focused retirement leaked capture");
        inputState.allowPollCapture=false;
    }
}

int main() try
{
    testRunning = true;
    std::cerr << "Checking exported registry and input services...\n";
    const auto* api = RPSUI_RequestApi(1);
    const auto* coop = RPSUI_RequestCooperationApi(1);
    const auto* directory = RPSUI_RequestRegistryApi(1);
    const auto* passive = RPSUI_RequestPassivePanelApi(1);
    const auto* numbers = RPSUI_RequestNumberOverlayApi(1);
    require(api && coop && directory && passive && numbers, "published service missing");
    for (auto version : {0u, 2u, 0xFFFFFFFFu}) {
        require(!RPSUI_RequestApi(version) && !RPSUI_RequestCooperationApi(version) &&
            !RPSUI_RequestRegistryApi(version) && !RPSUI_RequestPassivePanelApi(version) &&
            !RPSUI_RequestNumberOverlayApi(version), "unsupported version accepted");
    }
    auto& runtime = rpsui::FrameworkRuntime::get();
    runtime.setRendererReady(true);
    require(!api->isFrameworkReady(), "host ready without installed input hooks");
    const auto* inputApi = RPSUI_RequestInputApi();
    std::cerr << "Checking input capacity and publication...\n";
    require(inputApi && inputApi->version == 1 && !inputApi->subscribe(nullptr, nullptr), "input negotiation/validation changed");
    std::array<std::uint64_t, rpsui::kInputSubscriberCapacity> subscriptions{};
    std::array<unsigned, rpsui::kInputSubscriberCapacity> inputIds{};
    for (unsigned index = 0; index < subscriptions.size(); ++index) {
        auto& token = subscriptions[index]; inputIds[index] = index;
        token = inputApi->subscribe(&onInput, &inputIds[index]);
        require(token != 0, "input subscriptions hit old limit");
        InputCaptureV1 capture; capture.buttons[0] = 1ull << 33;
        require(inputApi->capture(token, &capture), "high-slot input capture failed");
    }
    require(!inputApi->subscribe(&onInput, nullptr), "input subscription budget not enforced");
    require(!inputApi->capturedButtons(0, ~0ull, ~0ull), "input escaped outside gameplay");
    auto& inputState = rpsui::input::state();
    inputState.allowPollCapture = true;
    require(inputApi->capturedButtons(0, ~0ull, ~0ull) == (1ull << 33), "active capture masks not published");
    for (unsigned i = 0; i + 1 < subscriptions.size(); ++i) {
        InputCaptureV1 clear;
        require(inputApi->capture(subscriptions[i], &clear), "capture clear failed");
    }
    require(inputApi->capturedButtons(0, ~0ull, ~0ull) == (1ull << 33), "last capture word omitted");
    InputCaptureV1 chord;
    chord.chord[0] = 1ull << 2; chord.chord[1] = 1ull << 7;
    require(inputApi->capture(subscriptions.back(), &chord), "chord publication failed");
    require(!inputApi->capturedButtons(0, 1ull << 2, 0) &&
        inputApi->capturedButtons(0, 1ull << 2, 1ull << 7) == (1ull << 2), "high-slot chord changed semantics");
    rpsui::input::update(); // Session is false: no native game access.
    require(inputCallbacks == subscriptions.size(), "input dispatch truncated");
    require(inputOrder == inputIds, "input callback order changed");
    require(inputApi->unsubscribe(subscriptions[0]), "first input slot could not retire");
    subscriptions[0] = inputApi->subscribe(&onInput, &inputIds[0]);
    require(subscriptions[0], "retired input slot not reusable");
    inputCallbacks = 0;
    rpsui::input::update();
    require(inputOrder == inputIds && inputCallbacks == subscriptions.size(), "reused slot changed callback order");
    inputState.allowPollCapture = true;
    require(!inputApi->capturedButtons(0, ~0ull, ~0ull), "inactive session retained input captures");
    inputState.allowPollCapture = false;
    for (const auto token : subscriptions) require(inputApi->unsubscribe(token), "input cleanup failed");
    const auto replacement = inputApi->subscribe(&onInput, nullptr);
    require(replacement > subscriptions.back() && inputApi->unsubscribe(replacement), "input slots not reusable");
    for (const auto& bits : inputState.activeMasks) require(!bits.load(), "input cleanup retained active mask slots");
    RegistryQueryV1 query;
    RegistryPageV1 page;
    require(directory->getPage(&query, &page) == ResultV1::Ok, "empty census failed");
    require(page.consumerCapacity == 256 && page.panelCapacity == 1024 && page.inputSubscriberCapacity == 256 &&
        !page.totalPanels, "wrong runtime capacity");
    require(directory->getPage(nullptr, &page) == ResultV1::InvalidArgument &&
        directory->getPage(&query, nullptr) == ResultV1::InvalidArgument, "null page accepted");
    query.structSize = 0;
    require(directory->getPage(&query, &page) == ResultV1::InvalidArgument, "undersized query accepted");
    query = {}; page.structSize = 0;
    require(directory->getPage(&query, &page) == ResultV1::InvalidArgument, "undersized output accepted");
    page = {}; query.apiVersion = 9;
    require(directory->getPage(&query, &page) == ResultV1::VersionMismatch, "wrong query version accepted");
    query = {}; page.apiVersion = 9;
    require(directory->getPage(&query, &page) == ResultV1::VersionMismatch, "wrong output version accepted");
    page = {};

    auto batch = std::make_unique<rpsui::RenderPanelBatch>();
    const auto measureIdle = [&] {
        InputFrameV1 frame; frame.ready = true;
        const auto start = std::chrono::steady_clock::now();
        for (unsigned i = 0; i < 20000; ++i) {
            ++frame.sequence;
            runtime.handleInputFrame(frame);
            runtime.snapshotRenderPanels(*batch);
        }
        return std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - start).count() / 20000;
    };
    const auto emptyRegistryMicros = measureIdle();

    std::array<ConsumerHandleV1, rpsui::kConsumerCapacity> owners;
    for (unsigned i = 0; i < owners.size(); ++i) {
        ConsumerRegistrationV1 consumer;
        std::snprintf(consumer.consumerId, sizeof(consumer.consumerId), "test.consumer.%u", i);
        std::snprintf(consumer.displayName, sizeof(consumer.displayName), "Consumer %u", i);
        require(api->registerConsumer(&consumer, &owners[i]) == ResultV1::Ok, "consumer registration hit old limit");
    }
    ConsumerRegistrationV1 extraOwner;
    std::strcpy(extraOwner.consumerId, "test.overflow"); std::strcpy(extraOwner.displayName, "Overflow");
    ConsumerHandleV1 rejected;
    require(api->registerConsumer(&extraOwner, &rejected) == ResultV1::CapacityReached && !rejected.ownerToken,
        "consumer budget not enforced");

    std::array<std::uint64_t, rpsui::kPanelCapacity> handles{};
    for (unsigned i = 0; i < handles.size(); ++i) {
        auto panel = registration(i);
        const auto owner = owners[i / 4].ownerToken;
        ResultV1 result;
        if (i % 4 == 0) {
            CooperativePanelRegistrationV1 contract; contract.panel = panel;
            PanelAgreementV1 agreement;
            result = coop->registerPanel(owner, &contract, &agreement);
            handles[i] = agreement.panelHandle;
        } else if (i % 4 == 1) result = passive->registerPanel(owner, &panel, &handles[i]);
        else if (i % 4 == 2) result = api->registerPanel(owner, &panel, &handles[i]);
        else {
            NumberOverlayRegistrationV1 number;
            std::strcpy(number.overlayId, panel.panelId); std::strcpy(number.displayName, panel.displayName);
            result = numbers->registerOverlay(owner, &number, &handles[i]);
        }
        require(result == ResultV1::Ok && handles[i], "mixed legacy registrations hit old eight-panel limit");
    }
    auto extraPanel = registration(2000);
    std::uint64_t overflow = 123;
    require(api->registerPanel(owners[0].ownerToken, &extraPanel, &overflow) == ResultV1::CapacityReached && !overflow,
        "panel budget not enforced");

    struct GuardedSnapshot { CooperationSnapshotV1 snapshot; std::uint64_t canary{0x0123456789ABCDEFull}; } old;
    require(coop->getSnapshot(&old.snapshot) == ResultV1::Ok && old.canary == 0x0123456789ABCDEFull &&
        old.snapshot.consumerCount == 16 && old.snapshot.panelCount == 8, "legacy diagnostic buffer overwritten");
    require(std::is_sorted(std::begin(old.snapshot.panels), std::end(old.snapshot.panels), [](const auto& a, const auto& b) {
        return a.sortOrder != b.sortOrder ? a.sortOrder < b.sortOrder : a.panelHandle < b.panelHandle;
    }), "legacy composition ordering changed");
    unsigned consumerCount = 0, panelCount = 0;
    std::uint64_t revision = 0;
    do {
        require(directory->getPage(&query, &page) == ResultV1::Ok, "registry page failed");
        require(!revision || revision == page.registryRevision, "read changed registry revision");
        revision = page.registryRevision;
        for (unsigned i = 0; i < page.consumerCount; ++i)
            require(page.consumers[i].ownerToken == owners[consumerCount++].ownerToken, "consumer pagination skipped or duplicated");
        for (unsigned i = 0; i < page.panelCount; ++i)
            require(page.panels[i].panelHandle == handles[panelCount++], "panel pagination skipped or duplicated");
        query.afterOwnerToken = page.nextOwnerToken; query.afterPanelHandle = page.nextPanelHandle;
    } while (page.moreConsumers || page.morePanels);
    require(consumerCount == owners.size() && panelCount == handles.size(), "full registry not discoverable");

    runtime.snapshotRenderPanels(*batch);
    require(batch->empty() && !runtime.hasOpenPanels(), "hidden registrations produced render work");
    const auto hiddenRegistryMicros = measureIdle();
    std::cout << "CPU input + render snapshot (microseconds/tick, no GPU): empty=" << emptyRegistryMicros
        << ", 1024 hidden=" << hiddenRegistryMicros << '\n';
    auto present = [&](unsigned i, bool open) {
        PanelPresentationV1 value;
        value.open = open;
        value.pose.center[0] = static_cast<float>(i) * 200;
        require(api->submitPanelPresentation(owners[i / 4].ownerToken, handles[i], &value) == ResultV1::Ok, "present failed");
    };
    // This is the failed backpack sequence: panels beyond the first eight open normally.
    present(8, true); present(9, true); present(10, true);
    runtime.snapshotRenderPanels(*batch);
    require(batch->count == 3 && rpsui::render::SceneDepthCapture::requested, "ninth/tenth panel excluded from rendering");
    require(batch->panels[0].panelHandle == handles[10], "expanded render order changed");
    InputFrameV1 pointing; pointing.ready = true; pointing.sequence = 50000;
    pointing.hands[0].valid = true;
    pointing.hands[0].position[0] = 2000; pointing.hands[0].position[1] = -100;
    pointing.hands[0].forward[1] = 1;
    runtime.handleInputFrame(pointing);
    PanelStateV1 selected;
    require(api->getPanelState(owners[2].ownerToken, handles[10], &selected) == ResultV1::Ok && selected.pointerValid,
        "input routing skipped panel beyond legacy limit");
    present(8, false); present(9, false); present(10, false);
    runtime.snapshotRenderPanels(*batch);
    require(batch->empty() && !rpsui::render::SceneDepthCapture::requested, "closing panels retained render/depth work");
    require(api->getPanelState(owners[2].ownerToken, handles[10], &selected) == ResultV1::Ok && !selected.pointerValid,
        "closed panel retained pointer state");

    InputFrameV1 input; input.ready = 1;
    runtime.handleInputFrame(input);
    for (unsigned i = 0; i < handles.size(); ++i) {
        if (i % 4 != 3) present(i, true);
        else {
            NumberOverlayPresentationV1 value; value.open = 1;
            value.pose.physicalWidth = 3; value.pose.physicalHeight = 3.f * 480 / 1024;
            require(numbers->submit(owners[i / 4].ownerToken, handles[i], &value) == ResultV1::Ok, "overlay present failed");
        }
    }
    runtime.snapshotRenderPanels(*batch);
    require(batch->count == handles.size(), "render batch silently truncated at expanded capacity");
    const auto staleGate = std::find_if(batch->begin(), batch->end(), [&](const auto& entry) {
        return entry.panelHandle == handles[0];
    })->callbackGate;
    {
        rpsui::PanelCallbackScope active(staleGate);
        require(static_cast<bool>(active), "callback admission failed");
        require(coop->unregisterConsumerSafely(owners[0].ownerToken) == ResultV1::CallbackBusy, "active callback did not delay retirement");
        runtime.snapshotRenderPanels(*batch);
        require(batch->count == handles.size() - 4, "retiring owner remained visible");
    }
    require(coop->unregisterConsumerSafely(owners[0].ownerToken) == ResultV1::Ok, "callback did not drain");
    require(!rpsui::PanelCallbackScope(staleGate), "retired callback admitted stale snapshot");
    // Removal shifts registry indices; other owners must retain exact open identities.
    runtime.snapshotRenderPanels(*batch);
    require(batch->count == handles.size() - 4, "registry compaction lost active panels");
    for (const auto& entry : *batch) require(entry.panelHandle > handles[3], "removed panel reappeared after compaction");
    query = {};
    require(directory->getPage(&query, &page) == ResultV1::Ok && page.registryRevision > revision &&
        page.totalConsumers == owners.size() - 1 && page.totalPanels == handles.size() - 4, "census did not reflect retirement");
    require(api->registerConsumer(&extraOwner, &rejected) == ResultV1::Ok, "consumer slot not reusable");
    require(api->registerPanel(rejected.ownerToken, &extraPanel, &overflow) == ResultV1::Ok && overflow > handles.back(),
        "panel slot not reusable or identity recycled");
    for (unsigned i = 1; i < owners.size(); ++i)
        require(coop->unregisterConsumerSafely(owners[i].ownerToken) == ResultV1::Ok, "owner cleanup failed");
    require(coop->unregisterConsumerSafely(rejected.ownerToken) == ResultV1::Ok, "replacement cleanup failed");
    runtime.snapshotRenderPanels(*batch);
    require(batch->empty() && !runtime.hasOpenPanels() && !rpsui::render::SceneDepthCapture::requested, "final cleanup retained work");
    testFocusedPanels();
    std::cout << "256 consumers / 1024 mixed panels / 256 input subscribers: legacy ABI, pagination, render capacity and safe retirement passed.\n";
    return 0;
}
catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
