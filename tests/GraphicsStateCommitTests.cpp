#include "render/GraphicsStateCommit.h"
#include <Windows.h>
#include <iostream>
#include <stdexcept>

namespace
{
    namespace commit = rpsui::render::graphics_state_commit;
    using Function = void (*)(std::uint8_t, std::uint8_t);
    Function original{};
    std::array<std::uint8_t, 2> observed{};
    unsigned hookCalls{};
    bool afterNative{};

    void require(bool value, const char* message)
    {
        if (!value) throw std::runtime_error(message);
    }

    void foreignHook(std::uint8_t first, std::uint8_t second)
    {
        ++hookCalls;
        original(first, second);
        afterNative = observed == std::array<std::uint8_t, 2>{first, second};
    }

    void test()
    {
        struct Memory
        {
            void* address = VirtualAlloc(nullptr, 4096, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
            ~Memory() { if (address) VirtualFree(address, 0, MEM_RELEASE); }
        } memory;
        require(memory.address != nullptr, "allocate executable fixture");
        auto* code = static_cast<std::uint8_t*>(memory.address);
        const auto entry = reinterpret_cast<std::uintptr_t>(code);
        const auto read = [&](std::uintptr_t p, void* output, std::size_t size) noexcept {
            if (p < entry || size > 4096 || p - entry > 4096 - size) return false;
            std::memcpy(output, reinterpret_cast<const void*>(p), size);
            return true;
        };
        const auto executable = [&](std::uintptr_t p) noexcept { return p >= entry && p < entry + 4096; };
        const auto inspect = [&] { return commit::inspect(entry, read, executable); };
        std::copy(commit::nativePrologue.begin(), commit::nativePrologue.end(), code);
        // Execute the actual native prologue, write both byte arguments, and
        // unwind its five register saves and 0x50 stack allocation.
        std::array<std::uint8_t, 27> body{
            0x48,0xB8,0,0,0,0,0,0,0,0,0x88,0x08,0x88,0x50,0x01,
            0x48,0x83,0xC4,0x50,0x41,0x5F,0x41,0x5E,0x5F,0x5E,0x5D,0xC3
        };
        const auto output = reinterpret_cast<std::uintptr_t>(observed.data());
        std::memcpy(body.data() + 2, &output, sizeof(output));
        std::copy(body.begin(), body.end(), code + commit::nativePrologue.size());
        const auto call = reinterpret_cast<Function>(code);
        require(inspect().valid && !inspect().detour, "native entry accepted");
        FlushInstructionCache(GetCurrentProcess(), code, 4096);
        call(0xA5, 0x5A);
        require(observed == std::array<std::uint8_t, 2>{0xA5, 0x5A}, "native byte arguments preserved");

        // MinHook relocates eight whole bytes despite its five-byte patch.
        std::copy_n(code, 8, code + 128);
        code[136] = 0xFF; code[137] = 0x25;
        const auto resume = entry + 8;
        std::memcpy(code + 142, &resume, sizeof(resume));
        original = reinterpret_cast<Function>(code + 128);
        code[256] = 0xFF; code[257] = 0x25;
        const auto hook = reinterpret_cast<std::uintptr_t>(&foreignHook);
        std::memcpy(code + 262, &hook, sizeof(hook));
        const auto jump = [&](std::int32_t displacement) {
            code[0] = 0xE9;
            std::memcpy(code + 1, &displacement, sizeof(displacement));
        };
        jump(251);
        std::array<std::uint8_t, 19> before{};
        std::copy_n(code, before.size(), before.begin());
        require(inspect().valid && inspect().detour == entry + 256, "existing E9 relay accepted");
        require(std::equal(before.begin(), before.end(), code), "inspection leaves foreign hook intact");
        FlushInstructionCache(GetCurrentProcess(), code, 4096);
        call(0xFF, 0x80);
        require(hookCalls == 1 && afterNative && observed == std::array<std::uint8_t, 2>{0xFF, 0x80},
            "callee entry preserves foreign post-commit work and both arguments");

        std::copy_n(code, before.size(), code + 512);
        const std::int32_t backwards = -261;
        std::memcpy(code + 513, &backwards, sizeof(backwards));
        require(commit::inspect(entry + 512, read, executable).detour == entry + 256,
            "signed backwards entry jump accepted");
        code[5] ^= 1;
        require(!inspect().valid, "changed native tail rejected");
        code[5] ^= 1;
        jump(-5);
        require(!inspect().valid, "self loop rejected");
        jump(0);
        require(!inspect().valid, "jump into prologue rejected");
        jump(4091);
        require(!inspect().valid, "non-executable destination rejected");
        jump(251); code[0] = 0xE8;
        require(!inspect().valid, "CALL opcode rejected");
        require(!commit::inspect(entry + 4090, read, executable).valid, "unreadable entry rejected");
        original = nullptr;
    }
}

int main()
{
    try {
        test();
        std::cout << "Graphics-state commit native and foreign-hook compatibility passed.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
