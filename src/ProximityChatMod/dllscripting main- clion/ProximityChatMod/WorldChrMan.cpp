#include "WorldChrMan.h"
#include "Signature.h"
#include "GameVersion.h"
#include "PointerChain.h"
#include "pch.h"
#include <cmath>


static void** WorldChrManIns;
static void** GameManIns;
static void** CsMenuManImpIns;
static void** FieldAreaIns;

// Location struct offset shifted between game patches (matches EROverlay's own
// version-gated locationOffset_): 0x248 pre-1.12, 0x250 on 1.12+. Threshold is
// EROverlay's own packed-version constant, same format GetGameVersion() returns.
static size_t LocationOffset = 0x250;

// BlockId offset shift point is a DIFFERENT patch boundary than LocationOffset's
// (EROverlay's own threshold for this field is 0x0002000200000000ULL, vs
// LocationOffset's 0x0002000100000000ULL) - kept as two separate constants on
// purpose, not reused.
static size_t BlockIdOffset = 0xE8;

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
    BlockIdOffset = (gameVersion < 0x0002000200000000ULL) ? 0xE4 : 0xE8;

    return WorldChrManIns;
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

WorldChrMan::WorldChrMan(void** add) : Address(add ? (void*)*(uintptr_t*)add : nullptr) {
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

//try [[GameMan]+D60]+14(or +1C) as a total player in session value
//horribly hacky and unclean temp solution(not even a worldchrman aob)
int WorldChrMan::getPlayerCount(){
    int playerCount = PointerChain::make<int>(GameManIns, 0xD60, 0x1C);//either 0x1C or 0x14 here. Unsure which
    return playerCount;
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

        // Same base as the local player's hp chain used to derive, mirrored here since this probe bypasses PointerChain.
        uintptr_t hpBase = *(uintptr_t*)(chrBase + 0);
        out.hp = *(int*)(hpBase + 0x138);
        out.maxHp = *(int*)(hpBase + 0x13C);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// See the MapRelativeCoords comment in WorldChrMan.h - same per-slot base pointer as
// tryReadSlot, but stops one hop earlier (skips the +0x190/+0x68 detour into ChrIns's
// coord block) and reads a different, parallel struct instead. SEH-wrapped for the
// same reason as tryReadSlot: an out-of-bounds/empty slot should return false, not
// crash.
bool WorldChrMan::tryReadMapRelative(int playerSlot, MapRelativeCoords& out) {
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
        out.x = *(float*)(base + 0x6C0);
        out.y = *(float*)(base + 0x6C4);
        out.z = *(float*)(base + 0x6C8);
        out.rad = *(float*)(base + 0x6CC);
        out.map = *(uint32_t*)(base + 0x6D0);

        // Rebase x/z onto the open-world grid origin (area 0x3C/0x3D, 256-unit
        // chunks) so they're stable across a local map-tile boundary crossing - see
        // the MapRelativeCoords comment in WorldChrMan.h. Legacy areas are left
        // unrebased.
        uint8_t areaId = (out.map >> 24) & 0xFF;
        if (areaId == 0x3C || areaId == 0x3D) {
            uint8_t gridXNo = (out.map >> 16) & 0xFF;
            uint8_t gridZNo = (out.map >> 8) & 0xFF;
            out.x += 256.0f * gridXNo;
            out.z += 256.0f * gridZNo;
        }
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// Used to gate on underground being in [0, 8] here too (EROverlay only ever checks
// "underground & 1", so it should only ever be a small flag) - dropped after live
// testing showed underground reading obviously-garbage values (e.g. -697717248)
// on the exact same reads where mapX/mapY/oriDeg all came back completely
// plausible. That pattern doesn't look like a misaligned chain (which would
// scramble every field, not just one) - it looks like underground specifically is
// aliased onto the wrong bytes for this game version, while the rest of the struct
// lines up fine. Since the proximity server no longer uses mapId/underground for
// anything (mapX/mapY drive it directly - see proximity.js), that one broken,
// unused field was doing nothing but vetoing perfectly good mapX/mapY reads. Only
// mapX/mapY are validated now - the fields actually relied on downstream.
// No confirmed real-world range for mapX/mapY yet, so this just rejects outright
// garbage (NaN/Inf, or a magnitude far beyond anything the game world could
// plausibly span) from a misaligned LocationOffset - not a tightly-tuned bound.
// Tighten once real in-game mapX/mapY values have actually been observed.
constexpr float kMapCoordSanityBound = 1'000'000.0f;

static bool saneLocation(const Location& loc) {
    if (!std::isfinite(loc.x) || !std::isfinite(loc.y)) {
        return false;
    }
    if (std::fabs(loc.x) > kMapCoordSanityBound || std::fabs(loc.y) > kMapCoordSanityBound) {
        return false;
    }
    return true;
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
        if (!saneLocation(out)) {
            return false;
        }
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
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

// Local client's own current "block" id, ported from EROverlay's boss-tracking
// module - see the comment in WorldChrMan.h for why this is a much more granular
// area identifier than Location.mapId. Simpler chain than Location/Camera: one
// deref of FieldArea, then a direct offset read, no further pointer hops.
bool WorldChrMan::tryReadBlockId(uint32_t& out) {
    __try {
        if (!FieldAreaIns) {
            return false;
        }
        uintptr_t addr = *(uintptr_t*)FieldAreaIns;
        if (!addr) {
            return false;
        }
        out = *(uint32_t*)(addr + BlockIdOffset);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}
