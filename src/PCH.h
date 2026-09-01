#pragma once

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#define NOMMNOSOUND

#include "F4SE/F4SE.h"
#include "RE/Fallout.h"
#include "REL/Relocation.h"

#include <DirectXMath.h>
#include <d3d11.h>
#include <dxgi.h>
#include <wrl/client.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
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
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

#include <spdlog/spdlog.h>

#define DLLEXPORT __declspec(dllexport)
