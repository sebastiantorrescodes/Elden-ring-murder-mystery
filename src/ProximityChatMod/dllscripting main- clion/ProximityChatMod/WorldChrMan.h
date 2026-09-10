#ifndef _WORLD_CHR_MAN_H_
#define _WORLD_CHR_MAN_H_

#include <cstdint>

struct PlayerCoords {
    float x;
    float y;
    float z;
    int hp;
    int maxHp;
};

// Local client's own current map/area, via CsMenuManImp - NOT per-player-slot data.
// Two players can only be meaningfully compared by raw ChrIns coords (PlayerCoords)
// when their mapId/underground both match; otherwise they're in different map
// instances (e.g. one in a legacy dungeon/catacomb/DLC realm) and raw coordinate
// distance between them is meaningless. Same layout as EROverlay's Location struct.
struct Location {
    int32_t mapId;
    float x;
    float y;
    int32_t underground;
    float oriDeg;
};

// This client's own current camera facing direction, via FieldArea - same "local
// client only, not per-player-slot" caveat as Location. yawCos/yawSin together give
// a 2D facing angle; which world axis "yaw=0" points along is not yet verified
// in-game (ported from EROverlay, same as the other signatures here) - treat the
// exact axis/sign mapping as unconfirmed until checked against real movement.
struct Camera {
    float yawCos;
    int32_t dummy0;
    float yawSin;
    int32_t dummy1;
};

// Same per-player base pointer as PlayerCoords (WorldChrMan+0x10EF8 -> per-slot
// entry), but read at a different offset instead of detouring through +0x190/+0x68
// into ChrIns's coord block - a second, parallel struct on the same object. Ported
// from tremwil (minusone12)'s "map-relative teleport" Cheat Engine script (coords
// credited to Pav there). x/z are relative to a 256-unit grid tile named by `map`
// (area/gridX/gridZ packed into its bytes, matching this repo's own m<area>_<gridX>_
// <gridZ>_<variant> map file naming) - see that script's getAbsoluteCoords for the
// +256*gridXNo/+256*gridZNo rebase needed to compare x/z across tiles. y is NOT
// tile-relative for the open world (area 0x3C/0x3D) per that same function - unlike
// PlayerCoords.y, it should need no rebase to stay comparable across a tile
// boundary. Unverified in this codebase until checked live against a known tile
// crossing (same bar as Camera's yawCos/yawSin axis mapping).
struct MapRelativeCoords {
    float x;
    float y;
    float z;
    float rad;
    uint32_t map;
};

class Address {
protected:
    void* address;
public:
    Address(void* address) :address(address) {};
    void* GetAddress() {
        return address;
    }
};

class WorldChrMan : public Address
{
private:
    WorldChrMan(void** address);

public:
    static WorldChrMan Make();
    bool isLoaded();
    static int getPlayerCount();
    static void debugPlayerCount();
    static void debugSlot(int playerSlot);
    // Guarded raw pointer walk (SEH, not PointerChain) so an out-of-bounds/empty
    // slot returns false instead of crashing. Shared by debugSlot and the TCP poll loop.
    static bool tryReadSlot(int playerSlot, PlayerCoords& out);

    // See the MapRelativeCoords comment above - per-slot like PlayerCoords (works for
    // any probed slot, not just the local player), just a different offset off the
    // same base pointer. Same SEH-guarded-slot-walk reasoning as tryReadSlot/debugSlot.
    static bool tryReadMapRelative(int playerSlot, MapRelativeCoords& out);
    static void debugMapRelative(int playerSlot);

    // Local client's own current map/area (see Location comment above), resolved via
    // CsMenuManImp - a separate global pointer chain from WorldChrMan/GameMan.
    static bool tryReadLocation(Location& out);
    static void debugLocation();

    // Local client's own current camera facing (see Camera comment above), resolved
    // via FieldArea - another separate global pointer chain.
    static bool tryReadCamera(Camera& out);
    static void debugCamera();

    // This client's own current "block" id - a much more granular area identifier
    // than Location.mapId (which only ever distinguishes base game vs DLC). Ported
    // from EROverlay's boss-tracking module, which divides this by 1000 to bucket
    // it into named regions (Limgrave, Liurnia, etc.) - the raw value is likely even
    // more granular than that, closer to "which specific loaded instance" than
    // Location.mapId is. Also resolved via FieldArea, one deref plus a direct offset
    // read (no further chain). Unlike Location/Camera, this hasn't been sanity-
    // checked against live data yet - treat the plausible range as unconfirmed.
    static bool tryReadBlockId(uint32_t& out);
    static void debugBlockId();
};
#endif // _WORLD_CHR_MAN_H_
