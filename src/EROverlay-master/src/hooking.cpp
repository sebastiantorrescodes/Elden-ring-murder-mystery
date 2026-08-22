//hooking.cpp

#include "hooking.hpp"
#include "d3drenderer.hpp"
#include "util/signature.hpp"

#include <MinHook.h>
#include <windows.h>
#include <cstring>

#include "global.hpp"

namespace er {

std::unique_ptr<Hooking> gHooking;

void Hooking::installLockOnTargetHook() {
    if (lockOnTargetCave_ != nullptr) {
        return;
    }

    util::Signature sig("48 8B 48 08 49 89 8D B0 06 00 00 49 8B CE E8");
    auto res = sig.scan();
    if (!res) {
        return;
    }

    lockOnTargetAccessor_ = res.as<uintptr_t>();
    constexpr size_t patchSize = 14;

    std::memcpy(lockOnTargetOriginal_, reinterpret_cast<void *>(lockOnTargetAccessor_), patchSize);

    auto *cave = reinterpret_cast<uint8_t *>(VirtualAlloc(nullptr, 128, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));

    if (!cave) {
        lockOnTargetAccessor_ = 0;
        return;
    }

    size_t i = 0;

    // push r11
    cave[i++] = 0x41;
    cave[i++] = 0x53;

    // mov r11, &lastLockOnTarget_
    cave[i++] = 0x49;
    cave[i++] = 0xBB;
    *reinterpret_cast<uintptr_t *>(cave + i) = reinterpret_cast<uintptr_t>(&lastLockOnTarget_);
    i += 8;

    // mov [r11], rax
    cave[i++] = 0x49;
    cave[i++] = 0x89;
    cave[i++] = 0x03;

    // mov r11, &lastLockOnTargetSerial_
    cave[i++] = 0x49;
    cave[i++] = 0xBB;
    *reinterpret_cast<uintptr_t *>(cave + i) = reinterpret_cast<uintptr_t>(&lastLockOnTargetSerial_);
    i += 8;

    // inc qword ptr [r11]
    cave[i++] = 0x49;
    cave[i++] = 0xFF;
    cave[i++] = 0x03;

    // pop r11
    cave[i++] = 0x41;
    cave[i++] = 0x5B;

    // Original overwritten instructions:
    // 48 8B 48 08                         mov rcx,[rax+08]
    // 49 89 8D B0 06 00 00               mov [r13+000006B0],rcx
    // 49 8B CE                            mov rcx,r14
    std::memcpy(cave + i, lockOnTargetOriginal_, patchSize);
    i += patchSize;

    // Absolute jump back to original code after our 14-byte patch.
    // ff 25 00 00 00 00 [8-byte address]
    cave[i++] = 0xFF;
    cave[i++] = 0x25;
    cave[i++] = 0x00;
    cave[i++] = 0x00;
    cave[i++] = 0x00;
    cave[i++] = 0x00;
    *reinterpret_cast<uintptr_t *>(cave + i) = lockOnTargetAccessor_ + patchSize;
    i += 8;

    uint8_t patch[patchSize] = {
        0xFF, 0x25, 0x00, 0x00, 0x00, 0x00, // jmp qword ptr [rip+0]
        0,    0,    0,    0,    0,    0,    0, 0 // absolute destination
    };

    *reinterpret_cast<uintptr_t *>(patch + 6) = reinterpret_cast<uintptr_t>(cave);

    DWORD oldProtect = 0;
    if (VirtualProtect(reinterpret_cast<void *>(lockOnTargetAccessor_), patchSize, PAGE_EXECUTE_READWRITE, &oldProtect)) {
        std::memcpy(reinterpret_cast<void *>(lockOnTargetAccessor_), patch, patchSize);
        VirtualProtect(reinterpret_cast<void *>(lockOnTargetAccessor_), patchSize, oldProtect, &oldProtect);
        FlushInstructionCache(GetCurrentProcess(), reinterpret_cast<void *>(lockOnTargetAccessor_), patchSize);
        lockOnTargetCave_ = cave;
    } else {
        VirtualFree(cave, 0, MEM_RELEASE);
        lockOnTargetAccessor_ = 0;
    }
}

void Hooking::uninstallLockOnTargetHook() {
    constexpr size_t patchSize = 14;

    if (lockOnTargetAccessor_ != 0 && lockOnTargetOriginal_[0] != 0) {
        DWORD oldProtect = 0;
        if (VirtualProtect(reinterpret_cast<void *>(lockOnTargetAccessor_), patchSize, PAGE_EXECUTE_READWRITE, &oldProtect)) {
            std::memcpy(reinterpret_cast<void *>(lockOnTargetAccessor_), lockOnTargetOriginal_, patchSize);
            VirtualProtect(reinterpret_cast<void *>(lockOnTargetAccessor_), patchSize, oldProtect, &oldProtect);
            FlushInstructionCache(GetCurrentProcess(), reinterpret_cast<void *>(lockOnTargetAccessor_), patchSize);
        }
    }

    if (lockOnTargetCave_ != nullptr) {
        VirtualFree(lockOnTargetCave_, 0, MEM_RELEASE);
        lockOnTargetCave_ = nullptr;
    }

    lockOnTargetAccessor_ = 0;
    lastLockOnTarget_ = 0;
    std::memset(lockOnTargetOriginal_, 0, sizeof(lockOnTargetOriginal_));
}

void Hooking::findHooks() {
    {
        util::Signature sig("48 8B 0D ?? ?? ?? ?? 48 8B 49 08 E8 ?? ?? ?? ?? 48 8B D0 48 8B CE E8");
        if (auto res = sig.scan()) {
            csMenuManImp_ = res.add(res.add(3).as<uint32_t &>() + 7).as<uintptr_t>();
        }
    }
    {
        util::Signature sig("48 8B 05 ?? ?? ?? ?? 48 85 C0 74 05 48 8B 40 58 C3 C3");
        if (auto res = sig.scan()) {
            auto addr = res.add(res.add(3).as<std::uint32_t &>() + 7);
            gameDataMan_ = addr.as<uintptr_t>();
        }
    }
    {
        util::Signature sig("48 8B 3D ?? ?? ?? ?? 48 85 FF ?? ?? 32 C0 E9");
        if (auto res = sig.scan()) {
            auto addr = res.add(res.add(3).as<std::uint32_t &>() + 7);
            eventFlagMan_ = addr.as<uintptr_t>();
        }
    }
    {
        util::Signature sig("48 8B 0D ?? ?? ?? ?? 48 ?? ?? ?? 44 0F B6 61 ?? E8 ?? ?? ?? ?? 48 63 87 ?? ?? ?? ?? 48 ?? ?? ?? 48 85 C0");
        if (auto res = sig.scan()) {
            auto addr = res.add(res.add(3).as<std::uint32_t &>() + 7);
            fieldArea_ = addr.as<uintptr_t>();
        }
    }
    {
        util::Signature sig("48 8B 05 ?? ?? ?? ?? 48 85 C0 74 0F 48 39 88");
        if (auto res = sig.scan()) {
            auto addr = res.add(res.add(3).as<std::uint32_t &>() + 7);
            worldChrMan_ = addr.as<uintptr_t>();
        }
    }
    if (gGameVersion < 0x0001000300000000ULL) {
        // 1.02 ~ 1.02.3
        menuInfoOffset_ = 0x708;
    } else if (gGameVersion < 0x0002000100000000ULL) {
        // 1.03 ~ 1.10.1
        menuInfoOffset_ = 0x718;
    } else {
        // 1.12+
        menuInfoOffset_ = 0x720;
    }

    installLockOnTargetHook();
}

Hooking::Hooking() {
    MH_Initialize();
    findHooks();
}

Hooking::~Hooking() noexcept {
    uninstallLockOnTargetHook();
    unhook();
    MH_Uninitialize();
}

void Hooking::hook() {
    gD3DRenderer->hook();
}

void Hooking::unhook() {
    gD3DRenderer->unhook();
}

void Hooking::showMouseCursor(bool show) const {
    if (csMenuManImp_ == 0) return;
    auto addr = *reinterpret_cast<uintptr_t*>(csMenuManImp_);
    if (addr == 0) return;
    auto *ptr = reinterpret_cast<uint8_t*>(addr + 0xAC);
    if (show)
        *ptr |= 1u;
    else
        *ptr &= ~1u;
}

bool Hooking::menuLoaded() const {
    if (csMenuManImp_ == 0) return false;
    auto addr = *reinterpret_cast<uintptr_t*>(csMenuManImp_);
    if (addr == 0) return false;
    return *reinterpret_cast<float*>(addr + menuInfoOffset_ + 0x24) > 0.0f;
}

int Hooking::screenState() const {
    if (csMenuManImp_ == 0) return 1;
    auto addr = *reinterpret_cast<uintptr_t*>(csMenuManImp_);
    if (addr == 0) return 1;
    return *reinterpret_cast<int*>(addr + menuInfoOffset_ + 0x10);
}

}
