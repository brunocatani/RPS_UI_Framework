#include "PCH.h"
#include "FrameworkRuntime.h"
#include <RPS_UI_Framework/Client.h>
#include <RPS_UI_Framework/PassivePanel.h>
#include <RPS_UI_Framework/FocusedPanel.h>
#include <RPS_UI_Framework/WorldDraw.h>
// Compile the real input service here so the test can exercise poll publication
// and game-tick dispatch without installing hooks or reading native game state.
#include "InputService.cpp"
#include <iostream>

using namespace rpsui::api;
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
extern "C" Result RPSUI_CALL RPSUI_QueryInterfaceV1(InterfaceId, std::uint32_t, std::uint32_t, std::uint32_t,
    const InterfaceDescriptorV1**) noexcept;
extern "C" const void* RPSUI_CALL RPSUI_RequestApi(std::uint32_t) noexcept;
extern "C" const void* RPSUI_CALL RPSUI_RequestCooperationApi(std::uint32_t) noexcept;
extern "C" const void* RPSUI_CALL RPSUI_RequestRegistryApi(std::uint32_t) noexcept;
extern "C" const void* RPSUI_CALL RPSUI_RequestPassivePanelApi(std::uint32_t) noexcept;
extern "C" const void* RPSUI_CALL RPSUI_RequestNumberOverlayApi(std::uint32_t) noexcept;
extern "C" const void* RPSUI_CALL RPSUI_RequestWorldDrawApi(std::uint32_t) noexcept;
extern "C" const void* RPSUI_CALL RPSUI_RequestInputApi() noexcept;
extern "C" const void* RPSUI_CALL RPSUI_RequestFocusedPanelApi(std::uint32_t, std::uint32_t) noexcept;
namespace rpsui::render::SceneDepthCapture {
    bool requested{};
    void SetCaptureRequested(bool value) noexcept { requested = value; }
}

namespace {
    void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
    template <class Table>
    const Table* served() noexcept
    {
        const Table* table{};
        return acquire(&RPSUI_QueryInterfaceV1, table) == Result::Ok ? table : nullptr;
    }
    void RPSUI_CALL render(const framework::PanelRenderFrame*, void*) noexcept {}
    unsigned inputCallbacks{};
    std::array<unsigned, rpsui::kInputSubscriberCapacity> inputOrder{};
    void RPSUI_CALL onInput(const input::Frame*, void* context) noexcept {
        if (inputCallbacks < inputOrder.size()) inputOrder[inputCallbacks] = *static_cast<const unsigned*>(context);
        ++inputCallbacks;
    }
    framework::PanelRegistration registration(unsigned id)
    {
        framework::PanelRegistration value;
        std::snprintf(value.panelId, sizeof(value.panelId), "test.panel.%u", id);
        std::snprintf(value.displayName, sizeof(value.displayName), "Panel %u", id);
        value.flags = unsigned(framework::PanelFlag::Transparent) | unsigned(framework::PanelFlag::FixedSize);
        value.renderCallback = &render;
        value.sortOrder = -static_cast<int>(id);
        return value;
    }

