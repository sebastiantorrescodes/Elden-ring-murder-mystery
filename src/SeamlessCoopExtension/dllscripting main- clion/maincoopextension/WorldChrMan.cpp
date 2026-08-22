#include "WorldChrMan.h"
#include "Signature.h"
#include "GameVersion.h"
#include "pch.h"
#include <iostream>
#include <cmath>


static void** WorldChrManIns;
static void** GameManIns;
static void** CsMenuManImpIns;
static void** FieldAreaIns;

// Location struct offset shifted between game patches (matches EROverlay's own
// version-gated locationOffset_): 0x248 pre-1.12, 0x250 on 1.12+. Threshold is
// EROverlay's own packed-version constant, same format GetGameVersion() returns.
static size_t LocationOffset = 0x250;

//TODO: refactor GameMan's impl here
void** InitHelper() {
    ModuleData EldenRingData("eldenring.exe");
    Signature worldChrManSig = Signature("48 8B 05 ?? ?? ?? ?? 48 85 C0 74 0F 48 39 88");
    WorldChrManIns = (void**)worldChrManSig.Scan(&EldenRingData, 0x3, 0x7);

    Signature gameManSig = Signature("48 8B 05 ?? ?? ?? ?? 80 B8 ?? ?? ?? ?? 0D 0F 94 C0 C3");
    GameManIns = (void**)gameManSig.Scan(&EldenRingData, 0x3, 0x7);

    Signature csMenuManImpSig = Signature("48 8B 0D ?? ?? ?? ?? 48 8B 49 08 E8 ?? ?? ?? ?? 48 8B D0 48 8B CE E8");
    CsMenuManImpIns = (void**)csMenuManImpSig.Scan(&EldenRingData, 0x3, 0x7);

    Signature fieldAreaSig = Signature("48 8B 0D ?? ?? ?? ?? 48 ?? ?? ?? 44 0F B6 61 ?? E8 ?? ?? ?? ?? 48 63 87 ?? ?? ?? ?? 48 ?? ?? ?? 48 85 C0");
    FieldAreaIns = (void**)fieldAreaSig.Scan(&EldenRingData, 0x3, 0x7);

    uint64_t gameVersion = GetGameVersion();
    LocationOffset = (gameVersion < 0x0002000100000000ULL) ? 0x248 : 0x250;

    return WorldChrManIns;
}

// PointerChain::make stores its base argument by reference, so a null-safe fallback
// must still return an lvalue (a ternary would bind a temporary and fail to compile).
static uintptr_t& SafeDeref(void** add) {
    static uintptr_t zero = 0;
    if (add) return *(uintptr_t*)add;
    return zero;
}

// Ported from EROverlay's saneCoord(): a pointer walk can succeed (no crash, no null)
// and still land on garbage/freed memory. Catches that case so a bad read is treated
// the same as an empty slot rather than being trusted and streamed out as-is.
static bool saneCoord(float x, float y, float z) {
    if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z)) {
        return false;
    }
    // Reject zero/near-zero fake reads.
    if (std::abs(x) < 0.001f && std::abs(y) < 0.001f && std::abs(z) < 0.001f) {
        return false;
    }
    // Reject nonsense reads like garbage memory interpreted as a huge float.
    if (std::abs(x) > 100000.f || std::abs(y) > 100000.f || std::abs(z) > 100000.f) {
        return false;
    }
    return true;
}

WorldChrMan::WorldChrMan(void** add) : Address(add ? (void*)*(uintptr_t*)add : nullptr),
php(SafeDeref(add), 0x10EF8, 0, 0x190u, 0, 0x138),
pmhp(SafeDeref(add), 0x10EF8, 0, 0x190u, 0, 0x13C),
ianim(SafeDeref(add), 0x10EF8, 0, 0x190u, 0x58, 0x18),
canim(SafeDeref(add), 0x10EF8, 0, 0x190u, 0x58, 0x20),
lxcoord(SafeDeref(add), 0x10EF8, 0, 0x190u, 0x68, 0x70),
lycoord(SafeDeref(add), 0x10EF8, 0, 0x190u, 0x68, 0x74),
lzcoord(SafeDeref(add), 0x10EF8, 0, 0x190u, 0x68, 0x78) {
}

bool WorldChrMan::isLoaded() {
    if (address) {
        return true;
    }
    else {
        return false;
    }
}

WorldChrMan WorldChrMan::Make() {
    WorldChrManIns = InitHelper();
    if(WorldChrManIns && *WorldChrManIns != NULL)
        return WorldChrMan(WorldChrManIns);
    else return WorldChrMan(nullptr);
}

int WorldChrMan::getHP() {
    if (address) {
        return *php;
    }
    return -1;
}

