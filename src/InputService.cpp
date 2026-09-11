#include "PCH.h"
#include "InputService.h"
#include "InputCapturePolicy.h"
#include "NativeWandPose.h"
#include "FrameworkRuntime.h"
#include "PanelCooperation.h"
#include "Logger.h"
#include "vrcf/VRControllersManager.h"

namespace rpsui::input {
namespace {
using namespace rock::provider;
constexpr std::uint64_t trigger=1ull<<33,grip=1ull<<2,accept=1ull<<7;
constexpr unsigned maxClients=16;
struct Client {
 std::uint64_t token{},rockOwner{};sdk::InputCallbackV1 callback{};void* context{};
 std::shared_ptr<PanelCallbackGate> gate;
 bool closing{};std::uint64_t renewed{};
};
struct PollCapture {
 std::atomic_uint64_t buttons[2]{},chord[2]{};
};
struct State {
 std::mutex mutex;std::array<Client,maxClients> clients;
 std::uint64_t nextToken{1};
 std::array<PollCapture,maxClients+1> masks;
 sdk::InputFrameV1 frame;
 RockProviderFrameSnapshot rock;
 std::atomic_bool installed{},session{},useRock{},allowPollCapture{},captureHealthy{true};
 std::atomic_uint32_t devices[2]{vr::k_unTrackedDeviceIndexInvalid,vr::k_unTrackedDeviceIndexInvalid};
 std::atomic_uint64_t raw[2]{};
 std::uint64_t rockOwner{},rockCallback{};
 bool hostCapture[2]{},hostConfig[2]{};
 unsigned failureStage{};
 unsigned handStage[2]{};
};
State& state(){static State s;return s;}
thread_local bool selfRead=false;
using Poll=bool(*)(vr::IVRSystem*,vr::TrackedDeviceIndex_t,vr::VRControllerState_t*,std::uint32_t);
using PollPose=bool(*)(vr::IVRSystem*,vr::ETrackingUniverseOrigin,vr::TrackedDeviceIndex_t,vr::VRControllerState_t*,std::uint32_t,vr::TrackedDevicePose_t*);
Poll originalPoll{};PollPose originalPollPose{};
using GameTick=void(*)(std::uint64_t);
GameTick originalTick{};

bool copyMemory(const void* source,void* target,std::size_t bytes) noexcept {
 const auto address=reinterpret_cast<std::uintptr_t>(source);
 if(address<0x10000 || address>0x00007fffffffffff || bytes>0x00007fffffffffff-address)return false;
 __try{std::memcpy(target,source,bytes);return true;}
 __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
template<class T>bool read(std::uintptr_t source,T& target) noexcept {return copyMemory(reinterpret_cast<void*>(source),&target,sizeof(target));}
bool prefix(std::uintptr_t rva,std::initializer_list<unsigned char> expected) {
 std::array<unsigned char,32> bytes{};
 if(expected.size()>bytes.size() || !copyMemory(reinterpret_cast<void*>(REL::Offset(rva).address()),bytes.data(),expected.size())) {
  log::error("UI input native guard unreadable at RVA {:#x} ({} bytes)",rva,expected.size());return false;
 }
 for(std::size_t i=0;i<expected.size();++i)if(bytes[i]!=expected.begin()[i]) {
  log::error("UI input native guard mismatch at RVA {:#x}+{:#x}: expected {:02x}, actual {:02x}",rva,i,expected.begin()[i],bytes[i]);return false;
 }
 return true;
}
void filter(vr::TrackedDeviceIndex_t device,vr::VRControllerState_t* sample) noexcept {
 auto& s=state();int side=-1;
 for(int i=0;i<2;++i)if(device==s.devices[i].load()){side=i;break;}
 if(side<0 || !sample)return;
 s.raw[side]=sample->ulButtonPressed;
 if(selfRead || s.useRock.load() || !s.allowPollCapture.load())return;
 std::uint64_t mask=0;
 for(const auto& entry:s.masks) {
  mask|=entry.buttons[side].load();
  const auto left=entry.chord[0].load(),right=entry.chord[1].load();
  if(input_policy::chordHeld(s.raw[0].load(),s.raw[1].load(),left,right))mask|=side?right:left;
 }
 sample->ulButtonPressed&=~mask;sample->ulButtonTouched&=~mask;
 const auto axes=input_policy::axesForButtons(mask);
 for(unsigned axis=0;axis<vr::k_unControllerStateAxisCount;++axis)
  if(axes&(1u<<axis))sample->rAxis[axis]={};
}
bool poll(vr::IVRSystem* system,vr::TrackedDeviceIndex_t device,vr::VRControllerState_t* sample,std::uint32_t size) {
 const bool result=originalPoll(system,device,sample,size);if(result && size>=sizeof(*sample))filter(device,sample);return result;
}
bool pollPose(vr::IVRSystem* system,vr::ETrackingUniverseOrigin origin,vr::TrackedDeviceIndex_t device,vr::VRControllerState_t* sample,std::uint32_t size,vr::TrackedDevicePose_t* pose) {
 const bool result=originalPollPose(system,origin,device,sample,size,pose);if(result && size>=sizeof(*sample))filter(device,sample);return result;
}
bool installPolling() {
 if(originalPoll)return true;
 auto* system=vr::VRSystem();if(!system)return false;
 // The live IVRSystem_019 layout is defined by the bundled OpenVR header.
 auto** table=*reinterpret_cast<void***>(system);DWORD protection{};
 if(!VirtualProtect(table+34,2*sizeof(void*),PAGE_EXECUTE_READWRITE,&protection))return false;
 originalPoll=reinterpret_cast<Poll>(table[34]);originalPollPose=reinterpret_cast<PollPose>(table[35]);
 table[34]=reinterpret_cast<void*>(&poll);table[35]=reinterpret_cast<void*>(&pollPose);
 DWORD ignored{};const bool restored=VirtualProtect(table+34,2*sizeof(void*),protection,&ignored)!=0;
 if(!restored)log::error("Input hook protection restoration failed: {}",GetLastError());
 return true;
}
bool blockedMenu() {
 auto* ui=RE::UI::GetSingleton();if(!ui)return true;
 static const std::array<RE::BSFixedString,13> names{"MainMenu","LoadingMenu","PipboyMenu","PauseMenu","Console","FavoritesMenu","ContainerMenu","BarterMenu","Crafting Menu","Dialogue Menu","MessageBoxMenu","Lockpicking Menu","WorkshopMenu"};
 for(const auto& name:names)if(ui->GetMenuOpen(name))return true;
 return false;
}
unsigned nativeHand(std::uintptr_t player,std::uintptr_t offset,sdk::HandInputV1& hand) {
 std::uintptr_t node{};RE::NiTransform transform{};
 if(!read(player+offset,node) || node<0x10000 || node>0x00007fffffffffff || node%alignof(void*))return 1;
 if(!read(node+offsetof(RE::NiAVObject,world),transform))return 2;
 return input_policy::nativeWandPose(transform,hand)?0:3;
}
void publishMask(unsigned slot,const sdk::InputCaptureV1& capture) {
 auto& s=state();for(unsigned hand=0;hand<2;++hand){s.masks[slot].buttons[hand]=capture.buttons[hand];s.masks[slot].chord[hand]=capture.chord[hand];}
}
std::uint64_t registerRockInputOwner() {
 auto* api=RockProviderApi::inst;if(!api)return 0;
 RockProviderConsumerRegistrationV1 registration{};std::snprintf(registration.modName,sizeof(registration.modName),"RPS UI input consumer");
 registration.requestedCapabilities=static_cast<unsigned>(RockProviderConsumerCapabilityV1::HandInputSuppression);
 RockProviderConsumerHandleV1 handle{};
 if(api->registerConsumerV1(&registration,&handle)==RockProviderResultV1::Ok && (handle.grantedCapabilities&registration.requestedCapabilities)==registration.requestedCapabilities)return handle.ownerToken;
 if(handle.ownerToken)(void)api->unregisterConsumerV1(handle.ownerToken);return 0;
}
void ROCK_PROVIDER_CALL providerFrame(const RockProviderFrameSnapshot*,void*) noexcept;
bool connectRock() {
 auto& s=state();
 if(RockProviderApi::initialize(ROCK_PROVIDER_API_VERSION,ROCK_PROVIDER_API_V1_RAW_WAND_THUMBSTICK_TABLE_BYTES)!=0 || !RockProviderApi::inst)
  return GetModuleHandleW(L"ROCK.dll")==nullptr;
 auto* api=RockProviderApi::inst;
 if(!api->registerConsumerV1 || !api->unregisterConsumerV1 || !api->registerFrameCallbackForOwnerV1 || !api->getFrameSnapshot || !api->getRawWandButtonStateV1 || !api->getRawWandThumbstickV1 || !api->setHandInputSuppressionV1 || !api->clearHandInputSuppressionV1)return false;
 RockProviderConsumerRegistrationV1 registration{};std::snprintf(registration.modName,sizeof(registration.modName),"RPS UI input");
 registration.requestedCapabilities=static_cast<unsigned>(RockProviderConsumerCapabilityV1::FrameSnapshots)|static_cast<unsigned>(RockProviderConsumerCapabilityV1::HandInputSuppression);
 RockProviderConsumerHandleV1 handle{};
 if(api->registerConsumerV1(&registration,&handle)==RockProviderResultV1::Ok && (handle.grantedCapabilities&registration.requestedCapabilities)==registration.requestedCapabilities)s.rockOwner=handle.ownerToken;
 else if(handle.ownerToken)(void)api->unregisterConsumerV1(handle.ownerToken);
 if(!s.rockOwner)return false;
 if(api->registerFrameCallbackForOwnerV1(s.rockOwner,providerFrame,nullptr,&s.rockCallback)!=RockProviderResultV1::Ok || !s.rockCallback) {
  (void)api->unregisterConsumerV1(s.rockOwner);s.rockOwner=0;return false;
 }
 return true;
}
bool applyRockCapture(std::uint64_t owner,unsigned slot) {
 auto& s=state();if(!owner)return !s.useRock.load();
 for(unsigned side=0;side<2;++side) {
  const auto hand=side?RockProviderHand::Right:RockProviderHand::Left;
  const auto buttons=s.masks[slot].buttons[side].load();
  const auto left=s.masks[slot].chord[0].load(),right=s.masks[slot].chord[1].load();
  const auto ownChord=side?right:left;
  if(!s.useRock.load() || !s.frame.ready || (!buttons && !ownChord)) {
   (void)RockProviderApi::inst->clearHandInputSuppressionV1(owner,hand);continue;
  }
  RockProviderHandInputSuppressionRequestV1 request{};request.hand=hand;request.leaseFrames=3;
  request.worldGeneration=s.rock.worldGeneration;request.skeletonGeneration=s.rock.skeletonGeneration;request.providerGeneration=s.rock.providerGeneration;
  if(ownChord) {
   request.flags=static_cast<unsigned>(RockProviderHandInputSuppressionFlagV1::ReserveButtonChord);
   request.chordButtonsLow[0]=static_cast<std::uint32_t>(left);request.chordButtonsHigh[0]=static_cast<std::uint32_t>(left>>32);
   request.chordButtonsLow[1]=static_cast<std::uint32_t>(right);request.chordButtonsHigh[1]=static_cast<std::uint32_t>(right>>32);
  }
  if(buttons)request.flags|=static_cast<unsigned>(RockProviderHandInputSuppressionFlagV1::SuppressOpenVrGameInput)|static_cast<unsigned>(RockProviderHandInputSuppressionFlagV1::SuppressConfigModeChord)|static_cast<unsigned>(RockProviderHandInputSuppressionFlagV1::SuppressNativeVats)|static_cast<unsigned>(RockProviderHandInputSuppressionFlagV1::SuppressNativeVans);
  const auto result=RockProviderApi::inst->setHandInputSuppressionV1(owner,&request);
  if(result!=RockProviderResultV1::Ok) {
   s.frame.ready=false;s.allowPollCapture=false;s.captureHealthy=false;
   static bool logged=false;if(!logged){logged=true;log::error("ROCK input capture rejected ({})",static_cast<unsigned>(result));}
  }
 }
 return s.captureHealthy.load();
}
void rockCapture() {
 auto& s=state();if(!s.rockOwner)return;
 std::array<std::uint64_t,maxClients+1> owners{};owners[0]=s.rockOwner;
 {std::scoped_lock lock(s.mutex);for(unsigned i=0;i<maxClients;++i)owners[i+1]=s.clients[i].rockOwner;}
 for(unsigned slot=0;slot<=maxClients;++slot)if(owners[slot])(void)applyRockCapture(owners[slot],slot);
}
void update(const RockProviderFrameSnapshot* provider=nullptr) {
 auto& s=state();++s.frame.sequence;s.frame.seconds=std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
 s.frame.ready=false;s.frame.hands[0]={};s.frame.hands[1]={};
 if(provider)s.rock=*provider;
 const auto source=input_policy::selectSource(s.rockOwner!=0,provider && provider->providerReady);
 s.useRock=s.rockOwner!=0;
 unsigned stage=0;
 if(!s.session.load())stage=1;
 else if(blockedMenu())stage=2;
 else if(!s.captureHealthy.load())stage=6;
 else if(source==input_policy::Source::Unavailable)stage=7;
 else if(source==input_policy::Source::Rock) {
  s.frame.ready=!s.rock.menuBlocking && !s.rock.configBlocking;
  s.frame.leftHanded=s.rock.primaryHand==RockProviderHand::Left;
  for(unsigned side=0;side<2;++side) {
   auto& hand=s.frame.hands[side];const auto& transform=side?s.rock.rightHandTransform:s.rock.leftHandTransform;
   std::copy_n(transform.translate,3,hand.position);std::copy_n(transform.rotate,3,hand.forward);
   const auto physical=side?RockProviderHand::Right:RockProviderHand::Left;bool valid=true;
   for(unsigned button:{1u,2u,7u,32u,33u}){RockProviderRawWandButtonStateV1 raw{};valid&=RockProviderApi::inst->getRawWandButtonStateV1(physical,button,&raw) && raw.available;if(raw.held)hand.pressed|=1ull<<button;}
   (void)RockProviderApi::inst->getRawWandThumbstickV1(physical,&hand.stick[0],&hand.stick[1]);hand.valid=valid;
  }
 }else {
  auto* system=vr::VRSystem();const auto player=reinterpret_cast<std::uintptr_t>(RE::PlayerCharacter::GetSingleton());
  std::uint8_t leftHanded{};
  if(!system || !installPolling())stage=3;
  else if(!player || !read(REL::Offset(0x37d5e48).address(),leftHanded) || leftHanded>1)stage=4;
  else {
   s.frame.leftHanded=leftHanded!=0;
   for(unsigned side=0;side<2;++side) {
    auto& hand=s.frame.hands[side];const bool primary=(side==0)==s.frame.leftHanded;
    const auto device=system->GetTrackedDeviceIndexForControllerRole(side?vr::TrackedControllerRole_RightHand:vr::TrackedControllerRole_LeftHand);s.devices[side]=device;
    vr::VRControllerState_t raw{};
    selfRead=true;const bool valid=device!=vr::k_unTrackedDeviceIndexInvalid && system->GetControllerState(device,&raw,sizeof(raw));selfRead=false;
    const unsigned poseStage=valid?nativeHand(player,primary?0x6f0:0x768,hand):4;
    hand.valid=poseStage==0;hand.pressed=raw.ulButtonPressed;hand.stick[0]=raw.rAxis[0].x;hand.stick[1]=raw.rAxis[0].y;
    if(s.handStage[side]!=poseStage){s.handStage[side]=poseStage;log::info("UI native wand hand={} stage={} (0=ready, 1=node, 2=world read, 3=transform, 4=tracking)",side,poseStage);}
   }
   s.frame.ready=s.frame.hands[0].valid || s.frame.hands[1].valid;
   if(!s.frame.ready)stage=5;
  }
 }
 if(stage!=s.failureStage){s.failureStage=stage;log::info("UI input availability stage={} (0=ready, 1=session, 2=menu, 3=OpenVR, 4=player, 5=wand, 6=capture, 7=ROCK unavailable)",stage);}
 s.allowPollCapture=s.frame.ready;
 FrameworkRuntime::get().handleInputFrame(s.frame);
 std::array<Client,maxClients> callbacks;
 {std::scoped_lock lock(s.mutex);callbacks=s.clients;}
 for(const auto& client:callbacks)if(client.token){PanelCallbackScope scope(client.gate);if(scope)client.callback(&s.frame,client.context);}
 {std::scoped_lock lock(s.mutex);for(unsigned i=0;i<maxClients;++i)if(!s.frame.ready || s.frame.sequence-s.clients[i].renewed>3)publishMask(i+1,{});}
 rockCapture();
}
void safeUpdate(const RockProviderFrameSnapshot* provider=nullptr) noexcept {
 try{update(provider);}catch(...){auto& s=state();s.allowPollCapture=false;s.frame.ready=false;for(unsigned i=0;i<=maxClients;++i)publishMask(i,{});static bool logged=false;if(!logged){logged=true;log::error("UI input frame failed; capture released");}}
}
void ROCK_PROVIDER_CALL providerFrame(const RockProviderFrameSnapshot* snapshot,void*) noexcept {
 // A loaded ROCK owns controller input even while its provider is unavailable.
 // Lifecycle notifications clear UI interaction instead of enabling native input.
 if(snapshot && state().installed.load())safeUpdate(snapshot);
}
void tick(std::uint64_t argument) {
 originalTick(argument);
 auto& s=state();RockProviderFrameSnapshot snapshot;
 if(s.rockCallback && RockProviderApi::inst->getFrameSnapshot(&snapshot) && snapshot.providerReady)return;
 safeUpdate();
}
std::uint64_t RPSUI_CALL subscribe(sdk::InputCallbackV1 callback,void* context) noexcept {
 try{if(!callback)return 0;auto& s=state();std::scoped_lock lock(s.mutex);
  for(auto& client:s.clients)if(!client.token && s.nextToken!=UINT64_MAX){Client value;value.gate=std::make_shared<PanelCallbackGate>();value.token=s.nextToken++;value.rockOwner=s.rockOwner?registerRockInputOwner():0;value.callback=callback;value.context=context;client=std::move(value);return client.token;}
 }catch(...){log::error("UI input subscription failed");}return 0;
}
bool RPSUI_CALL unsubscribe(std::uint64_t token) noexcept {
 auto& s=state();std::scoped_lock lock(s.mutex);
 for(unsigned i=0;i<maxClients;++i)if(s.clients[i].token==token && token){s.clients[i].closing=true;publishMask(i+1,{});if(!s.clients[i].gate->close())return false;if(s.clients[i].rockOwner)(void)RockProviderApi::inst->unregisterConsumerV1(s.clients[i].rockOwner);s.clients[i]={};return true;}return false;
}
bool RPSUI_CALL capture(std::uint64_t token,const sdk::InputCaptureV1* request) noexcept {
 if(!request || request->structSize!=sizeof(*request))return false;
 constexpr auto allowed=(1ull<<1)|(1ull<<2)|(1ull<<7)|(1ull<<32)|(1ull<<33);
 for(unsigned hand=0;hand<2;++hand)if((request->buttons[hand]|request->chord[hand])&~allowed)return false;
 auto& s=state();std::scoped_lock lock(s.mutex);
 for(unsigned i=0;i<maxClients;++i)if(s.clients[i].token==token && token){if(s.clients[i].closing || (s.useRock.load() && !s.clients[i].rockOwner))return false;s.clients[i].renewed=s.frame.sequence;publishMask(i+1,*request);return applyRockCapture(s.clients[i].rockOwner,i+1);}return false;
}
}
bool start() noexcept {
 try {
  auto& s=state();if(s.installed)return true;
  // Live context remains fixed even when another mod has chained the CALL.
  // Caller 140D83DE0 and callee 140D3C820; wand slots and handedness in
  // 140EF90C0 / 140F02150 were checked against raw FO4VR disassembly.
  if(!prefix(0xd84063,{0x48,0x8b,0x0d,0xa6,0xe2,0xba,0x04}) || !prefix(0xd8405e,{0xe8}) ||
     !prefix(0xef90c0,{0x40,0x53,0x48,0x83,0xec,0x60,0x48,0x83,0xb9,0xf0,0x06,0,0,0}) ||
     !prefix(0xc86c30,{0x40,0x57,0x48,0x83,0xec,0x20,0x48,0x83,0x79,0x38,0})) {log::error("Independent UI input native boundary rejected");return false;}
  std::int32_t relative{};
  if(!read(REL::Offset(0xd8405f).address(),relative))return false;
  const auto target=REL::Offset(0xd84063).address()+relative;MEMORY_BASIC_INFORMATION memory{};
  if(!VirtualQuery(reinterpret_cast<void*>(target),&memory,sizeof(memory)) || memory.State!=MEM_COMMIT ||
     (memory.Protect&(PAGE_GUARD|PAGE_NOACCESS)) || !(memory.Protect&(PAGE_EXECUTE|PAGE_EXECUTE_READ|PAGE_EXECUTE_READWRITE|PAGE_EXECUTE_WRITECOPY)))return false;
  if(!connectRock()){log::error("Loaded ROCK could not register UI input; refusing conflicting input hooks");return false;}
  s.useRock=s.rockOwner!=0;
  log::info("UI controller input source: {}",s.rockOwner?"ROCK (native polling disabled)":"native wands / OpenVR");
  {std::scoped_lock lock(s.mutex);for(auto& client:s.clients)if(client.token && s.rockOwner)client.rockOwner=registerRockInputOwner();}
  originalTick=reinterpret_cast<GameTick>(F4SE::GetTrampoline().write_call<5>(REL::Offset(0xd8405e).address(),&tick));
  s.installed=originalTick!=nullptr;log::info("Independent UI input installed; optional ROCK owner={}",s.rockOwner);return s.installed;
 }catch(...){log::error("Independent UI input installation failed");return false;}
}
bool installed() noexcept{return state().installed.load();}
void sessionReady(bool ready) noexcept{state().session=ready;if(!ready)state().allowPollCapture=false;else state().captureHealthy=true;}
bool rawButton(unsigned hand,unsigned button) noexcept{return hand<2 && button<64 && (state().frame.hands[hand].pressed&(1ull<<button));}
bool captureHost(unsigned hand,bool active,bool configNavigation) noexcept {
 auto& s=state();if(hand>=2)return false;s.hostCapture[hand]=active;s.hostConfig[hand]=configNavigation;
 // The pointing stick scrolls the panel; its analog axis must not also move
 // the player. Raw UI sampling remains unfiltered.
 sdk::InputCaptureV1 request;for(unsigned side=0;side<2;++side)if(s.hostCapture[side])request.buttons[side]=trigger|accept|(1ull<<32)|(s.hostConfig[side]?grip:0);
 publishMask(0,request);return applyRockCapture(s.rockOwner,0);
}
}
extern "C" __declspec(dllexport) const rpsui::sdk::InputApiV1* RPSUI_CALL RPSUI_RequestInputApi() noexcept {
 static const rpsui::sdk::InputApiV1 api{.subscribe=&rpsui::input::subscribe,.unsubscribe=&rpsui::input::unsubscribe,.capture=&rpsui::input::capture};return &api;
}