    void testFocusedPanels()
    {
        const auto* focused = served<focused_panel::Api>();
        const auto* api = served<framework::Api>();
        const auto* coop = served<cooperation::Api>();
        const auto* input = served<rpsui::api::input::Api>();
        require(focused && api && coop && input, "focused service negotiation failed");
        framework::ConsumerRegistration consumer;
        std::strcpy(consumer.consumerId,"focused-test"); std::strcpy(consumer.displayName,"Focused test");
        framework::ConsumerHandle owner;
        require(api->registerConsumer(&consumer,&owner)==Result::Ok,"focused owner failed");
        auto reg=registration(2000);
        std::uint64_t a{},b{};
        require(focused->registerPanel(owner.ownerToken,nullptr,&a)==Result::InvalidArgument,
            "focused null registration accepted");
        reg.flags=0;
        require(focused->registerPanel(owner.ownerToken,&reg,&a)==Result::InvalidArgument,
            "focused resizable panel accepted");
        reg.flags=unsigned(framework::PanelFlag::FixedSize)|unsigned(framework::PanelFlag::Transparent);
        reg.defaultPhysicalWidth=80; reg.minimumPhysicalWidth=80; reg.maximumPhysicalWidth=80;
        require(focused->registerPanel(owner.ownerToken,&reg,&a)==Result::Ok,"focused A failed");
        std::strcpy(reg.panelId,"focused-B");
        require(focused->registerPanel(owner.ownerToken,&reg,&b)==Result::Ok,"focused B failed");
        const auto present=[&](std::uint64_t handle,float x) {
            framework::PanelPresentation p; p.open=1; p.pose.center[0]=x;
            p.pose.physicalWidth=80; p.pose.physicalHeight=50;
            require(api->submitPanelPresentation(owner.ownerToken,handle,&p)==Result::Ok,"focused pose failed");
            framework::PanelState state;
            require(api->getPanelState(owner.ownerToken,handle,&state)==Result::Ok && state.pose.center[0]==x,
                "focused panel moved from its anchor");
        };
        present(a,-100); present(b,100);
        auto& runtime=rpsui::FrameworkRuntime::get();
        auto& inputState=rpsui::input::state();
        input::Frame frame; frame.ready=true; frame.sequence=100000;
        for(unsigned side=0;side<2;++side) {
            frame.hands[side].valid=true;
            frame.hands[side].position[0]=side?100.f:-100.f;
            frame.hands[side].position[1]=-100;
            frame.hands[side].forward[1]=1;
        }
        const auto tick=[&] { ++frame.sequence; inputState.frame=frame; inputState.allowPollCapture=true;
            runtime.handleInputFrame(frame); };
        const auto get=[&](std::uint64_t handle) {
            framework::PanelState state;
            require(api->getPanelState(owner.ownerToken,handle,&state)==Result::Ok,"focused state missing");
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
        require(api->registerPanel(owner.ownerToken,&legacy,&legacyHandle)==Result::Ok,"legacy coexistence registration failed");
        framework::PanelPresentation legacyPose;legacyPose.open=1;legacyPose.pose.center[0]=400;
        require(api->submitPanelPresentation(owner.ownerToken,legacyHandle,&legacyPose)==Result::Ok,"legacy coexistence pose failed");
        runtime.handleInputFrame({});
        frame.hands[0].pressed=0;frame.hands[0].forward[0]=1;frame.hands[0].forward[1]=0;
        frame.hands[1].position[0]=400;frame.hands[1].forward[0]=0;frame.hands[1].forward[1]=1;
        tick();tick();
        require(get(legacyHandle).pointerValid,"legacy pointer ownership changed");
        frame.hands[0].position[0]=-100;frame.hands[0].forward[0]=0;frame.hands[0].forward[1]=1;
        frame.hands[0].pressed=1ull<<33;tick();
        require(get(legacyHandle).pointerValid && !get(a).pointerValid && !input->capturedButtons(0,1ull<<33,0),
            "provisional focused hand captured input while the visible cursor stayed on a legacy panel");
        require(coop->unregisterConsumerSafely(owner.ownerToken)==Result::Ok,"focused cleanup failed");
        frame.hands[0].pressed=0; tick();
        require(!input->capturedButtons(0,0,0) && !input->capturedButtons(1,0,0),"focused retirement leaked capture");
        inputState.allowPollCapture=false;
    }
}

int main() try
{
    testRunning = true;
    std::cerr << "Checking exported registry and input services...\n";
    const auto* api = served<framework::Api>();
    const auto* coop = served<cooperation::Api>();
    const auto* directory = served<registry::Api>();
    const auto* passive = served<passive_panel::Api>();
    const auto* numbers = served<number_overlay::Api>();
    require(api && coop && directory && passive && numbers && served<world_draw::Api>(), "published service missing");
    // Every pre-2.0 way in refuses: retired exports and major-1 requests give no table.
    require(!RPSUI_RequestApi(1) && !RPSUI_RequestCooperationApi(1) && !RPSUI_RequestRegistryApi(1) &&
        !RPSUI_RequestPassivePanelApi(1) && !RPSUI_RequestNumberOverlayApi(1) && !RPSUI_RequestWorldDrawApi(1) &&
        !RPSUI_RequestInputApi() && !RPSUI_RequestFocusedPanelApi(1, 0), "retired export returned a table");
    for (const auto id : {InterfaceId::Framework, InterfaceId::Cooperation, InterfaceId::FocusedPanel, InterfaceId::Input,
             InterfaceId::NumberOverlay, InterfaceId::PassivePanel, InterfaceId::Registry, InterfaceId::WorldDraw}) {
        const InterfaceDescriptorV1* refused{};
        require(RPSUI_QueryInterfaceV1(id, 1, 0, 8, &refused) == Result::UnsupportedMajor && !refused, "major 1 was served");
    }
    auto& runtime = rpsui::FrameworkRuntime::get();
    runtime.setRendererReady(true);
    require(!api->isFrameworkReady(), "host ready without installed input hooks");
    const auto* inputApi = served<input::Api>();
    std::cerr << "Checking input capacity and publication...\n";
    require(inputApi && !inputApi->subscribe(nullptr, nullptr), "input negotiation/validation changed");
    std::array<std::uint64_t, rpsui::kInputSubscriberCapacity> subscriptions{};
    std::array<unsigned, rpsui::kInputSubscriberCapacity> inputIds{};
    for (unsigned index = 0; index < subscriptions.size(); ++index) {
        auto& token = subscriptions[index]; inputIds[index] = index;
        token = inputApi->subscribe(&onInput, &inputIds[index]);
        require(token != 0, "input subscriptions hit old limit");
        input::Capture capture; capture.buttons[0] = 1ull << 33;
        require(inputApi->capture(token, &capture), "high-slot input capture failed");
    }
    require(!inputApi->subscribe(&onInput, nullptr), "input subscription budget not enforced");
    require(!inputApi->capturedButtons(0, ~0ull, ~0ull), "input escaped outside gameplay");
    auto& inputState = rpsui::input::state();
    inputState.allowPollCapture = true;
    require(inputApi->capturedButtons(0, ~0ull, ~0ull) == (1ull << 33), "active capture masks not published");
    for (unsigned i = 0; i + 1 < subscriptions.size(); ++i) {
        input::Capture clear;
        require(inputApi->capture(subscriptions[i], &clear), "capture clear failed");
    }
    require(inputApi->capturedButtons(0, ~0ull, ~0ull) == (1ull << 33), "last capture word omitted");
    input::Capture chord;
    chord.chord[0] = 1ull << 2; chord.chord[1] = 1ull << 7;
    require(inputApi->capture(subscriptions.back(), &chord), "chord publication failed");
    require(!inputApi->capturedButtons(0, 1ull << 2, 0) &&
        inputApi->capturedButtons(0, 1ull << 2, 1ull << 7) == (1ull << 2), "high-slot chord changed semantics");
    input::KeyboardRead keyboardRead;
    require(!inputApi->captureKeyboard(subscriptions[0], nullptr), "null keyboard read accepted");
    require(!inputApi->captureKeyboard(subscriptions[0], &keyboardRead) && !inputState.keyboardOwner,
        "keyboard captured while input was not ready or the service was absent");
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
    registry::Query query;
    registry::Page page;
    require(directory->getPage(&query, &page) == Result::Ok, "empty census failed");
    require(page.consumerCapacity == 256 && page.panelCapacity == 1024 && page.inputSubscriberCapacity == 256 &&
        !page.totalPanels, "wrong runtime capacity");
    require(directory->getPage(nullptr, &page) == Result::InvalidArgument &&
        directory->getPage(&query, nullptr) == Result::InvalidArgument, "null page accepted");
    query.size = 0;
    require(directory->getPage(&query, &page) == Result::InvalidArgument, "undersized query accepted");
    query = {}; page.size = 0;
    require(directory->getPage(&query, &page) == Result::InvalidArgument, "undersized output accepted");
    page = {}; query.version = 9;
    require(directory->getPage(&query, &page) == Result::VersionMismatch, "wrong query version accepted");
    query = {}; page.version = 9;
    require(directory->getPage(&query, &page) == Result::VersionMismatch, "wrong output version accepted");
    page = {};

    auto batch = std::make_unique<rpsui::RenderPanelBatch>();
    const auto measureIdle = [&] {
        input::Frame frame; frame.ready = true;
        const auto start = std::chrono::steady_clock::now();
        for (unsigned i = 0; i < 20000; ++i) {
            ++frame.sequence;
            runtime.handleInputFrame(frame);
            runtime.snapshotRenderPanels(*batch);
        }
        return std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - start).count() / 20000;
    };
    const auto emptyRegistryMicros = measureIdle();

