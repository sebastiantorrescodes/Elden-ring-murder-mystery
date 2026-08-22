//data.cpp

#include "data.hpp"
#include "gpsbridge.hpp"

#include "defs/BonfireWarpParam.h"
#include "defs/WorldMapLegacyConvParam.h"
#include "defs/WorldMapPointParam.h"

#include "api.h"
#include "params/param.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <numbers>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>
#include <windows.h>
#include <cstring>

extern EROverlayAPI *api;

namespace er::minimap {

Data gData;
Atlas gAtlas;

void Atlas::load(const wchar_t *basePath) {
    std::wstring path = std::wstring(basePath) + L"\\data\\map\\atlas.json";

    FILE *f = _wfopen(path.c_str(), L"rb");
    if (!f) return;

    fseek(f, 0, SEEK_END);
    auto size = static_cast<size_t>(ftell(f));
    fseek(f, 0, SEEK_SET);
    std::vector<char> buf(size);
    fread(buf.data(), 1, size, f);
    fclose(f);

    std::string content(buf.data(), size);
    auto j = nlohmann::json::parse(content, nullptr, false);
    if (j.is_discarded() || !j.contains("atlases") || !j["atlases"].is_array()) return;

    atlases_.clear();
    for (auto &ja : j["atlases"]) {
        auto &atlas = atlases_.emplace_back();
        auto filename = ja.value("file", "");
        atlas.filePath = std::wstring(basePath) + L"\\data\\map\\" + std::wstring(filename.begin(), filename.end());
        atlas.width = ja.value("width", 0);
        atlas.height = ja.value("height", 0);

        if (ja.contains("sprites") && ja["sprites"].is_array()) {
            for (auto &js : ja["sprites"]) {
                auto &sprite = atlas.sprites.emplace_back();
                sprite.name = js.value("name", "");
                sprite.x = js.value("x", 0);
                sprite.y = js.value("y", 0);
                sprite.width = js.value("width", 0);
                sprite.height = js.value("height", 0);
                sprite.u0 = sprite.x / (float)atlas.width;
                sprite.v0 = sprite.y / (float)atlas.height;
                sprite.u1 = (sprite.x + sprite.width) / (float)atlas.width;
                sprite.v1 = (sprite.y + sprite.height) / (float)atlas.height;
                sprite.centerX = js.contains("centerX") ? js["centerX"].get<float>() : sprite.width / 2.0f;
                sprite.centerY = js.contains("centerY") ? js["centerY"].get<float>() : sprite.height / 2.0f;
                sprite.texture = nullptr;
                atlas.spriteIndex[sprite.name] = atlas.sprites.size() - 1;
            }
        }
    }
}

void Atlas::loadTextures() {
    if (texturesLoaded_) return;
    texturesLoaded_ = true;
    for (auto &atlas : atlases_) {
        if (atlas.texture.loaded) continue;
        atlas.texture = api->loadTexture(atlas.filePath.c_str());
        for (auto &sprite : atlas.sprites) {
            sprite.texture = &atlas.texture;
        }
    }
}

const SpriteInfo *Atlas::findSprite(const std::string &name) const {
    for (auto &atlas : atlases_) {
        auto it = atlas.spriteIndex.find(name);
        if (it != atlas.spriteIndex.end()) {
            return &atlas.sprites[it->second];
        }
    }
    return nullptr;
}

bool DecorationInfo::isUnlocked() const {
    if (eventFlagAddress == 0) {
        eventFlagAddress = api->resolveFlagAddress(eventFlagId, &eventFlagBits);
        if (eventFlagAddress == 0) {
            eventFlagAddress = (uintptr_t)-1;
            return false;
        }
    } else if (eventFlagAddress == (uintptr_t)-1) {
        return false;
    }
    return (*(uint8_t *)(eventFlagAddress) & eventFlagBits) != 0;
}

void Data::load() {
    std::thread th([this]() {
        const void *t = api->paramFindTable(L"WorldMapLegacyConvParam");
        if (t == nullptr) {
            fwprintf(stderr, L"Unable to find WorldMapLegacyConvParam\n");
            return;
        }
        struct ConvValue {
            uint8_t u;
            uint8_t v;
            uint8_t w;
            float x;
            float y;
            float z;
        };
        auto buildKey = [](uint8_t area, uint8_t gridX, uint8_t gridZ) {
            return (uint32_t)area * 10000 + (uint32_t)gridX * 100 + (uint32_t)gridZ;
        };
        std::unordered_map<uint32_t, ConvValue> convTable;
        paramTableIterateBegin(t, WorldMapLegacyConvParam, wmlcp) {
            auto u0 = wmlcp->srcAreaNo;
            auto u1 = wmlcp->dstAreaNo;
            if (u0 != 60 && u0 != 61 && (u1 == 60 || u1 == 61)) {
                auto key = buildKey(u0, wmlcp->srcGridXNo, wmlcp->srcGridZNo);
                convTable[key] = { u1, wmlcp->dstGridXNo, wmlcp->dstGridZNo, wmlcp->dstPosX - wmlcp->srcPosX, wmlcp->dstPosY - wmlcp->srcPosY, wmlcp->dstPosZ - wmlcp->srcPosZ };
            }
        } paramTableIterateEnd();
        paramTableIterateBegin(t, WorldMapLegacyConvParam, wmlcp) {
            auto u0 = wmlcp->srcAreaNo;
            auto u1 = wmlcp->dstAreaNo;
            if (u0 != 60 && u0 != 61 && u1 != 60 && u1 != 61) {
                auto key = buildKey(u0, wmlcp->srcGridXNo, wmlcp->srcGridZNo);
                auto key2 = buildKey(u1, wmlcp->dstGridXNo, wmlcp->dstGridZNo);
                auto it1 = convTable.find(key);
                auto it2 = convTable.find(key2);
                if (it1 == convTable.end() && it2 != convTable.end()) {
                    auto &c = it2->second;
                    convTable[key] = {c.u, c.v, c.w, c.x + wmlcp->dstPosX - wmlcp->srcPosX, c.y + wmlcp->dstPosY - wmlcp->srcPosY, c.z + wmlcp->dstPosZ - wmlcp->srcPosZ};
                }
                if (it2 == convTable.end() && it1 != convTable.end()) {
                    auto &c = it1->second;
                    convTable[key2] = {c.u, c.v, c.w, c.x + wmlcp->srcPosX - wmlcp->dstPosX, c.y + wmlcp->srcPosY - wmlcp->dstPosY, c.z + wmlcp->srcPosZ - wmlcp->dstPosZ};
                }
            }
        } paramTableIterateEnd();

        for (auto i = 0; i < 3; ++i) {
            decorations_[i].clear();
            decorationsAround_[i].clear();
            decorationsAround_[i].resize(100);
        }
        auto addDecoration = [&](uint64_t id, int32_t layer, uint16_t iconId, uint32_t eventFlagId, uint8_t areaNo, uint8_t gridXNo, uint8_t gridZNo, float posX, float posY, float posZ, float rotationDeg, DecorationSource source) {
            if (layer == -1) return;
            if (iconId == 0) return;
            char iconIdStr[16];
            sprintf(iconIdStr, "%02d", iconId);
            auto *sprite = gAtlas.findSprite(iconIdStr);
            if (sprite == nullptr) return;
            float worldX, worldZ, worldH;
            if (areaNo == 60 || areaNo == 61) {
                worldX = (gridXNo - 28) * 256.0f + 128.0f + posX;
                worldZ = (64 - gridZNo) * 256.0f + 128.0f - posZ;
                worldH = posY;
                if (layer == 2) {
                    worldX -= 3035.0f;
                    worldZ -= 1864.0f;
                }
            } else {
                auto key = buildKey(areaNo, gridXNo, gridZNo);
                auto convIt = convTable.find(key);
                if (convIt != convTable.end()) {
                    auto &c = convIt->second;
                    worldX = (c.v - 28) * 256.0f + 128.0f + posX + c.x;
                    worldZ = (64 - c.w) * 256.0f + 128.0f - posZ - c.z;
                    worldH = posY + c.y;
                    if (layer == 2) {
                        worldX -= 3035.0f;
                        worldZ -= 1864.0f;
                    }
                } else {
                    return;
                }
            }
            auto &g = decorations_[layer].emplace_back();
            g.x = worldX;
            g.y = worldZ;
            g.height = worldH;
            g.id = id;
            g.eventFlagId = eventFlagId;
            g.layer = layer;
            g.source = source;
            int areaX = (int32_t)std::floor(g.x) / 1024;
            int areaY = (int32_t)std::floor(g.y) / 1024;
            g.localX = g.x - areaX * 1024;
            g.localY = g.y - areaY * 1024;
            g.rotationRad = rotationDeg == 0.f ? 0.f : static_cast<float>((rotationDeg + 180.0) * std::numbers::pi / 180.0);
            g.sortKey = areaY * 10 + areaX;
            g.sprite = sprite;
        };
        t = api->paramFindTable(L"BonfireWarpParam");
        if (t == nullptr) {
            fwprintf(stderr, L"Unable to find BonfireWarpParam\n");
            return;
        }
        paramTableIterateBegin(t, BonfireWarpParam, bwp) {
            int32_t layer = bwp->dispMask00 ? 0 : bwp->dispMask01 ? 1 : bwp->dispMask02 ? 2 : -1;
            addDecoration(entry->paramId, layer, bwp->iconId, bwp->eventflagId, bwp->areaNo, bwp->gridXNo, bwp->gridZNo, bwp->posX, bwp->posY, bwp->posZ, 0.f, DecorationSource::Grace);
        } paramTableIterateEnd();
        t = api->paramFindTable(L"WorldMapPointParam");
        if (t == nullptr) {
            fwprintf(stderr, L"Unable to find WorldMapPointParam\n");
            return;
        }
        paramTableIterateBegin(t, WorldMapPointParam, wmpp) {
            /* 80 is the icon id for the NPCs, I don't want to show them as now, because its condition fields are a bit silly */
            if (wmpp->iconId == 80) continue;
            int32_t layer = wmpp->dispMask00 ? 0 : wmpp->dispMask01 ? 1 : wmpp->dispMask02 ? 2 : -1;
            addDecoration(entry->paramId, layer, wmpp->iconId, wmpp->eventFlagId, wmpp->areaNo, wmpp->gridXNo, wmpp->gridZNo, wmpp->posX, wmpp->posY, wmpp->posZ, wmpp->angle, DecorationSource::Landmark);
        } paramTableIterateEnd();
        for (auto i = 0; i < 3; ++i) {
            auto &l = decorations_[i];
            if (l.empty()) continue;
            std::sort(l.begin(), l.end(), [](const auto &a, const auto &b) {
                if (a.sortKey == b.sortKey) {
                    if (a.source == b.source) {
                        return a.id > b.id;
                    }
                    return a.source > b.source;
                }
                return a.sortKey < b.sortKey;
            });
            auto sz = l.size();
            auto lastSortKey = -1;
            const auto *lastDecoration = &l.front();
            auto &garound = decorationsAround_[i];
            for (auto &g : l) {
                if (g.sortKey != lastSortKey) {
                    if (lastSortKey != -1) {
                        if ((size_t)lastSortKey >= garound.size()) {
                            garound.resize((size_t)(lastSortKey + 1));
                        }
                        garound[lastSortKey] = {lastDecoration, &g};
                    }
                    lastSortKey = g.sortKey;
                    lastDecoration = &g;
                }
            }
            if (lastSortKey != -1) {
                if ((size_t)lastSortKey >= garound.size()) {
                    garound.resize((size_t)(lastSortKey + 1));
                }
                garound[lastSortKey] = {lastDecoration, l.data() + sz};
            }
        }
        paramsLoaded_ = true;
    });
    th.detach();
    auto gameAddresses = api->getGameAddresses();
    csMenuManImp_ = gameAddresses.csMenuManImp;
    fieldArea_ = gameAddresses.fieldArea;
    if (api->getGameVersion() < 0x0002000100000000ULL) {
        // 1.02 ~ 1.10.1
        locationOffset_ = 0x248;
    } else {
        // 1.12+
        locationOffset_ = 0x250;
    }
}

static bool saneCoord(float x, float y, float z) {
    if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z)) {
        return false;
    }

    // Reject zero/near-zero fake reads.
    if (std::abs(x) < 0.001f && std::abs(y) < 0.001f && std::abs(z) < 0.001f) {
        return false;
    }

    // Reject nonsense reads like -666294090099379790675968.
    if (std::abs(x) > 100000.f || std::abs(y) > 100000.f || std::abs(z) > 100000.f) {
        return false;
    }

    return true;
}

