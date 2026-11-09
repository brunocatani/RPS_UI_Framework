#pragma once

// Dear ImGui build contract between RPS UI Framework and its panel consumers.
//
// RPS UI owns every panel's Dear ImGui context: its DX11 renderer, its font
// atlas, its input and its frame. A consumer issues ImGui calls into that
// context from its own copy of Dear ImGui, so both copies must share one source
// revision and one configuration. Compile Dear ImGui with this file as its user
// configuration:
//
//     IMGUI_USER_CONFIG="RPS_UI_Framework/ImGuiBuild.h"
//
// (with the directory holding RPS_UI_Framework/ on the include path), and use
// the Dear ImGui revision named by docs/api/Framework.md. A consumer that needs
// more configuration includes this file from its own user configuration first.
// registerPanel compares the consumer's build against the host's and refuses a
// different one.
//
// The two macros below expand only in consumer code that includes <imgui.h>
// and <imgui_internal.h>; this header itself includes nothing.

// Configuration that changes Dear ImGui's data layout. Nothing else may differ.
#ifndef IMGUI_USE_WCHAR32
#define IMGUI_USE_WCHAR32
#endif
#ifndef IMGUI_DISABLE_OBSOLETE_FUNCTIONS
#define IMGUI_DISABLE_OBSOLETE_FUNCTIONS
#endif

// The consumer's Dear ImGui build, for PanelRegistration::imgui.
#define RPSUI_IMGUI_BUILD()                                                                                            \
    ::rpsui::api::framework::ImGuiBuild                                                                                \
    {                                                                                                                  \
        .versionNum = static_cast<std::uint32_t>(IMGUI_VERSION_NUM),                                                   \
        .wcharSize = static_cast<std::uint32_t>(sizeof(ImWchar)),                                                      \
        .ioSize = static_cast<std::uint32_t>(sizeof(ImGuiIO)),                                                         \
        .styleSize = static_cast<std::uint32_t>(sizeof(ImGuiStyle)),                                                   \
        .contextSize = static_cast<std::uint32_t>(sizeof(ImGuiContext)),                                               \
        .drawListSize = static_cast<std::uint32_t>(sizeof(ImDrawList)),                                                \
        .fontAtlasSize = static_cast<std::uint32_t>(sizeof(ImFontAtlas)),                                              \
        .fontSize = static_cast<std::uint32_t>(sizeof(ImFont)),                                                        \
        .textureDataSize = static_cast<std::uint32_t>(sizeof(ImTextureData)),                                          \
        .drawVertSize = static_cast<std::uint32_t>(sizeof(ImDrawVert)),                                                \
        .drawIdxSize = static_cast<std::uint32_t>(sizeof(ImDrawIdx)),                                                  \
    }

// Makes the host's context current in the consumer's Dear ImGui copy, with the
// host's allocator so memory moves safely across the DLL boundary. Call it at
// the start of every setup and draw callback, before any other ImGui call.
#define RPSUI_IMGUI_BIND(binding)                                                                                      \
    do {                                                                                                               \
        ::ImGui::SetAllocatorFunctions((binding).alloc, (binding).free, (binding).allocUserData);                      \
        ::ImGui::SetCurrentContext(static_cast<::ImGuiContext*>((binding).context));                                   \
    } while (false)