    std::array<framework::ConsumerHandle, rpsui::kConsumerCapacity> owners;
    for (unsigned i = 0; i < owners.size(); ++i) {
        framework::ConsumerRegistration consumer;
        std::snprintf(consumer.consumerId, sizeof(consumer.consumerId), "test.consumer.%u", i);
        std::snprintf(consumer.displayName, sizeof(consumer.displayName), "Consumer %u", i);
        require(api->registerConsumer(&consumer, &owners[i]) == Result::Ok, "consumer registration hit old limit");
    }
    framework::ConsumerRegistration extraOwner;
    std::strcpy(extraOwner.consumerId, "test.overflow"); std::strcpy(extraOwner.displayName, "Overflow");
    framework::ConsumerHandle rejected;
    require(api->registerConsumer(&extraOwner, &rejected) == Result::CapacityReached && !rejected.ownerToken,
        "consumer budget not enforced");

    std::array<std::uint64_t, rpsui::kPanelCapacity> handles{};
    for (unsigned i = 0; i < handles.size(); ++i) {
        auto panel = registration(i);
        const auto owner = owners[i / 4].ownerToken;
        Result result;
        if (i % 4 == 0) {
            cooperation::PanelRegistration contract; contract.panel = panel;
            cooperation::PanelAgreement agreement;
            result = coop->registerPanel(owner, &contract, &agreement);
            handles[i] = agreement.panelHandle;
        } else if (i % 4 == 1) result = passive->registerPanel(owner, &panel, &handles[i]);
        else if (i % 4 == 2) result = api->registerPanel(owner, &panel, &handles[i]);
        else {
            number_overlay::Registration number;
            std::strcpy(number.overlayId, panel.panelId); std::strcpy(number.displayName, panel.displayName);
            result = numbers->registerOverlay(owner, &number, &handles[i]);
        }
        require(result == Result::Ok && handles[i], "mixed legacy registrations hit old eight-panel limit");
    }
    auto extraPanel = registration(2000);
    std::uint64_t overflow = 123;
    require(api->registerPanel(owners[0].ownerToken, &extraPanel, &overflow) == Result::CapacityReached && !overflow,
        "panel budget not enforced");