void WorldChrMan::setHP(int hp) {
    if (address) {
        *php = hp;
    }
}

int WorldChrMan::getMaxHP() {
    if (address) {
        return *pmhp;
    }
    return -1;
}

void WorldChrMan::setIdleAnim(int idleAnimId) {
    if (address) {
        *ianim = idleAnimId;
    }
}

int WorldChrMan::getCurrentAnimID() {
    if (address) {
        return *canim;
    }
    return -1;
}

void WorldChrMan::setLocalCoords(float x, float y, float z) {
    if (address) {
        if (x != NULL) {
            *lxcoord = x;
        }
        if (y != NULL) {
            *lycoord = y;
        }
        if (z != NULL) {
            *lzcoord = z;
        }
    }
}

// Raw pointer walk (not PointerChain) wrapped in SEH, same reasoning as tryReadSlot:
// a bad/out-of-bounds slot returns a safe default instead of crashing the whole game.
// Note: preserves the existing chain shape exactly (0x190, 0x18, 0x20, no +0x58 step),
// which differs from the local player's canim chain (0x190u, 0x58, 0x20) - that
// discrepancy predates this change and is left as-is; flagging it separately.
int WorldChrMan::getCurrentAnimIdForOtherPlayer(int playerSlot) {
    __try {
        if (!WorldChrManIns) {
            return -1;
        }
        uintptr_t base = *(uintptr_t*)WorldChrManIns;
        base = *(uintptr_t*)(base + 0x10EF8);
        base = *(uintptr_t*)(base + (0x10 * playerSlot));
        if (!base) {
            return -1;
        }
        base = *(uintptr_t*)(base + 0x190);
        if (!base) {
            return -1;
        }
        base = *(uintptr_t*)(base + 0x18);
        if (!base) {
            return -1;
        }
        return *(int*)(base + 0x20);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return -1;
    }
}

// tryReadSlot already does exactly this walk safely (same offsets, SEH-wrapped) -
// reuse it instead of duplicating the offset chain a third time.
PlayerCoords WorldChrMan::getCoordsForOtherPlayer(int playerSlot) {
    PlayerCoords coords{};
    tryReadSlot(playerSlot, coords);
    return coords;
}

// Note: preserves the existing (likely buggy) unscaled "i" offset - every other
// per-slot walk in this file uses playerOffset = 0x10 * playerSlot since each slot
// entry is 0x10 bytes, but this one indexes by raw slot number. Left as-is here
// (SEH-wrapped only) and flagged separately rather than silently changed, since it's
// a behavior change that needs verifying in-game, not a crash-safety fix.
int WorldChrMan::getPlayerSlotFromChrIns(ChrIns *chrIns) {
    __try {
        if (!WorldChrManIns) {
            return -1;
        }
        int playerCount = getPlayerCount();
        uintptr_t base = *(uintptr_t*)WorldChrManIns;
        base = *(uintptr_t*)(base + 0x10EF8);
        for (int i = 0; i < playerCount; i++) {
            ChrIns* arrayChrIns = *(ChrIns**)(base + i);
            if (arrayChrIns == chrIns) {
                return i;
            }
        }
        return -1;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return -1;
    }
}

/*
 * Least significant bit(pos 0) is on the right here.
 * Simple flag checker
 */
bool WorldChrMan::readBitFlag(int value, int pos){
    int mask = (1 << pos);
    return ((value & mask) != 0);
}

