#include <windows.h>

#include "pch.h"
#include "Signature.h"
#include "WorldChrMan.h"
#include <iostream>
#include "TcpTestClient.h"
#include <sstream>

// How many player slots to probe each poll cycle when broadcasting to the TCP test listener.
// Currently unused by the poll loop below (only the local slot is sent - see the
// commented-out multi-slot loop), kept for when/if that's re-enabled.
constexpr int kTcpPollSlotCount = 4;
constexpr unsigned long long kTcpPollIntervalMs = 250;
// Matches dll-protocol.ts's LOCAL_PLAYER_SLOT - slot 0 is structurally always the
// local player in the per-player array, never another peer.
constexpr int kLocalPlayerSlot = 0;

DWORD WINAPI MainThread(LPVOID lpParam)
{
    WorldChrMan mainBase = WorldChrMan::Make();

    TcpTestClient tcpTestClient("127.0.0.1", 9191);
    unsigned long long nextTcpPollMs = 0;

	while (true) {
        if (GetAsyncKeyState(VK_NUMPAD3) & 1) {
            WorldChrMan freshBase = WorldChrMan::Make();
            std::cout << "mainBase(startup) addr: " << std::hex << mainBase.GetAddress()
                       << ", freshBase(rescanned) addr: " << freshBase.GetAddress() << std::dec << std::endl;
            WorldChrMan::debugPlayerCount();
            WorldChrMan::debugLocation();
            WorldChrMan::debugCamera();
            WorldChrMan::debugBlockId();
            if (!freshBase.isLoaded()) {
                std::cout << "WorldChrMan not loaded, cannot read player coords" << std::endl;
            } else {
                int playerCount = WorldChrMan::getPlayerCount();
                std::cout << "Player count: " << playerCount << std::endl;
                for (int i = 0; i < playerCount; i++) {
                    WorldChrMan::debugSlot(i);
                }
                std::cout << "-- safe probe of slots 1-3 regardless of reported count --" << std::endl;
                WorldChrMan::debugSlot(1);
                WorldChrMan::debugSlot(2);
                WorldChrMan::debugSlot(3);
            }
        }

        unsigned long long nowMs = GetTickCount64();
        if (nowMs >= nextTcpPollMs) {
            nextTcpPollMs = nowMs + kTcpPollIntervalMs;

            // Only this client's own (local player) slot is sent - each participant
            // runs their own DLL+app instance and self-reports just their own state;
            // nothing downstream (App.tsx filters to LOCAL_PLAYER_SLOT) ever consumed
            // the other slots anyway. Left commented rather than deleted in case a
            // future host-authoritative feature (e.g. murder mystery gamemode logic)
            // wants everyone's data from one client's perspective again.
            /*
            for (int slot = 0; slot < kTcpPollSlotCount; slot++) {
                PlayerCoords coords{};
                if (WorldChrMan::tryReadSlot(slot, coords)) {
                    std::ostringstream line;
                    line << "slot=" << slot << " x=" << coords.x << " y=" << coords.y << " z=" << coords.z
                         << " hp=" << coords.hp << " maxHp=" << coords.maxHp;
                    tcpTestClient.sendLine(line.str());
                }
            }
            */
            PlayerCoords coords{};
            if (WorldChrMan::tryReadSlot(kLocalPlayerSlot, coords)) {
                std::ostringstream line;
                line << "slot=" << kLocalPlayerSlot << " x=" << coords.x << " y=" << coords.y << " z=" << coords.z
                     << " hp=" << coords.hp << " maxHp=" << coords.maxHp;
                tcpTestClient.sendLine(line.str());
            }

            // This client's own current map/area - not per-slot data (see Location
            // comment in WorldChrMan.h), so it's sent as its own line once per poll
            // cycle rather than repeated on every slot= line above.
            Location loc{};
            if (WorldChrMan::tryReadLocation(loc)) {
                std::ostringstream areaLine;
                areaLine << "area mapId=" << loc.mapId << " underground=" << loc.underground
                          << " mapX=" << loc.x << " mapY=" << loc.y;
                tcpTestClient.sendLine(areaLine.str());
            }

            // This client's own current camera facing - same "local client only, not
            // per-slot" reasoning as the area line above.
            Camera cam{};
            if (WorldChrMan::tryReadCamera(cam)) {
                std::ostringstream cameraLine;
                cameraLine << "camera yawCos=" << cam.yawCos << " yawSin=" << cam.yawSin;
                tcpTestClient.sendLine(cameraLine.str());
            }

            // This client's own current "block" id - see the comment in WorldChrMan.h
            // for why this is a separate, more granular signal than area mapId=...
            // above. Sent unconditionally (no sanity check yet) since we don't have
            // enough live data yet to know what a plausible range even looks like.
            uint32_t blockId = 0;
            if (WorldChrMan::tryReadBlockId(blockId)) {
                std::ostringstream blockLine;
                blockLine << "block blockId=" << blockId;
                tcpTestClient.sendLine(blockLine.str());
            }
        }
	}
	return 0;
}

BOOL WINAPI DllMain(HINSTANCE module, DWORD reason, LPVOID)
{
	if (reason == DLL_PROCESS_ATTACH)
	{
		DisableThreadLibraryCalls(module);
		CreateThread(0, 0, &MainThread, 0, 0, NULL);
	}
	return 1;
}