    struct GuardedSnapshot { cooperation::Snapshot snapshot; std::uint64_t canary{0x0123456789ABCDEFull}; } old;
    require(coop->getSnapshot(&old.snapshot) == Result::Ok && old.canary == 0x0123456789ABCDEFull &&
        old.snapshot.consumerCount == 16 && old.snapshot.panelCount == 8, "legacy diagnostic buffer overwritten");
    require(std::is_sorted(std::begin(old.snapshot.panels), std::end(old.snapshot.panels), [](const auto& a, const auto& b) {
        return a.sortOrder != b.sortOrder ? a.sortOrder < b.sortOrder : a.panelHandle < b.panelHandle;
    }), "legacy composition ordering changed");
    unsigned consumerCount = 0, panelCount = 0;
    std::uint64_t revision = 0;
    do {
        require(directory->getPage(&query, &page) == Result::Ok, "registry page failed");
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
        framework::PanelPresentation value;
        value.open = open;
        value.pose.center[0] = static_cast<float>(i) * 200;
        require(api->submitPanelPresentation(owners[i / 4].ownerToken, handles[i], &value) == Result::Ok, "present failed");
    };
    // This is the failed backpack sequence: panels beyond the first eight open normally.
    present(8, true); present(9, true); present(10, true);
    runtime.snapshotRenderPanels(*batch);
    require(batch->count == 3 && rpsui::render::SceneDepthCapture::requested, "ninth/tenth panel excluded from rendering");
    require(batch->panels[0].panelHandle == handles[10], "expanded render order changed");
    input::Frame pointing; pointing.ready = true; pointing.sequence = 50000;
    pointing.hands[0].valid = true;
    pointing.hands[0].position[0] = 2000; pointing.hands[0].position[1] = -100;
    pointing.hands[0].forward[1] = 1;
    runtime.handleInputFrame(pointing);
    framework::PanelState selected;
    require(api->getPanelState(owners[2].ownerToken, handles[10], &selected) == Result::Ok && selected.pointerValid,
        "input routing skipped panel beyond legacy limit");
    present(8, false); present(9, false); present(10, false);
    runtime.snapshotRenderPanels(*batch);
    require(batch->empty() && !rpsui::render::SceneDepthCapture::requested, "closing panels retained render/depth work");
    require(api->getPanelState(owners[2].ownerToken, handles[10], &selected) == Result::Ok && !selected.pointerValid,
        "closed panel retained pointer state");

