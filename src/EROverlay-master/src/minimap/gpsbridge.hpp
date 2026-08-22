#pragma once

#include <cstdint>

namespace er::minimap::bridge {

static constexpr uint32_t GPS_MAGIC = 0x47505331; // GPS1
static constexpr uint32_t GPS_VERSION = 1;
static constexpr int GPS_MAX_NPCS = 256;
static constexpr int GPS_MAX_GRACES = 512;

struct GpsNpc {
    uintptr_t address = 0;
    uint8_t alliance = 0;
    uint8_t reserved0[3] = {};
    int hp = 0;
    int maxHp = 0;
    float x = 0.f;
    float y = 0.f;
    float z = 0.f;
    float distance = 0.f;
    uint8_t isCurrentTarget = 0;
    uint8_t activeLock = 0;
    uint8_t reserved1[2] = {};
};

struct GpsGrace {
    float x = 0.f;
    float y = 0.f;
    float height = 0.f;
    float distance = 0.f;

    uint8_t active = 0;
    uint8_t reserved[3] = {};

    int spriteX = 0;
    int spriteY = 0;
    int spriteW = 0;
    int spriteH = 0;
    float spriteCenterX = 0.f;
    float spriteCenterY = 0.f;
};

struct GpsState {
    uint32_t magic = GPS_MAGIC;
    uint32_t version = GPS_VERSION;
    uint32_t frameCounter = 0;
    uint32_t valid = 0;
    uint32_t onGUI = 0;

    int32_t mapId = 0;
    int32_t layer = 0;
    int32_t underground = 0;
    int32_t reserved0 = 0;

    float mapX = 0.f;
    float mapY = 0.f;
    float mapOriDeg = 0.f;

    float playerCombatX = 0.f;
    float playerCombatY = 0.f;
    float playerCombatZ = 0.f;

    float cameraYawCos = 1.f;
    float cameraYawSin = 0.f;

    uint32_t targetValid = 0;
    uintptr_t targetAddress = 0;
    uint8_t targetAlliance = 0;
    uint8_t targetActiveLock = 0;
    uint8_t reserved1[2] = {};

    float targetX = 0.f;
    float targetY = 0.f;
    float targetZ = 0.f;
    int targetHp = 0;
    int targetMaxHp = 0;

    uint32_t npcCount = 0;
    GpsNpc npcs[GPS_MAX_NPCS] = {};

    uint32_t graceCount = 0;
    GpsGrace graces[GPS_MAX_GRACES] = {};
};

bool init();
void writeState();
void shutdownBridge();

} // namespace er::minimap::bridge
