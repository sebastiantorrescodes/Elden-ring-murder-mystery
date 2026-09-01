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
