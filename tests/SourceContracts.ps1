$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $PSScriptRoot
$cmake = Get-Content -Raw (Join-Path $root 'CMakeLists.txt')
$plugin = Get-Content -Raw (Join-Path $root 'src/Plugin.cpp')
$api = Get-Content -Raw (Join-Path $root 'SDK/include/RPSUIFrameworkApi.h')
$runtime = Get-Content -Raw (Join-Path $root 'src/FrameworkRuntime.cpp')
$runtimeHeader = Get-Content -Raw (Join-Path $root 'src/FrameworkRuntime.h')
$renderer = Get-Content -Raw (Join-Path $root 'src/render/HostRenderer.cpp')
$hooks = Get-Content -Raw (Join-Path $root 'src/render/RenderHooks.cpp')

foreach ($token in @(
    'F4SEPlugin_Query',
    'F4SEPlugin_Load',
    'REL::Module::IsVR()',
    'REL::Module::get().version()',
    'F4SE::RUNTIME_VR_1_2_72',
    'const auto requiredRuntime = F4SE::RUNTIME_1_10_138',
    'F4SE::AllocTrampoline(4096)'
)) {
    if (-not $plugin.Contains($token)) {
        throw "FO4VR loader contract token missing: $token"
    }
}
if ($plugin -match 'RuntimeVersion\(\)\s*[=!<>]+\s*F4SE::RUNTIME_(LATEST_)?VR') {
    throw 'F4SE compatibility runtime was compared with a VR executable constant.'
}

foreach ($token in @(
    'RPSUI_API_VERSION = 1',
    'RPSUI_API_FLAVOR',
    'RPSUI_MAX_CONSUMERS',
    'RPSUI_MAX_PANELS',
    'PanelRenderCallbackV1',
    'registerConsumer',
    'registerPanel',
    'submitPanelPresentation',
    'resetPanelSize',
    'getPanelState',
    'std::is_standard_layout_v'
)) {
    if (-not $api.Contains($token)) {
        throw "Public SDK ABI token missing: $token"
    }
}

foreach ($token in @(
    'RPSUI_RequestApi',
    'MultipleWorldPanels',
    'CentralPointerRouting',
    'ConsumerRenderCallbacks'
)) {
    $all = $api + (Get-Content -Raw (Join-Path $root 'src/Api.cpp'))
    if (-not $all.Contains($token)) {
        throw "Provider API contract token missing: $token"
    }
}

foreach ($token in @(
    'pointer_hand_selection::choose(',
    'pointer_click_gate::advance(',
    'contextual_scroll::update(',
    'panel_resize::hitTest(',
    'separateNewPanelLocked'
)) {
    if (-not $runtime.Contains($token)) {
        throw "Central input/registry token missing: $token"
    }
}

foreach ($token in @(
    'snapshotRenderPanels()',
    'PanelRenderFrameV1',
    'renderConsumerPanel(',
    'drawWorldPanel(',
    'AcquireForSubmittedTarget',
    'CaptureSnapshot'
)) {
    if (-not $renderer.Contains($token)) {
        throw "Shared compositor token missing: $token"
    }
}
if (-not $runtimeHeader.Contains('RenderPanelBatch')) {
    throw 'The render path no longer uses its fixed-capacity panel snapshot.'
}
if ($renderer -match 'imgui|ImGui') {
    throw 'The framework host must not own a consumer ImGui context.'
}

foreach ($token in @(
    'stereo guards do not match the pristine',
    'exclusive guarded FO4VR stereo composition',
    'roll back a failed stereo-hook transaction'
)) {
    if (-not $hooks.Contains($token)) {
        throw "Exclusive host hook guard missing: $token"
    }
}

if (-not $cmake.Contains('D:/FO4/mods/RPS_UI_Framework') -and
    -not (Test-Path -LiteralPath (Join-Path $root 'CMakeUserPresets.json'))) {
    throw 'No local custom-fast deployment configuration is available.'
}
if (-not $cmake.Contains('RPSUIFrameworkPolicyTests') -or
    -not $cmake.Contains('RPSUIFramework.SourceContracts')) {
    throw 'Framework validation targets are missing.'
}

Write-Host 'RPS UI Framework source contracts passed.'
