$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $PSScriptRoot
$cmake = Get-Content -Raw (Join-Path $root 'CMakeLists.txt')
$plugin = Get-Content -Raw (Join-Path $root 'src/Plugin.cpp')
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
    'snapshotRenderPanels(',
    'panel_imgui::drawPanel(',
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
# The host owns every panel's ImGui renderer; a consumer never receives a render target.
$frameworkHeader = Get-Content -Raw (Join-Path $root 'include/RPS_UI_Framework/Framework.h')
if ($frameworkHeader -match 'renderTargetView|d3dContext') {
    throw 'A panel consumer was given the render target or device context; the host renders panels.'
}

if (-not $hooks.Contains('roll back a failed stereo-hook transaction')) {
    throw 'The stereo hook no longer rolls back a failed installation.'
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
