#pragma once

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#define NOMMNOSOUND

#include <algorithm>
using std::min;

#ifndef RPSUI_SETTINGS_MENU_PREVIEW
#include "F4SE/F4SE.h"
#include "RE/Fallout.h"
#include <REL/Relocation.h>
#endif

#include <DirectXMath.h>
#include <d3d11.h>
#include <dxgi.h>
#include <wrl/client.h>

#include <array>
#include <atomic>
#include <chrono>
#include <cfloat>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <format>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include <imgui.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/spdlog.h>

using namespace std::literals;
namespace logger = spdlog;

#define DLLEXPORT __declspec(dllexport)
