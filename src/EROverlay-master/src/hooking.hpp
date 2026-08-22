//hooking.hpp

#pragma once

#include <cstdint>
#include <memory>

namespace er {

class Hooking {
    friend class EROverlayAPIWrapper;

public:
    explicit Hooking();
    ~Hooking() noexcept;

    Hooking(Hooking const &) = delete;
    Hooking(Hooking &&) = delete;
    Hooking &operator=(Hooking const &) = delete;
    Hooking &operator=(Hooking &&) = delete;

    static void hook();
    static void unhook();

    void findHooks();

    void showMouseCursor(bool show) const;
    [[nodiscard]] bool menuLoaded() const;
    [[nodiscard]] int screenState() const;

private:
    uintptr_t csMenuManImp_ = 0;
    uintptr_t gameDataMan_ = 0;
    uintptr_t eventFlagMan_ = 0;
    uintptr_t fieldArea_ = 0;
    uintptr_t worldChrMan_ = 0;
    uintptr_t lastLockOnTarget_ = 0;
    uint64_t lastLockOnTargetSerial_ = 0;

    uintptr_t lockOnTargetAccessor_ = 0;
    void *lockOnTargetCave_ = nullptr;
    uint8_t lockOnTargetOriginal_[14] = {};

    int menuInfoOffset_ = 0;

    void installLockOnTargetHook();
    void uninstallLockOnTargetHook();
};

extern std::unique_ptr<Hooking> gHooking;

} // namespace er
