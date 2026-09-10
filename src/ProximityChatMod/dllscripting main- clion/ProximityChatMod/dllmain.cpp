#include <windows.h>

#include "pch.h"
#include "Signature.h"
#include "WorldChrMan.h"
#include <iostream>
#include "TcpTestClient.h"
#include <sstream>
#include <cstdio>

// eldenring.exe is a GUI-subsystem process with no console of its own, so std::cout
// from this DLL has nowhere to go until a console is created and stdout/stderr are
// redirected into it. Ported from the old Visual Studio build's Logger.h, which did
// the same AllocConsole+freopen dance (there, gated to debug builds only - kept
// unconditional here since this whole DLL is a debug/dev tool).
static void AttachDebugConsole()
{
    AllocConsole();
    FILE* filePtr;
    freopen_s(&filePtr, "CONOUT$", "w", stdout);
    freopen_s(&filePtr, "CONOUT$", "w", stderr);
    SetConsoleTitleA("ProximityChatMod Debug Console");
}

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
    // Discarded return value - Make() is what runs the signature scan populating
    // WorldChrManIns/GameManIns/CsMenuManImpIns/FieldAreaIns, which every tryRead*
    // call below depends on. Needs to happen once up front so the TCP poll loop
    // works immediately, without waiting on a NUMPAD3 press.
    WorldChrMan::Make();

    TcpTestClient tcpTestClient("127.0.0.1", 9191);
    unsigned long long nextTcpPollMs = 0;

	while (true) {
        // Prints a single clean snapshot of the local player's own current stats -
        // deliberately local-player-only (no other slot probing): each participant
        // runs their own DLL+app instance and self-reports just their own state (see
        // the kLocalPlayerSlot comment above and the TCP poll loop below), so another
        // slot's raw data was never actually meaningful for this client to display.
        if (GetAsyncKeyState(VK_NUMPAD3) & 1) {
            if (!WorldChrMan::Make().isLoaded()) {
                std::cout << "Local player: WorldChrMan not loaded" << std::endl;
            } else {
                std::cout << "=== Local player ===" << std::endl;

                PlayerCoords coords{};
                if (WorldChrMan::tryReadSlot(kLocalPlayerSlot, coords)) {
                    std::cout << "Position: x=" << coords.x << " y=" << coords.y << " z=" << coords.z
                               << " hp=" << coords.hp << "/" << coords.maxHp << std::endl;
                } else {
                    std::cout << "Position: unavailable" << std::endl;
                }

                Location loc{};
                if (WorldChrMan::tryReadLocation(loc)) {
                    std::cout << "Map: mapX=" << loc.x << " mapY=" << loc.y << " oriDeg=" << loc.oriDeg
                               << " (mapId=" << loc.mapId << " underground=" << loc.underground << ")" << std::endl;
                } else {
                    std::cout << "Map: unavailable" << std::endl;
                }

                Camera cam{};
                if (WorldChrMan::tryReadCamera(cam)) {
                    std::cout << "Camera: yawCos=" << cam.yawCos << " yawSin=" << cam.yawSin << std::endl;
                } else {
                    std::cout << "Camera: unavailable" << std::endl;
                }

                MapRelativeCoords mapRel{};
                if (WorldChrMan::tryReadMapRelative(kLocalPlayerSlot, mapRel)) {
                    uint8_t areaId = (mapRel.map >> 24) & 0xFF;
                    uint8_t gridXNo = (mapRel.map >> 16) & 0xFF;
                    uint8_t gridZNo = (mapRel.map >> 8) & 0xFF;
                    std::cout << "MapRelative: x=" << mapRel.x << " y=" << mapRel.y << " z=" << mapRel.z
                               << " rad=" << mapRel.rad << " (areaId=0x" << std::hex << (int)areaId
                               << " gridX=0x" << (int)gridXNo << " gridZ=0x" << (int)gridZNo << std::dec << ")"
                               << std::endl;
                } else {
                    std::cout << "MapRelative: unavailable" << std::endl;
                }

                uint32_t blockId = 0;
                if (WorldChrMan::tryReadBlockId(blockId)) {
                    std::cout << "BlockId: " << blockId << std::endl;
                } else {
                    std::cout << "BlockId: unavailable" << std::endl;
                }
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
                          << " mapX=" << loc.x << " mapY=" << loc.y << " oriDeg=" << loc.oriDeg;
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

            // This client's own current map-relative position (see MapRelativeCoords
            // comment in WorldChrMan.h) - same "local client only, not per-slot"
            // pattern as the lines above, even though tryReadMapRelative itself is
            // per-slot capable. Unlike the slot=... line's raw PlayerCoords, x/y/z
            // here shouldn't reset when crossing a local map-tile boundary in the
            // open world (tryReadMapRelative applies the grid rebase internally) -
            // proximity.js's mapDistance uses y for elevation. x/z/rad/map aren't
            // currently consumed downstream (directional/panning audio, which used
            // to read x/z/rad here, was removed) - still sent in case direction is
            // ever revisited.
            MapRelativeCoords mapRel{};
            if (WorldChrMan::tryReadMapRelative(kLocalPlayerSlot, mapRel)) {
                std::ostringstream mapRelLine;
                mapRelLine << "mapRelative x=" << mapRel.x << " y=" << mapRel.y << " z=" << mapRel.z
                           << " rad=" << mapRel.rad;
                tcpTestClient.sendLine(mapRelLine.str());
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
		AttachDebugConsole();
		CreateThread(0, 0, &MainThread, 0, 0, NULL);
	}
	return 1;
}