    input::Frame input; input.ready = 1;
    runtime.handleInputFrame(input);
    for (unsigned i = 0; i < handles.size(); ++i) {
        if (i % 4 != 3) present(i, true);
        else {
            number_overlay::Presentation value; value.open = 1;
            value.pose.physicalWidth = 3; value.pose.physicalHeight = 3.f * 480 / 1024;
            require(numbers->submit(owners[i / 4].ownerToken, handles[i], &value) == Result::Ok, "overlay present failed");
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
        require(coop->unregisterConsumerSafely(owners[0].ownerToken) == Result::CallbackBusy, "active callback did not delay retirement");
        runtime.snapshotRenderPanels(*batch);
        require(batch->count == handles.size() - 4, "retiring owner remained visible");
    }
    require(coop->unregisterConsumerSafely(owners[0].ownerToken) == Result::Ok, "callback did not drain");
    require(!rpsui::PanelCallbackScope(staleGate), "retired callback admitted stale snapshot");
    // Removal shifts registry indices; other owners must retain exact open identities.
    runtime.snapshotRenderPanels(*batch);
    require(batch->count == handles.size() - 4, "registry compaction lost active panels");
    for (const auto& entry : *batch) require(entry.panelHandle > handles[3], "removed panel reappeared after compaction");
    query = {};
    require(directory->getPage(&query, &page) == Result::Ok && page.registryRevision > revision &&
        page.totalConsumers == owners.size() - 1 && page.totalPanels == handles.size() - 4, "census did not reflect retirement");
    require(api->registerConsumer(&extraOwner, &rejected) == Result::Ok, "consumer slot not reusable");
    require(api->registerPanel(rejected.ownerToken, &extraPanel, &overflow) == Result::Ok && overflow > handles.back(),
        "panel slot not reusable or identity recycled");
    for (unsigned i = 1; i < owners.size(); ++i)
        require(coop->unregisterConsumerSafely(owners[i].ownerToken) == Result::Ok, "owner cleanup failed");
    require(coop->unregisterConsumerSafely(rejected.ownerToken) == Result::Ok, "replacement cleanup failed");
    runtime.snapshotRenderPanels(*batch);
    require(batch->empty() && !runtime.hasOpenPanels() && !rpsui::render::SceneDepthCapture::requested, "final cleanup retained work");
    testFocusedPanels();
    std::cout << "256 consumers / 1024 mixed panels / 256 input subscribers: discovery, pagination, render capacity and safe retirement passed.\n";
    return 0;
}
catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