static bool tryReadPlayerCoords(uintptr_t worldChrMan, bool derefWorldChrMan, bool derefCoordBase, float &outX, float &outY, float &outZ) {
    __try {
        uintptr_t chrMan = derefWorldChrMan ? *(uintptr_t *)worldChrMan : worldChrMan;
        if (chrMan == 0)
            return false;

        // Hexinton local player path:
        // [WorldChrMan] + LocalPlayerOffset / 0x10EF8
        auto localPlayer = *(uintptr_t *)(chrMan + 0x10EF8);
        if (localPlayer == 0)
            return false;

        auto p1 = *(uintptr_t *)(localPlayer + 0x0);
        if (p1 == 0)
            return false;

        auto p2 = *(uintptr_t *)(p1 + 0x190);
        if (p2 == 0)
            return false;

        uintptr_t coordBase = derefCoordBase ? *(uintptr_t *)(p2 + 0x68) : (p2 + 0x68);

        if (coordBase == 0)
            return false;

        float x = *(float *)(coordBase + 0x70);
        float y = *(float *)(coordBase + 0x74);
        float z = *(float *)(coordBase + 0x78);

        if (!saneCoord(x, y, z))
            return false;

        outX = x;
        outY = y;
        outZ = z;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

static bool tryReadPlayerStats(uintptr_t worldChrMan, int &outHp, int &outMaxHp) {
    __try {
        uintptr_t chrMan = *(uintptr_t *)worldChrMan;
        if (chrMan == 0)
            return false;

        auto localPlayer = *(uintptr_t *)(chrMan + 0x10EF8);
        if (localPlayer == 0)
            return false;

        auto p1 = *(uintptr_t *)(localPlayer + 0x0);
        if (p1 == 0)
            return false;

        auto chrModule = *(uintptr_t *)(p1 + 0x190);
        if (chrModule == 0)
            return false;

        auto statsBase = *(uintptr_t *)(chrModule + 0x00);
        if (statsBase == 0)
            return false;

        int hp = *(int *)(statsBase + 0x138);
        int maxHp = *(int *)(statsBase + 0x13C);

        if (maxHp <= 0 || maxHp > 99999)
            return false;

        outHp = hp;
        outMaxHp = maxHp;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

void Data::update() {
    if (csMenuManImp_ == 0) { onGUI_ = true; return; }
    auto addr = *(uintptr_t*)csMenuManImp_;
    if (addr == 0) {
        onGUI_ = true;
        return;
    }
    onGUI_ = api->screenState() != 0 || *reinterpret_cast<uint32_t*>(addr + 0x1C) != 0;

    addr = *(uintptr_t*)(addr + 0x80);
    if (addr != 0) {
        addr = *(uintptr_t*)(addr + locationOffset_);
        if (addr != 0) {
            location_ = *(Location*)(addr + 0x24);
        }
    }
    addr = *(uintptr_t*)(fieldArea_);
    if (addr != 0) {
        addr = *(uintptr_t*)(addr + 0x20);
        if (addr != 0) {
            addr = *(uintptr_t*)(addr + 0x18);
            if (addr != 0) {
                camera_ = *(Camera*)(addr + 0x10);
            }
        }
    }
    playerCombatX_ = 0.f;
    playerCombatY_ = 0.f;
    playerCombatZ_ = 0.f;

    auto gameAddresses = api->getGameAddresses();
    auto worldChrMan = gameAddresses.worldChrMan;

    if (worldChrMan != 0) {
        float x = 0.f;
        float y = 0.f;
        float z = 0.f;

        int hp = 1;
        int maxHp = 1;

        if (tryReadPlayerStats(worldChrMan, hp, maxHp)) {
            playerHp_ = hp;
            playerMaxHp_ = maxHp;
        } else {
            playerHp_ = 1;
            playerMaxHp_ = 1;
        }

        if (tryReadPlayerCoords(worldChrMan, true, true, x, y, z) || tryReadPlayerCoords(worldChrMan, true, false, x, y, z) ||
            tryReadPlayerCoords(worldChrMan, false, true, x, y, z) || tryReadPlayerCoords(worldChrMan, false, false, x, y, z)) {
            playerCombatX_ = x;
            playerCombatY_ = y;
            playerCombatZ_ = z;
        } else {
            playerCombatX_ = 0.f;
            playerCombatY_ = 0.f;
            playerCombatZ_ = 0.f;
        }

        // Nearby NPC/enemy radar scan.
        // First test: scan loaded WorldChrMan character list every ~15 update ticks.
        npcScanTick_++;

        if (npcScanTick_ >= 1) {
            npcScanTick_ = 0;
            nearbyNpcs_.clear();

            auto gameAddressesNpc = api->getGameAddresses();
            auto worldChrManNpc = gameAddressesNpc.worldChrMan;

            const float playerX = playerCombatX_;
            const float playerY = playerCombatY_;
            const float playerZ = playerCombatZ_;

            if (worldChrManNpc != 0 && !(playerX == 0.f && playerZ == 0.f)) {
                __try {
                    auto chrMan = *(uintptr_t *)worldChrManNpc;
                    if (chrMan != 0) {
                        auto begin = *(uintptr_t *)(chrMan + 0x1F1B8);
                        auto finish = *(uintptr_t *)(chrMan + 0x1F1C0);

                        if (begin != 0 && finish != 0 && finish > begin) {
                            size_t count = (finish - begin) / 8;

                            // Safety cap for first test.
                            if (count > 512) {
                                count = 512;
                            }

                            for (size_t i = 0; i < count; i++) {
                                auto chr = *(uintptr_t *)(begin + i * 8);
                                if (chr < 0x10000) {
                                    continue;
                                }

                                uint8_t alliance = *(uint8_t *)(chr + 0x6C);

                                auto shouldShowAlliance = [](uint8_t a) {
                                    switch (a) {
                                        // Friendly / non-hostile NPCs, shown green in GPSMapDX.
                                        case 26: // Friendly NPC
                                            return true;

                                        // Hostile / enemy-like alliances, shown red.
                                        case 6:
                                        case 7:
                                        case 9:
                                        case 27: // Hostile NPC
                                        case 33:
                                        case 48:
                                        case 51:
                                        case 52:
                                        case 60:
                                        case 63:
                                            return true;

                                        default:
                                            return false;
                                    }
                                };

                                if (!shouldShowAlliance(alliance)) {
                                    continue;
                                }

                                auto chrModule = *(uintptr_t *)(chr + 0x190);
                                if (chrModule == 0) {
                                    continue;
                                }

                                auto statsBase = *(uintptr_t *)(chrModule + 0x00);
                                auto coordBase = *(uintptr_t *)(chrModule + 0x68);

                                if (statsBase == 0 || coordBase == 0) {
                                    continue;
                                }

                                int hp = *(int *)(statsBase + 0x138);
                                int maxHp = *(int *)(statsBase + 0x13C);

                                if (hp <= 0 || maxHp <= 0) {
                                    continue;
                                }

                                float x = *(float *)(coordBase + 0x70);
                                float y = *(float *)(coordBase + 0x74);
                                float z = *(float *)(coordBase + 0x78);

                                if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z)) {
                                    continue;
                                }

                                float dx = x - playerX;
                                float dy = y - playerY;
                                float dz = z - playerZ;

                                // Height gate: ignore enemies far above/below the player.
                                if (std::abs(dy) > 25.0f) {
                                    continue;
                                }

                                // Use mostly horizontal distance for radar filtering.
                                float distance = std::sqrt(dx * dx + dz * dz);

                                NearbyNpcInfo npc;
                                npc.address = chr;
                                npc.alliance = alliance;
                                npc.hp = hp;
                                npc.maxHp = maxHp;
                                npc.x = x;
                                npc.y = y;
                                npc.z = z;
                                npc.distance = distance;

                                nearbyNpcs_.push_back(npc);
                            }
                        }
                    }
                } __except (EXCEPTION_EXECUTE_HANDLER) {
                    nearbyNpcs_.clear();
                }
            }
        }
    }

    target_ = {};

    auto gameAddresses2 = api->getGameAddresses();
    auto targetBase = gameAddresses2.lastLockOnTarget;
    auto lockSerial = gameAddresses2.lastLockOnTargetSerial;

    if (lockSerial != lastLockOnSerialSeen_) {
        lastLockOnSerialSeen_ = lockSerial;
        activeLockFreshTicks_ = 2; // about a short grace window
    } else if (activeLockFreshTicks_ > 0) {
        activeLockFreshTicks_--;
    }

    if (targetBase != 0) {
        __try {
            target_.address = targetBase;
            target_.alliance = *(uint8_t *)(targetBase + 0x6C);
            target_.activeLock = activeLockFreshTicks_ > 0;

            auto sharedBlock = *(uintptr_t *)(targetBase + 0x190);
            if (sharedBlock != 0) {
                auto coordBase = *(uintptr_t *)(sharedBlock + 0x68);
                auto statsBase = *(uintptr_t *)(sharedBlock + 0x00);

                if (coordBase != 0) {
                    target_.azimuth = *(float *)(coordBase + 0x54);
                    target_.x = *(float *)(coordBase + 0x70);
                    target_.y = *(float *)(coordBase + 0x74);
                    target_.z = *(float *)(coordBase + 0x78);
                    target_.valid = true;
                }

                if (statsBase != 0) {
                    target_.hp = *(int *)(statsBase + 0x138);
                    target_.maxHp = *(int *)(statsBase + 0x13C);

                    if (target_.hp <= 0 || target_.maxHp <= 0) {
                        target_.valid = false;
                    }
                }
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            target_ = {};
        }
    }
}

std::tuple<const DecorationInfo *, const DecorationInfo *> Data::decorationsAround(int32_t layer, int u, int v) const {
    size_t sortKey = (size_t)(v * 10 + u);
    auto &decorations = decorationsAround_[layer];
    if (sortKey >= decorations.size()) {
        return { nullptr, nullptr };
    }
    return decorations[sortKey];
}

}

namespace er::minimap::bridge {

static HANDLE gMapping = nullptr;
static GpsState *gState = nullptr;

bool init() {
    if (gState) {
        return true;
    }

    gMapping = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, sizeof(GpsState), L"Local\\ERMapOnlyGPSBridge");

    if (!gMapping) {
        return false;
    }

    gState = reinterpret_cast<GpsState *>(MapViewOfFile(gMapping, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(GpsState)));

    if (!gState) {
        CloseHandle(gMapping);
        gMapping = nullptr;
        return false;
    }

    std::memset(gState, 0, sizeof(GpsState));
    gState->magic = GPS_MAGIC;
    gState->version = GPS_VERSION;

    return true;
}

void writeState() {
    if (!gState && !init()) {
        return;
    }

    GpsState next = {};

    next.magic = GPS_MAGIC;
    next.version = GPS_VERSION;
    next.frameCounter = gState->frameCounter + 1;

    const auto &location = gData.location();
    const auto &camera = gData.camera();
    const auto &target = gData.target();
    const auto &npcs = gData.nearbyNpcs();

    next.valid = location.x != 0.f ? 1u : 0u;
    next.onGUI = gData.onGUI() ? 1u : 0u;

    next.mapId = location.mapId;
    next.underground = location.underground;
    next.mapX = location.x;
    next.mapY = location.y;
    next.mapOriDeg = location.oriDeg;

    if (location.mapId == 10) {
        next.layer = 2;
    } else if (location.mapId == 0) {
        next.layer = (location.underground & 1) ? 1 : 0;
    } else {
        next.layer = -1;
    }

    next.playerCombatX = gData.playerCombatX();
    next.playerCombatY = gData.playerCombatY();
    next.playerCombatZ = gData.playerCombatZ();

    next.cameraYawCos = camera.yawCos;
    next.cameraYawSin = camera.yawSin;

    next.targetValid = target.valid ? 1u : 0u;
    next.targetAddress = target.address;
    next.targetAlliance = target.alliance;
    next.targetActiveLock = target.activeLock ? 1u : 0u;
    next.targetX = target.x;
    next.targetY = target.y;
    next.targetZ = target.z;
    next.targetHp = target.hp;
    next.targetMaxHp = target.maxHp;

    const uint32_t count = static_cast<uint32_t>(std::min<size_t>(npcs.size(), GPS_MAX_NPCS));

    next.npcCount = count;

    for (uint32_t i = 0; i < count; i++) {
        const auto &src = npcs[i];
        auto &dst = next.npcs[i];

        dst.address = src.address;
        dst.alliance = src.alliance;
        dst.hp = src.hp;
        dst.maxHp = src.maxHp;
        dst.x = src.x;
        dst.y = src.y;
        dst.z = src.z;
        dst.distance = src.distance;

        if (target.valid && src.address == target.address) {
            dst.isCurrentTarget = 1;
            dst.activeLock = target.activeLock ? 1 : 0;
        }
    }

    // Export activated/unlocked graces only.
    if (next.layer >= 0 && next.layer < 3) {
        const auto &graces = gData.decorations(next.layer);

        uint32_t graceOut = 0;

        for (const auto &deco: graces) {
            if (graceOut >= GPS_MAX_GRACES) {
                break;
            }

            if (deco.source != DecorationSource::Grace) {
                continue;
            }

            const bool unlocked = deco.isUnlocked();

            const float dx = deco.x - next.mapX;
            const float dy = deco.y - next.mapY;
            const float dist = std::sqrt(dx * dx + dy * dy);

            auto& out = next.graces[graceOut++];
            out.x = deco.x;
            out.y = deco.y;
            out.height = deco.height;
            out.distance = dist;
            out.active = unlocked ? 1 : 0;

            if (deco.sprite) {
                out.spriteX = deco.sprite->x;
                out.spriteY = deco.sprite->y;
                out.spriteW = deco.sprite->width;
                out.spriteH = deco.sprite->height;
                out.spriteCenterX = deco.sprite->centerX;
                out.spriteCenterY = deco.sprite->centerY;
            }
        }

        next.graceCount = graceOut;
    }

    std::memcpy(gState, &next, sizeof(GpsState));
}

void shutdownBridge() {
    if (gState) {
        UnmapViewOfFile(gState);
        gState = nullptr;
    }

    if (gMapping) {
        CloseHandle(gMapping);
        gMapping = nullptr;
    }
}

} // namespace er::minimap::bridge