bool WorldChrMan::isInIframes(int playerSlot) {
    __try {
        if (!WorldChrManIns) {
            return false;
        }
        uintptr_t base = *(uintptr_t*)WorldChrManIns;
        base = *(uintptr_t*)(base + 0x10EF8);
        base = *(uintptr_t*)(base + (0x10 * playerSlot));
        if (!base) {
            return false;
        }
        base = *(uintptr_t*)(base + 0x190);
        if (!base) {
            return false;
        }
        base = *(uintptr_t*)(base + 0x8);
        if (!base) {
            return false;
        }
        int flagData = *(int*)(base + 0x40);
        return readBitFlag(flagData, 1);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}


//try [[GameMan]+D60]+14(or +1C) as a total player in session value
//horribly hacky and unclean temp solution(not even a worldchrman aob)
int WorldChrMan::getPlayerCount(){
    int playerCount = PointerChain::make<int>(GameManIns, 0xD60, 0x1C);//either 0x1C or 0x14 here. Unsure which
    return playerCount;
}

void WorldChrMan::debugPlayerCount() {
    std::cout << "GameManIns addr: " << std::hex << GameManIns << std::dec << std::endl;
    if (GameManIns && *GameManIns) {
        int val14 = PointerChain::make<int>(GameManIns, 0xD60, 0x14);
        int val1C = PointerChain::make<int>(GameManIns, 0xD60, 0x1C);
        std::cout << "offset 0x14 value: " << val14 << ", offset 0x1C value: " << val1C << std::endl;
    } else {
        std::cout << "GameManIns not resolved" << std::endl;
    }
}

// Uses raw pointer walks (not PointerChain) wrapped in SEH so a bad/out-of-bounds
// slot returns false instead of crashing the whole game.
bool WorldChrMan::tryReadSlot(int playerSlot, PlayerCoords& out) {
    __try {
        if (!WorldChrManIns) {
            return false;
        }
        uintptr_t base = *(uintptr_t*)WorldChrManIns;
        base = *(uintptr_t*)(base + 0x10EF8);
        base = base + (0x10 * playerSlot);
        base = *(uintptr_t*)base;
        if (!base) {
            return false;
        }
        uintptr_t chrBase = *(uintptr_t*)(base + 0x190);

        uintptr_t coordBase = *(uintptr_t*)(chrBase + 0x68);
        out.x = *(float*)(coordBase + 0x70);
        out.y = *(float*)(coordBase + 0x74);
        out.z = *(float*)(coordBase + 0x78);

        // Reads without crashing, but could still be garbage/freed memory - reject
        // NaN/Inf, near-zero, and wildly-out-of-range reads the same as an empty slot.
        if (!saneCoord(out.x, out.y, out.z)) {
            return false;
        }

        // Same base as php/pmhp in the constructor, mirrored here since this probe bypasses PointerChain.
        uintptr_t hpBase = *(uintptr_t*)(chrBase + 0);
        out.hp = *(int*)(hpBase + 0x138);
        out.maxHp = *(int*)(hpBase + 0x13C);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

void WorldChrMan::debugSlot(int playerSlot) {
    if (!WorldChrManIns) {
        std::cout << "Slot " << playerSlot << ": WorldChrManIns is null" << std::endl;
        return;
    }
    PlayerCoords coords{};
    if (!tryReadSlot(playerSlot, coords)) {
        std::cout << "Slot " << playerSlot << ": empty or read crashed, likely out of bounds" << std::endl;
        return;
    }
    std::cout << "Slot " << playerSlot << " coords: x=" << coords.x << " y=" << coords.y << " z=" << coords.z
               << " hp=" << coords.hp << "/" << coords.maxHp << std::endl;
}

// Local client's own current map/area via CsMenuManImp - see the Location comment
// in WorldChrMan.h for why this is not per-player-slot data. SEH-wrapped since this
// chain has more hops than the other reads here.
bool WorldChrMan::tryReadLocation(Location& out) {
    __try {
        if (!CsMenuManImpIns) {
            return false;
        }
        uintptr_t addr = *(uintptr_t*)CsMenuManImpIns;
        if (!addr) {
            return false;
        }
        addr = *(uintptr_t*)(addr + 0x80);
        if (!addr) {
            return false;
        }
        addr = *(uintptr_t*)(addr + LocationOffset);
        if (!addr) {
            return false;
        }
        out = *(Location*)(addr + 0x24);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

void WorldChrMan::debugLocation() {
    Location loc{};
    if (!tryReadLocation(loc)) {
        std::cout << "Location: unavailable (CsMenuManImp not resolved or chain broken)" << std::endl;
        return;
    }
    std::cout << "Location: mapId=" << loc.mapId << " underground=" << loc.underground
               << " mapX=" << loc.x << " mapY=" << loc.y << " oriDeg=" << loc.oriDeg << std::endl;
}

// Local client's own current camera facing via FieldArea - see the Camera comment
// in WorldChrMan.h for why this is not per-player-slot data, and why the exact
// axis/sign mapping of yawCos/yawSin is unverified until checked in-game.
bool WorldChrMan::tryReadCamera(Camera& out) {
    __try {
        if (!FieldAreaIns) {
            return false;
        }
        uintptr_t addr = *(uintptr_t*)FieldAreaIns;
        if (!addr) {
            return false;
        }
        addr = *(uintptr_t*)(addr + 0x20);
        if (!addr) {
            return false;
        }
        addr = *(uintptr_t*)(addr + 0x18);
        if (!addr) {
            return false;
        }
        out = *(Camera*)(addr + 0x10);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

void WorldChrMan::debugCamera() {
    Camera cam{};
    if (!tryReadCamera(cam)) {
        std::cout << "Camera: unavailable (FieldArea not resolved or chain broken)" << std::endl;
        return;
    }
    std::cout << "Camera: yawCos=" << cam.yawCos << " yawSin=" << cam.yawSin << std::endl;
}

