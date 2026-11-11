namespace {unsigned calls{};bool accept=true;}
extern "C" __declspec(dllexport) bool DIAMONDAPI_ReloadSettingsV1() noexcept {++calls;return accept;}
extern "C" __declspec(dllexport) unsigned DIAMONDTestReloadCount() noexcept {return calls;}
extern "C" __declspec(dllexport) void DIAMONDTestAcceptReload(bool value) noexcept {accept=value;}
