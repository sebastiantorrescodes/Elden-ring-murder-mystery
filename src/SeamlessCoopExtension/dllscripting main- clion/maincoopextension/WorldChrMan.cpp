#include "WorldChrMan.h"
#include "Signature.h"
#include "pch.h"
#include <iostream>


static void** WorldChrManIns;
static void** GameManIns;

//TODO: refactor GameMan's impl here
void** InitHelper() {
    ModuleData EldenRingData("eldenring.exe");
    Signature worldChrManSig = Signature("48 8B 05 ?? ?? ?? ?? 48 85 C0 74 0F 48 39 88");
    WorldChrManIns = (void**)worldChrManSig.Scan(&EldenRingData, 0x3, 0x7);

    Signature gameManSig = Signature("48 8B 05 ?? ?? ?? ?? 80 B8 ?? ?? ?? ?? 0D 0F 94 C0 C3");
    GameManIns = (void**)gameManSig.Scan(&EldenRingData, 0x3, 0x7);

    return WorldChrManIns;
}

// PointerChain::make stores its base argument by reference, so a null-safe fallback
// must still return an lvalue (a ternary would bind a temporary and fail to compile).
static uintptr_t& SafeDeref(void** add) {
    static uintptr_t zero = 0;
    if (add) return *(uintptr_t*)add;
    return zero;
}

WorldChrMan::WorldChrMan(void** add) : Address(add ? (void*)*(uintptr_t*)add : nullptr),
php(SafeDeref(add), 0x10EF8, 0, 0x190u, 0, 0x138),
pmhp(SafeDeref(add), 0x10EF8, 0, 0x190u, 0, 0x13C),
ianim(SafeDeref(add), 0x10EF8, 0, 0x190u, 0x58, 0x18),
canim(SafeDeref(add), 0x10EF8, 0, 0x190u, 0x58, 0x20),
lxcoord(SafeDeref(add), 0x10EF8, 0, 0x190u, 0x68, 0x70),
lycoord(SafeDeref(add), 0x10EF8, 0, 0x190u, 0x68, 0x78),
lzcoord(SafeDeref(add), 0x10EF8, 0, 0x190u, 0x68, 0x74) {
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

int WorldChrMan::getCurrentAnimIdForOtherPlayer(int playerSlot) {
    int64_t playerOffset = 0x10 * playerSlot;
    int animId = *PointerChain::make<int>(WorldChrManIns, 0x10EF8, playerOffset, 0x190, 0x18, 0x20);
    return animId;
}

PlayerCoords WorldChrMan::getCoordsForOtherPlayer(int playerSlot) {
    int64_t playerOffset = 0x10 * playerSlot;
    float x = *PointerChain::make<float>(WorldChrManIns, 0x10EF8, playerOffset, 0x190u, 0x68, 0x70);
    float y = *PointerChain::make<float>(WorldChrManIns, 0x10EF8, playerOffset, 0x190u, 0x68, 0x78);
    float z = *PointerChain::make<float>(WorldChrManIns, 0x10EF8, playerOffset, 0x190u, 0x68, 0x74);
    return { x, y, z };
}


int WorldChrMan::getPlayerSlotFromChrIns(ChrIns *chrIns) {

    for(int i = 0; i<getPlayerCount(); i++){
       ChrIns* arrayChrIns = PointerChain::make<PlayerStruct>(WorldChrManIns, 0x10EF8, i)->playerIns;
       if(arrayChrIns == chrIns) return i;
    }
    return -1;
}

/*
 * Least significant bit(pos 0) is on the right here.
 * Simple flag checker
 */
bool WorldChrMan::readBitFlag(int value, int pos){
    int mask = (1 << pos);
    return ((value & mask) != 0);
}

bool WorldChrMan::isInIframes(int playerSlot){
    int64_t playerOffset = 0x10 * playerSlot;
    int flagData = PointerChain::make<int>(WorldChrManIns, 0x10EF8, playerOffset, 0x190, 0x8, 0x40);
    return readBitFlag(flagData, 1);
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
// slot reports an error instead of crashing the whole game.
void WorldChrMan::debugSlot(int playerSlot) {
    __try {
        if (!WorldChrManIns) {
            std::cout << "Slot " << playerSlot << ": WorldChrManIns is null" << std::endl;
            return;
        }
        uintptr_t base = *(uintptr_t*)WorldChrManIns;
        base = *(uintptr_t*)(base + 0x10EF8);
        base = base + (0x10 * playerSlot);
        base = *(uintptr_t*)base;
        if (!base) {
            std::cout << "Slot " << playerSlot << ": empty (null sub-pointer)" << std::endl;
            return;
        }
        base = *(uintptr_t*)(base + 0x190);
        base = *(uintptr_t*)(base + 0x68);
        float x = *(float*)(base + 0x70);
        float y = *(float*)(base + 0x78);
        float z = *(float*)(base + 0x74);
        std::cout << "Slot " << playerSlot << " coords: x=" << x << " y=" << y << " z=" << z << std::endl;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        std::cout << "Slot " << playerSlot << ": read crashed (access violation), likely out of bounds" << std::endl;
    }
}

