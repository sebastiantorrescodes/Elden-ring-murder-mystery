#include "pch.h"

#include <Windows.h>
#include <cstdint>
#include <fstream>
#include <string>
#include <vector>
#include <sstream>
#include <iomanip>
#include <cstdlib>
#include <map>
#include "MMTcpClient.h"

// ============================================================
// Murder Mystery v1.3.2
// Elden Ring 1.17 Updated
// ============================================================

namespace MM
{
    // ============================================================
    // Proper equipment system
    // ============================================================

    constexpr int SLOT_PRIMARY_RIGHT = 1;
    constexpr int SLOT_SECONDARY_RIGHT = 3;
    constexpr int SLOT_TERTIARY_RIGHT = 5;

    constexpr int SLOT_TALISMAN_1 = 17;

    constexpr int32_t FIST_WEAPON = 110000;
    constexpr int32_t MISERICORDE = 1030000;
    constexpr int32_t SHERIFF_STAFF = 33200000;

    constexpr int32_t SPEFFECT_ROUND_CLEANUP_REQUEST = 500050;
    constexpr int32_t SPEFFECT_TALISMAN_BREAK_REQUEST = 500051;
    constexpr int32_t SPEFFECT_TALISMAN_SHATTER_REQUEST = 500054;

    // veil talisman

    constexpr uint32_t CONCEALING_VEIL =
        0x20000000u | 6010u;

    constexpr int32_t SPEFFECT_EQUIP_CONCEALING = 500022;

    constexpr int32_t SPEFFECT_CONCEALING_VEIL = 360100;
    constexpr int32_t SPEFFECT_TALISMAN_SHATTER = 360101;
    constexpr int32_t SPEFFECT_STEALTH = 8001; // unused rn

    // Bubbletear talisman

    constexpr uint32_t PEARLDRAKE_TALISMAN =
        0x20000000u | 4050u;

    constexpr int32_t SPEFFECT_EQUIP_PEARLDRAKE = 500023;

    constexpr int32_t SPEFFECT_PEARLDRAKE = 340500;
    constexpr int32_t SPEFFECT_PEARLDRAKE_BREAK_REQUEST = 340501; // signal

    constexpr float PEARLDRAKE_MAJOR_HIT_RATIO = 0.25f;
    constexpr float PEARLDRAKE_DAMAGE_MULTIPLIER = 0.10f;

    // Crucible Feather talisman

    constexpr uint32_t CRUCIBLE_FEATHER_TALISMAN =
        0x20000000u | 4070u;

    constexpr int32_t SPEFFECT_EQUIP_CRUCIBLE_FEATHER = 500024;

    constexpr int32_t SPEFFECT_CRUCIBLE_FEATHER = 340700;
    constexpr int32_t SPEFFECT_CRUCIBLE_FEATHER_CYCLE = 340701;

    // Alloyed Fist talisman

    constexpr uint32_t ALLOYED_FIST =
        0x20000000u | 1250u;

    constexpr int32_t SPEFFECT_EQUIP_ALLOYED_FIST = 500025;

    constexpr int32_t SPEFFECT_ALLOYED_FIST = 312510;

    constexpr int32_t SPEFFECT_ALLOYED_FIST_HIT = 312511;
    constexpr int32_t SPEFFECT_ALLOYED_FIST_ARMED = 312512;
    constexpr int32_t SPEFFECT_ALLOYED_FIST_CONSUME = 312513;

    // kill talisman

    constexpr uint32_t MARK_OF_GREED_ITEM =
        0x20000000u | 6060u;

    constexpr int32_t SPEFFECT_MARK_OF_GREED = 500020;
    constexpr int32_t SPEFFECT_MARK_OF_GREED_DEATH = 500021;
    constexpr int32_t SPEFFECT_SHOW_GREED_POPUP_REQUEST = 500053;

    // Player inventory
    constexpr uintptr_t INVENTORY_PTR_OFFSET = 0x5D0;

    // Structure used by equipGear
    constexpr uintptr_t EQUIP_GAME_DATA_OFFSET = 0x2B0;

    // Structure containing tailDataIdx used by equipGear
    constexpr uintptr_t EQUIP_INVENTORY_DATA_OFFSET = 0x408;

    // blank talisman

    constexpr uint32_t NULL_MM_TALISMAN =
        0x20000000u | 6700u;

    enum class NullTalismanState
    {
        Idle,
        WaitingForEquip,
        Equipped
    };

    NullTalismanState nullTalismanState =
        NullTalismanState::Idle;

    ULONGLONG nullTalismanEquipTime = 0;

    constexpr ULONGLONG NULL_TALISMAN_TEST_DURATION_MS = 100;

    // --------------------------------------------------------
    // Goods equipment slots
    // TGA EquipItem slot numbering
    // --------------------------------------------------------

    constexpr int SLOT_QUICK_ITEM_1 = 22;
    constexpr int SLOT_GREAT_RUNE = 38;

    // --------------------------------------------------------
    // Goods inventory IDs
    //
    // Goods use:
    // 0x40000000 | EquipParamGoods ID
    // --------------------------------------------------------

    constexpr uint32_t MIMIC_VEIL =
        0x40000000u | 3040u;

    constexpr uint32_t GODRICK_GREAT_RUNE =
        0x40000000u | 191u;

    constexpr uint32_t MORGOTT_GREAT_RUNE =
        0x40000000u | 193u;

    constexpr uint32_t MOHG_GREAT_RUNE =
        0x40000000u | 195u;

    // --------------------------------------------------------
    // Sheriff magic
    // --------------------------------------------------------

    constexpr int32_t SWIFT_GLINTSTONE_SHARD = 4010;
    constexpr int32_t CRYSTAL_BURST = 4090;

    // --------------------------------------------------------
    // Sheriff magic
    // --------------------------------------------------------

    constexpr uintptr_t MAGIC_DATA_PTR_OFFSET = 0x530;
    constexpr uintptr_t QUICK_ITEM_SELECTED_OFFSET = 0x5D8;

    constexpr int32_t SPEFFECT_MURDERER = 500000;
    constexpr int32_t SPEFFECT_SHERIFF = 500001;
    constexpr int32_t SPEFFECT_INNOCENT = 500002;
    constexpr int32_t SPEFFECT_BOOTSTRAP = 500003;
    constexpr int32_t SPEFFECT_ROUND_START_LOCK = 500005;

    // --------------------------------------------------------
    // Sheriff punishment logic
    // --------------------------------------------------------

    int sheriffWrongKills = 0;

    constexpr int MAX_MM_PLAYERS = 16;

    bool sheriffTrackedAlive[MAX_MM_PLAYERS]{};
    bool sheriffTrackedInitialized[MAX_MM_PLAYERS]{};
    bool sheriffTargetWasInnocent[MAX_MM_PLAYERS]{};
    bool sheriffTargetHitBySheriff[MAX_MM_PLAYERS]{};

    // --------------------------------------------------------

    constexpr int32_t
        SPEFFECT_SHERIFF_PICKUP_REQUEST =
        500062;

    constexpr int32_t
        SPEFFECT_SHERIFF_ATTACK_TAGGER =
        500063;

    constexpr int32_t
        SPEFFECT_SHERIFF_HIT_MARKER =
        500060;

    // ChrAsm / active weapon selection inside PlayerGameData.
    constexpr uintptr_t ARM_STYLE_OFFSET = 0x324;
    constexpr uintptr_t CURRENT_RIGHT_WEAPON_SLOT_OFFSET = 0x32C;

    constexpr int32_t WEAPON_SLOT_PRIMARY = 0;
    constexpr int32_t WEAPON_SLOT_SECONDARY = 1;
    constexpr int32_t WEAPON_SLOT_TERTIARY = 2;

    // --------------------------------------------------------
    // Result banner requests
    // --------------------------------------------------------

    constexpr uint32_t FLAG_VICTORY_REQUEST =
        16009872;

    constexpr uint32_t FLAG_DEFEAT_REQUEST =
        16009873;

    constexpr uint32_t FLAG_STALEMATE_REQUEST =
        16009874;

    constexpr uint32_t FLAG_COMMENCE_REQUEST =
        16009875;

    // --------------------------------------------------------
    // tp to hub
    // --------------------------------------------------------

    constexpr uint32_t FLAG_RETURN_TO_HUB_REQUEST =
        16009876;

    constexpr uint32_t FLAG_VOLCANO_LOADED =
        16009877;

    // --------------------------------------------------------

    enum class Role
    {
        None,
        Murderer,
        Sheriff,
        Innocent
    };

    enum class MMCommand
    {
        SetInnocent,
        SetSheriff,
        SetMurderer
    };

    struct PlayerState
    {
        int slot = -1;
        int32_t entityId = -1;

        uintptr_t chrIns = 0;

        int hp = -1;
        int maxHp = -1;

        bool valid = false;
        bool alive = false;
    };

    struct EventFlagRef
    {
        uint8_t* address = nullptr;
        uint8_t bit = 0;
        bool valid = false;
    };

    // --------------------------------------------------------
    // Forward declarations
    // --------------------------------------------------------

    bool UnequipGearSlot(int32_t slot);

    bool GetEquippedItemInSlot(
        int32_t slot,
        uint32_t& itemId);

    bool UnequipGoodsSlot(int32_t slot);

    bool UnequipGoodsItemEverywhere(
        uint32_t itemId);

    bool SetSheriffMagic();
    bool ClearRoleMagic();
    bool UnlockAllGestures();

    bool RemoveLocalTalismanInventory();

    bool RemoveAllInventoryCopies(
        int32_t itemId);

    // --------------------------------------------------------
    // Globals
    // --------------------------------------------------------

    uintptr_t g_gameDataManAddress = 0;
    uintptr_t g_equipGearAddress = 0;
    uintptr_t g_equipGoodsAddress = 0;
    uintptr_t g_worldChrManAddress = 0;
    uintptr_t g_mapItemManAddress = 0;
    uintptr_t g_itemGiveAddress = 0;
    uintptr_t g_applySpEffectAddress = 0;
    uintptr_t g_removeSpEffectAddress = 0;
    uintptr_t g_entityToChrInsAddress = 0;
    uintptr_t g_eventFlagManAddress = 0;
    uintptr_t g_itemPopupAddress = 0;
    uintptr_t g_removeItemAddress = 0;

    HMODULE g_module = nullptr;
    bool g_running = true;

    bool concealingTracking = false;
    bool mmTalismanWasEquipped = false;
    bool concealingBreakInProgress = false;

    // StartGame no longer applies the role SpEffect right away - it now waits
    // for the m11_10_00_00 warp event to actually consume the spawn flag
    // (that's the only reliable "the player has left the lobby" signal we
    // have, since PlayerInMap is an EMEVD-only condition with no C++-side
    // equivalent) before granting the role at the Volcano Manor end.
    std::string g_activeRoundId;
    bool g_waitingForVolcanoLoad = false;
    bool g_volcanoLoadReported = false;
    bool g_roundActive = false;
    bool g_playerDeathReported = false;

    // --------------------------------------------------------
    // Native randomizer-style auto-equip
    // --------------------------------------------------------

    struct EquipItemStruct
    {
        uint8_t unused_01[8]{};

        // +0x08
        uint32_t equipment_slot = 0xFFFFFFFF;

        uint8_t unused_02[76]{};

        // +0x58
        uint32_t inventory_slot = 0xFFFFFFFF;
    };

    using EquipItemNativeFn =
        void(*)(EquipItemStruct*);

    using GetInventoryIdNativeFn =
        int(*)(uintptr_t equipInventoryData, int32_t* itemId);

    EquipItemNativeFn g_equipItemNative = nullptr;
    GetInventoryIdNativeFn g_getInventoryIdNative = nullptr;

    // --------------------------------------------------------
    // Logging
    // --------------------------------------------------------

    void Log(const std::string& text)
    {
        char dllPath[MAX_PATH]{};
        GetModuleFileNameA(g_module, dllPath, MAX_PATH);

        std::string path = dllPath;

        const auto slash = path.find_last_of("\\/");
        if (slash != std::string::npos)
            path.resize(slash + 1);

        path += "MurderMystery.log";

        std::ofstream file(path, std::ios::app);

        if (file)
            file << text << "\n";
    }

    std::string Hex(uintptr_t value)
    {
        std::ostringstream ss;
        ss << "0x"
            << std::uppercase
            << std::hex
            << value;

        return ss.str();
    }

    // --------------------------------------------------------
    // Pattern scanning
    // --------------------------------------------------------

    std::vector<int> PatternToBytes(const char* pattern)
    {
        std::vector<int> bytes;

        const char* current = pattern;

        while (*current)
        {
            if (*current == ' ')
            {
                ++current;
                continue;
            }

            if (*current == '?')
            {
                ++current;

                if (*current == '?')
                    ++current;

                bytes.push_back(-1);
                continue;
            }

            char* end = nullptr;

            const int value =
                static_cast<int>(strtoul(current, &end, 16));

            bytes.push_back(value);
            current = end;
        }

        return bytes;
    }

    uintptr_t FindPattern(
        uintptr_t moduleBase,
        size_t moduleSize,
        const char* pattern)
    {
        const auto patternBytes = PatternToBytes(pattern);

        const size_t patternSize = patternBytes.size();

        auto* scanBytes =
            reinterpret_cast<uint8_t*>(moduleBase);

        for (size_t i = 0;
            i <= moduleSize - patternSize;
            ++i)
        {
            bool found = true;

            for (size_t j = 0; j < patternSize; ++j)
            {
                if (patternBytes[j] != -1 &&
                    scanBytes[i + j] !=
                    static_cast<uint8_t>(patternBytes[j]))
                {
                    found = false;
                    break;
                }
            }

            if (found)
                return moduleBase + i;
        }

        return 0;
    }

    bool GetModuleInfo(
        uintptr_t& base,
        size_t& size)
    {
        HMODULE exe =
            GetModuleHandleW(L"eldenring.exe");

        if (!exe)
            return false;

        base = reinterpret_cast<uintptr_t>(exe);

        auto* dos =
            reinterpret_cast<IMAGE_DOS_HEADER*>(base);

        if (dos->e_magic != IMAGE_DOS_SIGNATURE)
            return false;

        auto* nt =
            reinterpret_cast<IMAGE_NT_HEADERS*>(
                base + dos->e_lfanew);

        if (nt->Signature != IMAGE_NT_SIGNATURE)
            return false;

        size = nt->OptionalHeader.SizeOfImage;

        return true;
    }

    // --------------------------------------------------------
    // Resolve GameDataMan
    //
    // TGA AOB:
    //
    // 48 8B 05 ?? ?? ?? ??
    // 48 85 C0
    // 74 05
    // 48 8B 40 58
    // C3 C3
    //
    // First instruction:
    //
    // mov rax,[RIP+XXXXXXXX]
    //
    // The RIP target is the ADDRESS containing
    // the GameDataMan singleton pointer.
    // --------------------------------------------------------

    bool ResolveGameDataMan()
    {
        uintptr_t base = 0;
        size_t size = 0;

        if (!GetModuleInfo(base, size))
        {
            Log("[ERROR] Could not resolve eldenring.exe.");
            return false;
        }

        constexpr const char* GAME_DATA_MAN_PATTERN =
            "48 8B 05 ?? ?? ?? ?? "
            "48 85 C0 "
            "74 05 "
            "48 8B 40 58 "
            "C3 C3";

        const uintptr_t instruction =
            FindPattern(
                base,
                size,
                GAME_DATA_MAN_PATTERN);

        if (!instruction)
        {
            Log("[ERROR] GameDataMan AOB not found.");
            return false;
        }

        const int32_t relative =
            *reinterpret_cast<int32_t*>(
                instruction + 3);

        g_gameDataManAddress =
            instruction + 7 + relative;

        Log(
            "[OK] GameDataMan global = " +
            Hex(g_gameDataManAddress));

        return true;
    }

    bool ResolveWorldChrMan()
    {
        uintptr_t base = 0;
        size_t size = 0;

        if (!GetModuleInfo(base, size))
        {
            Log("[ERROR] Could not resolve eldenring.exe for WorldChrMan.");
            return false;
        }

        constexpr const char* WORLD_CHR_MAN_PATTERN =
            "48 8B 05 ?? ?? ?? ?? "
            "48 85 C0 "
            "74 0F "
            "48 39 88";

        const uintptr_t instruction =
            FindPattern(
                base,
                size,
                WORLD_CHR_MAN_PATTERN);

        if (!instruction)
        {
            Log("[ERROR] WorldChrMan AOB not found.");
            return false;
        }

        const int32_t relative =
            *reinterpret_cast<int32_t*>(
                instruction + 3);

        g_worldChrManAddress =
            instruction + 7 + relative;

        Log(
            "[OK] WorldChrMan global = " +
            Hex(g_worldChrManAddress));

        return true;
    }

    bool ResolveMapItemMan()
    {
        uintptr_t base = 0;
        size_t size = 0;

        if (!GetModuleInfo(base, size))
        {
            Log("[ERROR] Could not resolve eldenring.exe for MapItemMan.");
            return false;
        }

        constexpr const char* PATTERN =
            "48 8B 0D ?? ?? ?? ?? "
            "C7 44 24 50 FF FF FF FF";

        const uintptr_t instruction =
            FindPattern(
                base,
                size,
                PATTERN);

        if (!instruction)
        {
            Log("[ERROR] MapItemMan AOB not found.");
            return false;
        }

        const int32_t relative =
            *reinterpret_cast<int32_t*>(
                instruction + 3);

        g_mapItemManAddress =
            instruction + 7 + relative;

        Log(
            "[OK] MapItemMan global = " +
            Hex(g_mapItemManAddress));

        return true;
    }

    bool ResolveItemGive()
    {
        uintptr_t base = 0;
        size_t size = 0;

        if (!GetModuleInfo(base, size))
        {
            Log("[ERROR] Could not resolve eldenring.exe for ItemGive.");
            return false;
        }

        constexpr const char* PATTERN =
            "8B 02 83 F8 0A";

        const uintptr_t match =
            FindPattern(
                base,
                size,
                PATTERN);

        if (!match)
        {
            Log("[ERROR] ItemGive AOB not found.");
            return false;
        }

        g_itemGiveAddress =
            match - 0x52;

        Log(
            "[OK] ItemGive = " +
            Hex(g_itemGiveAddress));

        return true;
    }

    bool ResolveItemPopup()
    {
        uintptr_t base = 0;
        size_t size = 0;

        if (!GetModuleInfo(base, size))
            return false;

        constexpr const char* PATTERN =
            "?? 8B FA ?? 8B D9 ?? 8B 81 A8 00 00 00";

        const uintptr_t match =
            FindPattern(
                base,
                size,
                PATTERN);

        if (!match)
        {
            Log("[ERROR] ItemPopup AOB not found.");
            return false;
        }

        g_itemPopupAddress =
            match - 0x14;

        Log(
            "[OK] ItemPopup = " +
            Hex(g_itemPopupAddress));

        return true;
    }

    bool ResolveEquipGear()
    {
        uintptr_t base = 0;
        size_t size = 0;

        if (!GetModuleInfo(base, size))
        {
            Log("[ERROR] ResolveEquipGear: eldenring.exe unavailable.");
            return false;
        }

        constexpr const char* PATTERN =
            "?? 8B F1 ?? 8B D8 ?? 63 EA ?? 8B F9";

        const uintptr_t match =
            FindPattern(base, size, PATTERN);

        if (!match)
        {
            Log("[ERROR] equipGear AOB not found.");
            return false;
        }

        g_equipGearAddress = match - 0x17;

        Log(
            "[OK] equipGear = " +
            Hex(g_equipGearAddress));

        return true;
    }

    bool ResolveEquipGoods()
    {
        uintptr_t base = 0;
        size_t size = 0;

        if (!GetModuleInfo(base, size))
        {
            Log("[ERROR] ResolveEquipGoods: eldenring.exe unavailable.");
            return false;
        }

        // Exact TGA equipGoods pattern.
        constexpr const char* PATTERN =
            "?? FA ?? ?? 0F ?? 81 C1 ?? ?? ?? ?? ?? 8B C1 E9";

        const uintptr_t match =
            FindPattern(base, size, PATTERN);

        if (!match)
        {
            Log("[ERROR] equipGoods AOB not found.");
            return false;
        }

        g_equipGoodsAddress = match;

        Log(
            "[OK] equipGoods = " +
            Hex(g_equipGoodsAddress));

        return true;
    }

    bool ResolveNativeEquipItem()
    {
        uintptr_t base = 0;
        size_t size = 0;

        if (!GetModuleInfo(base, size))
        {
            Log("[ERROR] ResolveNativeEquipItem: eldenring.exe unavailable.");
            return false;
        }

        constexpr const char* PATTERN =
            "57 48 83 EC 20 "
            "48 8B F9 "
            "48 83 C1 10 "
            "E8 ?? ?? ?? ?? "
            "8B 4F 08 "
            "48 8D 57 10 "
            "E8 ?? ?? ?? ?? "
            "E8 ?? ?? ?? ?? "
            "44 8B 47 58";

        const uintptr_t match =
            FindPattern(base, size, PATTERN);

        if (!match)
        {
            Log("[ERROR] Native EquipItem AOB not found.");
            return false;
        }

        g_equipItemNative =
            reinterpret_cast<EquipItemNativeFn>(match);

        Log(
            "[OK] Native EquipItem = " +
            Hex(match));

        return true;
    }

    bool ResolveNativeGetInventoryId()
    {
        uintptr_t base = 0;
        size_t size = 0;

        if (!GetModuleInfo(base, size))
        {
            Log("[ERROR] ResolveNativeGetInventoryId: eldenring.exe unavailable.");
            return false;
        }

        constexpr const char* PATTERN =
            "48 8D 8F 58 01 00 00 "
            "E8 ?? ?? ?? ?? "
            "8B D8 "
            "85 C0 "
            "78 6A";

        const uintptr_t match =
            FindPattern(base, size, PATTERN);

        if (!match)
        {
            Log("[ERROR] Native GetInventoryId AOB not found.");
            return false;
        }

        // E8 rel32 begins at match + 7.
        const uintptr_t callInstruction =
            match + 7;

        const int32_t displacement =
            *reinterpret_cast<int32_t*>(
                callInstruction + 1);

        const uintptr_t target =
            callInstruction + 5 + displacement;

        g_getInventoryIdNative =
            reinterpret_cast<GetInventoryIdNativeFn>(
                target);

        Log(
            "[OK] Native GetInventoryId = " +
            Hex(target));

        return true;
    }

    bool ResolveApplySpEffect()
    {
        uintptr_t base = 0;
        size_t size = 0;

        if (!GetModuleInfo(base, size))
            return false;

        constexpr const char* PATTERN =
            "48 8B C4 "
            "48 89 58 08 "
            "48 89 70 10 "
            "57 "
            "48 81 EC ?? ?? ?? ?? "
            "0F 28 05 ?? ?? ?? ?? "
            "48 8B F1 "
            "0F 28 0D ?? ?? ?? ?? "
            "48 8D 48 88";

        const uintptr_t match = FindPattern(base, size, PATTERN);

        if (!match)
        {
            Log("[ERROR] ApplySpEffect AOB not found.");
            return false;
        }

        g_applySpEffectAddress = match;
        Log("[OK] ApplySpEffect = " + Hex(match));
        return true;
    }

    bool ResolveRemoveSpEffect()
    {
        uintptr_t base = 0;
        size_t size = 0;

        if (!GetModuleInfo(base, size))
            return false;

        constexpr const char* PATTERN =
            "48 83 EC 28 "
            "8B C2 "
            "48 8B 51 08 "
            "48 85 D2 ?? ?? "
            "90";

        const uintptr_t match = FindPattern(base, size, PATTERN);

        if (!match)
        {
            Log("[ERROR] RemoveSpEffect AOB not found.");
            return false;
        }

        g_removeSpEffectAddress = match;
        Log("[OK] RemoveSpEffect = " + Hex(match));
        return true;
    }

    bool ResolveEntityToChrIns()
    {
        uintptr_t base = 0;
        size_t size = 0;

        if (!GetModuleInfo(base, size))
            return false;

        constexpr const char* PATTERN =
            "48 89 5C 24 08 "
            "48 89 74 24 10 "
            "48 89 7C 24 18 "
            "41 56 "
            "48 83 EC ?? "
            "48 8B 3D ?? ?? ?? ?? "
            "33 DB "
            "49 8B F0 "
            "4C 8B F1 "
            "48 85 FF";

        const uintptr_t match = FindPattern(base, size, PATTERN);

        if (!match)
        {
            Log("[ERROR] EntityToChrIns AOB not found.");
            return false;
        }

        g_entityToChrInsAddress = match;
        Log("[OK] EntityToChrIns = " + Hex(match));
        return true;
    }

    bool ResolveEventFlagMan()
    {
        uintptr_t base = 0;
        size_t size = 0;

        if (!GetModuleInfo(base, size))
            return false;

        constexpr const char* PATTERN =
            "48 8B 3D ?? ?? ?? ?? "
            "48 85 FF "
            "74 ?? "
            "48 8B 49";

        const uintptr_t match =
            FindPattern(base, size, PATTERN);

        if (!match)
        {
            Log("[ERROR] EventFlagMan AOB not found.");
            return false;
        }

        // 48 8B 3D xx xx xx xx
        // mov rdi, [rip+disp32]
        const int32_t displacement =
            *reinterpret_cast<int32_t*>(match + 3);

        g_eventFlagManAddress =
            match + 7 + displacement;

        Log(
            "[OK] EventFlagMan global = " +
            Hex(g_eventFlagManAddress));

        return true;
    }

    bool ResolveRemoveItem()
    {
        uintptr_t base = 0;
        size_t size = 0;

        if (!GetModuleInfo(base, size))
            return false;

        constexpr const char* PATTERN =
            "?? 83 EC ?? 8B F2 ?? 8B E9 ?? 85 C0 74";

        const uintptr_t match =
            FindPattern(
                base,
                size,
                PATTERN);

        if (!match)
        {
            Log("[ERROR] RemoveItem AOB not found.");
            return false;
        }

        g_removeItemAddress =
            match - 0x10;

        Log(
            "[OK] RemoveItem = " +
            Hex(g_removeItemAddress));

        return true;
    }

    // --------------------------------------------------------
    // Resolve player equipment structure
    //
    // CE representation:
    //
    // Address: GameDataMan
    // offsets:
    //   08
    //   equipment field
    //
    // Equivalent:
    //
    // gameDataMan = *GameDataManGlobal
    // equipment   = *(gameDataMan + 0x08)
    // --------------------------------------------------------

    uintptr_t GetPlayerGameData()
    {
        if (!g_gameDataManAddress)
            return 0;

        __try
        {
            const uintptr_t gameDataMan =
                *reinterpret_cast<uintptr_t*>(
                    g_gameDataManAddress);

            if (!gameDataMan)
                return 0;

            return *reinterpret_cast<uintptr_t*>(
                gameDataMan + 0x08);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return 0;
        }
    }

    uintptr_t GetLocalPlayerIns()
    {
        if (!g_worldChrManAddress)
            return 0;

        __try
        {
            const uintptr_t worldChrMan =
                *reinterpret_cast<uintptr_t*>(
                    g_worldChrManAddress);

            if (!worldChrMan)
                return 0;

            const uintptr_t playerArray =
                *reinterpret_cast<uintptr_t*>(
                    worldChrMan + 0x10EF8);

            if (!playerArray)
                return 0;

            return *reinterpret_cast<uintptr_t*>(
                playerArray);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return 0;
        }
    }

    bool TryReadPlayerSlot(
        int playerSlot,
        PlayerState& out)
    {
        out = PlayerState{};
        out.slot = playerSlot;

        if (!g_worldChrManAddress)
            return false;

        __try
        {
            const uintptr_t worldChrMan =
                *reinterpret_cast<uintptr_t*>(
                    g_worldChrManAddress);

            if (!worldChrMan)
                return false;

            const uintptr_t playerArray =
                *reinterpret_cast<uintptr_t*>(
                    worldChrMan + 0x10EF8);

            if (!playerArray)
                return false;

            const uintptr_t slotAddress =
                playerArray +
                static_cast<uintptr_t>(playerSlot) * 0x10;

            // This is the actual ChrIns/playerIns pointer.
            const uintptr_t chrIns =
                *reinterpret_cast<uintptr_t*>(
                    slotAddress);

            if (!chrIns)
                return false;

            out.chrIns = chrIns;

            // Entity ID belongs to ChrIns.
            out.entityId =
                *reinterpret_cast<int32_t*>(
                    chrIns + 0x1E8);

            // Character data used for HP.
            const uintptr_t chrData =
                *reinterpret_cast<uintptr_t*>(
                    chrIns + 0x190);

            if (!chrData)
                return false;

            const uintptr_t hpBase =
                *reinterpret_cast<uintptr_t*>(
                    chrData);

            if (!hpBase)
                return false;

            out.hp =
                *reinterpret_cast<int*>(
                    hpBase + 0x138);

            out.maxHp =
                *reinterpret_cast<int*>(
                    hpBase + 0x13C);

            out.valid = true;
            out.alive = out.hp > 0;

            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            out.valid = false;
            return false;
        }
    }

    bool WritePlayerHp(
        uintptr_t chrIns,
        int newHp)
    {
        if (!chrIns)
            return false;

        __try
        {
            const uintptr_t chrData =
                *reinterpret_cast<uintptr_t*>(
                    chrIns + 0x190);

            if (!chrData)
                return false;

            const uintptr_t hpBase =
                *reinterpret_cast<uintptr_t*>(
                    chrData);

            if (!hpBase)
                return false;

            *reinterpret_cast<int*>(
                hpBase + 0x138) =
                newHp;

            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    void DumpPlayerStates()
    {
        Log("");
        Log("========== PLAYER STATE ==========");

        for (int slot = 0; slot < MAX_MM_PLAYERS; ++slot)
        {
            PlayerState player{};

            if (!TryReadPlayerSlot(slot, player))
            {
                Log(
                    "Slot " +
                    std::to_string(slot) +
                    ": EMPTY");

                continue;
            }

            Log(
                "Slot " +
                std::to_string(slot) +
                " Entity=" +
                std::to_string(player.entityId) +
                " ChrIns=" +
                Hex(player.chrIns));

            Log(
                "  HP=" +
                std::to_string(player.hp) +
                "/" +
                std::to_string(player.maxHp));

            Log(
                std::string("  Alive=") +
                (player.alive ? "YES" : "NO"));
        }

        Log("==================================");
        Log("");
    }

    bool HasSpEffect(int32_t wantedId)
    {
        const uintptr_t playerIns =
            GetLocalPlayerIns();

        if (!playerIns)
            return false;

        __try
        {
            const uintptr_t specialEffect =
                *reinterpret_cast<uintptr_t*>(
                    playerIns + 0x178);

            if (!specialEffect)
                return false;

            uintptr_t effect =
                *reinterpret_cast<uintptr_t*>(
                    specialEffect + 0x08);

            // Safety limit in case a bad pointer creates a loop.
            for (int i = 0; effect && i < 2048; ++i)
            {
                const int32_t id =
                    *reinterpret_cast<int32_t*>(
                        effect + 0x08);

                if (id == wantedId)
                    return true;

                const int32_t hasNext =
                    *reinterpret_cast<int32_t*>(
                        effect + 0x30);

                if (hasNext == 0)
                    break;

                effect =
                    *reinterpret_cast<uintptr_t*>(
                        effect + 0x30);
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }

        return false;
    }

    bool ChrInsHasSpEffect(
        uintptr_t chrIns,
        int32_t wantedId)
    {
        if (!chrIns)
            return false;

        __try
        {
            const uintptr_t specialEffect =
                *reinterpret_cast<uintptr_t*>(
                    chrIns + 0x178);

            if (!specialEffect)
                return false;

            uintptr_t effect =
                *reinterpret_cast<uintptr_t*>(
                    specialEffect + 0x08);

            for (int i = 0;
                effect && i < 2048;
                ++i)
            {
                const int32_t id =
                    *reinterpret_cast<int32_t*>(
                        effect + 0x08);

                if (id == wantedId)
                    return true;

                const int32_t hasNext =
                    *reinterpret_cast<int32_t*>(
                        effect + 0x30);

                if (hasNext == 0)
                    break;

                effect =
                    *reinterpret_cast<uintptr_t*>(
                        effect + 0x30);
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }

        return false;
    }

    using ApplySpEffectFn =
        void(*)(uintptr_t chrIns, int32_t spEffectId);

    using RemoveSpEffectFn =
        void(*)(uintptr_t specialEffect, int32_t spEffectId);

    using EntityToChrInsFn =
        uintptr_t(*)(int32_t* entityId);

    uintptr_t GetChrInsByEntityId(int32_t entityId)
    {
        if (!g_entityToChrInsAddress)
            return 0;

        __try
        {
            auto fn =
                reinterpret_cast<EntityToChrInsFn>(
                    g_entityToChrInsAddress);

            int32_t id = entityId;
            return fn(&id);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return 0;
        }
    }

    bool ApplySpEffectToChrIns(
        uintptr_t chrIns,
        int32_t spEffectId)
    {
        if (!chrIns || !g_applySpEffectAddress)
            return false;

        auto fn =
            reinterpret_cast<ApplySpEffectFn>(
                g_applySpEffectAddress);

        fn(chrIns, spEffectId);

        Log(
            "[ACTION] Applied SpEffect " +
            std::to_string(spEffectId));

        return true;
    }

    bool RemoveSpEffectFromChrIns(
        uintptr_t chrIns,
        int32_t spEffectId)
    {
        if (!chrIns || !g_removeSpEffectAddress)
            return false;

        const uintptr_t specialEffect =
            *reinterpret_cast<uintptr_t*>(
                chrIns + 0x178);

        if (!specialEffect)
            return false;

        auto fn =
            reinterpret_cast<RemoveSpEffectFn>(
                g_removeSpEffectAddress);

        fn(specialEffect, spEffectId);

        Log(
            "[ACTION] Removed SpEffect " +
            std::to_string(spEffectId));

        return true;
    }

    int32_t GetItemIndex(
        int32_t requestedItemId,
        bool keyItem = false)
    {
        const uintptr_t player =
            GetPlayerGameData();

        if (!player)
            return -1;

        const uintptr_t inventory =
            *reinterpret_cast<uintptr_t*>(
                player + INVENTORY_PTR_OFFSET);

        if (!inventory)
            return -1;

        const uintptr_t keyOffset =
            keyItem ? 0x10 : 0x00;

        const uintptr_t inventoryList =
            *reinterpret_cast<uintptr_t*>(
                inventory + 0x10 + keyOffset);

        const int32_t inventoryNum =
            *reinterpret_cast<int32_t*>(
                inventory + 0x18 + keyOffset);

        if (!inventoryList)
            return -1;

        const int32_t maxSlots =
            keyItem ? 384 : 2688;

        int32_t itemCount = 0;

        for (int32_t i = 0; i <= maxSlots; ++i)
        {
            const uintptr_t entry =
                inventoryList +
                static_cast<uintptr_t>(i) * 0x18;

            const int32_t itemId =
                *reinterpret_cast<int32_t*>(
                    entry + 0x04);

            if (itemId == requestedItemId)
            {
                /*Log(
                    "[OK] Found item " +
                    std::to_string(requestedItemId) +
                    " at inventory index " +
                    std::to_string(i) +
                    (keyItem ? " [KEY]" : " [NORMAL]"));*/

                return i;
            }

            if (
                static_cast<uint32_t>(itemId) !=
                0xFFFFFFFF)
            {
                ++itemCount;
            }

            if (itemCount >= inventoryNum)
                break;
        }

        return -1;
    }

    uint32_t GetItemDataByIndex(
        int32_t index,
        bool keyItem = false)
    {
        if (index < 0)
            return 0xFFFFFFFF;

        const uintptr_t player =
            GetPlayerGameData();

        if (!player)
            return 0xFFFFFFFF;

        const uintptr_t inventory =
            *reinterpret_cast<uintptr_t*>(
                player + INVENTORY_PTR_OFFSET);

        if (!inventory)
            return 0xFFFFFFFF;

        const uintptr_t keyOffset =
            keyItem ? 0x10 : 0x00;

        const uintptr_t inventoryList =
            *reinterpret_cast<uintptr_t*>(
                inventory + 0x10 + keyOffset);

        if (!inventoryList)
            return 0xFFFFFFFF;

        return *reinterpret_cast<uint32_t*>(
            inventoryList +
            static_cast<uintptr_t>(index) * 0x18);
    }

    Role GetCurrentRole()
    {
        if (HasSpEffect(SPEFFECT_MURDERER))
            return Role::Murderer;

        if (HasSpEffect(SPEFFECT_SHERIFF))
            return Role::Sheriff;

        if (HasSpEffect(SPEFFECT_INNOCENT))
            return Role::Innocent;

        return Role::None;
    }

    bool SetSelectedQuickItem(int32_t index)
    {
        const uintptr_t player =
            GetPlayerGameData();

        if (!player)
            return false;

        *reinterpret_cast<int32_t*>(
            player + QUICK_ITEM_SELECTED_OFFSET) =
            index;

        Log(
            "[ACTION] Selected quick item index = " +
            std::to_string(index));

        return true;
    }

    bool TryResolveEventFlagRaw(
        uint32_t id,
        EventFlagRef& out)
    {
        out = {};

        if (!g_eventFlagManAddress)
            return false;

        __try
        {
            // Equivalent to:
            // readPointer("CSFD4VirtualMemoryFlag")
            const uintptr_t virtualMemoryFlag =
                *reinterpret_cast<uintptr_t*>(
                    g_eventFlagManAddress);

            if (!virtualMemoryFlag)
                return false;

            const int32_t mod =
                *reinterpret_cast<int32_t*>(
                    virtualMemoryFlag + 0x1C);

            if (mod <= 0)
                return false;

            const uint32_t block =
                id / static_cast<uint32_t>(mod);

            const uint32_t index =
                id - block * static_cast<uint32_t>(mod);

            const uintptr_t tree =
                *reinterpret_cast<uintptr_t*>(
                    virtualMemoryFlag + 0x38);

            if (!tree)
                return false;

            // Lua:
            // local node = readPointer(tree + 0x8)
            uintptr_t node =
                *reinterpret_cast<uintptr_t*>(
                    tree + 0x8);

            if (!node)
                return false;

            // while (!_Trynode->_Isnil)
            while (
                *reinterpret_cast<uint8_t*>(
                    node + 0x19) == 0)
            {
                const uint32_t nodeBlock =
                    *reinterpret_cast<uint32_t*>(
                        node + 0x20);

                if (nodeBlock < block)
                {
                    // right
                    node =
                        *reinterpret_cast<uintptr_t*>(
                            node + 0x10);
                }
                else if (nodeBlock == block)
                {
                    break;
                }
                else
                {
                    // left
                    node =
                        *reinterpret_cast<uintptr_t*>(
                            node);
                }

                if (!node)
                    return false;
            }

            if (
                *reinterpret_cast<uint32_t*>(
                    node + 0x20) != block)
            {
                return false;
            }

            const uintptr_t blockInfo =
                node + 0x28;

            const int32_t addressMode =
                *reinterpret_cast<int32_t*>(
                    blockInfo);

            uintptr_t blockAddress =
                *reinterpret_cast<uintptr_t*>(
                    blockInfo + 0x8);

            if (addressMode == 1)
            {
                const int32_t blockSize =
                    *reinterpret_cast<int32_t*>(
                        virtualMemoryFlag + 0x20);

                const uintptr_t blocks =
                    *reinterpret_cast<uintptr_t*>(
                        virtualMemoryFlag + 0x28);

                if (!blocks || blockSize <= 0)
                    return false;

                const uint32_t blockIndex =
                    static_cast<uint32_t>(
                        blockAddress & 0xFFFFFFFFull);

                blockAddress =
                    blocks +
                    static_cast<uintptr_t>(blockIndex) *
                    static_cast<uintptr_t>(blockSize);
            }
            else if (
                addressMode != 2 ||
                blockAddress == 0)
            {
                return false;
            }

            const uint32_t byteIndex =
                index / 8;

            uint32_t bitIndex =
                index - byteIndex * 8;

            // CE code:
            // bitIndex = 7 - bitIndex
            bitIndex = 7 - bitIndex;

            out.address =
                reinterpret_cast<uint8_t*>(
                    blockAddress + byteIndex);

            out.bit =
                static_cast<uint8_t>(
                    1u << bitIndex);

            out.valid = true;

            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            out = {};
            return false;
        }
    }

    bool GetEventFlag(
        uint32_t id,
        bool& value)
    {
        EventFlagRef flag{};

        if (!TryResolveEventFlagRaw(id, flag))
            return false;

        if (!flag.valid ||
            flag.address == nullptr ||
            flag.bit == 0)
        {
            return false;
        }

        __try
        {
            const uint8_t current =
                *flag.address;

            value =
                (current & flag.bit) != 0;

            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    bool SetEventFlag(
        uint32_t id,
        bool value)
    {
        EventFlagRef flag{};

        if (!TryResolveEventFlagRaw(id, flag))
            return false;

        if (!flag.valid ||
            flag.address == nullptr ||
            flag.bit == 0)
        {
            return false;
        }

        __try
        {
            uint8_t current =
                *flag.address;

            if (value)
            {
                current |= flag.bit;
            }
            else
            {
                current &=
                    static_cast<uint8_t>(
                        ~flag.bit);
            }

            *flag.address = current;

            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    bool RequestSpawn(int spawnIndex)
    {
        switch (spawnIndex)
        {
        case 1:  return SetEventFlag(11109912, true);
        case 2:  return SetEventFlag(11109913, true);
        case 3:  return SetEventFlag(11109914, true);
        case 4:  return SetEventFlag(11109915, true);
        case 5:  return SetEventFlag(11109916, true);
        case 6:  return SetEventFlag(11109917, true);
        case 7:  return SetEventFlag(11109918, true);
        case 8:  return SetEventFlag(11109919, true);
        case 9:  return SetEventFlag(11109920, true);
        case 10: return SetEventFlag(11109921, true);
        case 11: return SetEventFlag(11109922, true);

        default:
            return false;
        }
    }

    bool RequestRandomSpawn(uint32_t* outSpawnFlagId = nullptr)
    {
        const int spawnIndex =
            1 + (rand() % 11);

        Log(
            "[SPAWN TEST] Random spawn selected: " +
            std::to_string(spawnIndex));

        // Mirrors the case table in RequestSpawn() - flag 11109912 is index 1,
        // ..., flag 11109922 is index 11.
        if (outSpawnFlagId)
        {
            *outSpawnFlagId =
                11109911u + static_cast<uint32_t>(spawnIndex);
        }

        return RequestSpawn(spawnIndex);
    }

    /*bool RequestTalismanSpawnTest()
    {
        Log("[ITEM TEST] Requesting talisman spawn #1.");

        return SetEventFlag(
            16009961,
            true);
    }*/

    bool IsMMTalismanEquipped()
    {
        for (int32_t slot = 17;
            slot <= 21;
            ++slot)
        {
            uint32_t equipped = 0xFFFFFFFF;

            if (!GetEquippedItemInSlot(
                slot,
                equipped))
            {
                continue;
            }

            if (equipped == CONCEALING_VEIL ||
                equipped == PEARLDRAKE_TALISMAN ||
                equipped == CRUCIBLE_FEATHER_TALISMAN ||
                equipped == ALLOYED_FIST)
            {
                return true;
            }
        }

        return false;
    }

    bool UnequipMMTalismanEverywhere()
    {
        bool found = false;

        for (int32_t slot = 17;
            slot <= 21;
            ++slot)
        {
            uint32_t equipped = 0xFFFFFFFF;

            if (!GetEquippedItemInSlot(
                slot,
                equipped))
            {
                continue;
            }

            if (equipped == CONCEALING_VEIL ||
                equipped == PEARLDRAKE_TALISMAN ||
                equipped == CRUCIBLE_FEATHER_TALISMAN ||
                equipped == ALLOYED_FIST)
            {
                UnequipGearSlot(slot);
                found = true;
            }
        }

        return found;
    }

    bool RequestItemSpawn(int spawnIndex)
    {
        switch (spawnIndex)
        {
        case 1: return SetEventFlag(16009961, true);
        case 2: return SetEventFlag(16009962, true);
        case 3: return SetEventFlag(16009963, true);
        case 4: return SetEventFlag(16009964, true);
        case 5: return SetEventFlag(16009965, true);
        case 6: return SetEventFlag(16009966, true);
        case 7: return SetEventFlag(16009967, true);
        case 8: return SetEventFlag(16009968, true);
        case 9: return SetEventFlag(16009969, true);
        case 10: return SetEventFlag(16009971, true);

        default:
            return false;
        }
    }

    bool RequestRandomItemSpawn()
    {
        const int spawnIndex =
            1 + (rand() % 10);

        Log(
            "[ITEM TEST] Random talisman spawn selected: " +
            std::to_string(spawnIndex));

        return RequestItemSpawn(spawnIndex);
    }

    // --------------------------------------------------------
    // Weapon operations
    // --------------------------------------------------------

    using EquipGearFn = uint64_t(*)(
        uintptr_t equipGameData,
        int32_t slot,
        uintptr_t itemData,
        int32_t inventoryIndex,
        int32_t arg5,
        int32_t arg6,
        int32_t arg7
        );

    using EquipGoodsFn = uint64_t(*)(
        uintptr_t equipGameData,
        int32_t slot,
        uintptr_t itemData,
        int32_t inventoryIndex
        );

    using ItemGiveFn = uint64_t(*)(
        uintptr_t mapItemMan,
        uintptr_t items,
        uintptr_t itemGiveData,
        int32_t unknown);

    using ItemPopupFn = uint64_t(*)(
        uintptr_t mapItemManData,
        uintptr_t itemData
        );

    using RemoveItemFn = uint64_t(*)(
        uintptr_t equipInventoryData,
        int32_t inventoryIndex,
        int32_t unknown
        );

    bool CallEquipGearRaw(
        uintptr_t equipGameData,
        int32_t slot,
        uintptr_t itemData,
        int32_t inventoryIndex,
        uintptr_t equipGearAddress)
    {
        auto equipGear =
            reinterpret_cast<EquipGearFn>(
                equipGearAddress);

        equipGear(
            equipGameData,
            slot,
            itemData,
            inventoryIndex,
            1,
            1,
            0);

        return true;
    }

    bool CallEquipGoodsRaw(
        uintptr_t equipGameData,
        int32_t slot,
        uintptr_t itemData,
        int32_t inventoryIndex,
        uintptr_t equipGoodsAddress)
    {
        auto equipGoods =
            reinterpret_cast<EquipGoodsFn>(
                equipGoodsAddress);

        equipGoods(
            equipGameData,
            slot,
            itemData,
            inventoryIndex);

        return true;
    }

    bool EquipItemNativeByItemId(
        int32_t equipmentSlot,
        int32_t itemId)
    {
        if (!g_equipItemNative ||
            !g_getInventoryIdNative)
        {
            Log("[ERROR] Native auto-equip functions unavailable.");
            return false;
        }

        const uintptr_t player =
            GetPlayerGameData();

        if (!player)
        {
            Log("[ERROR] PlayerGameData unavailable for native equip.");
            return false;
        }

        // This is exactly the EquipInventoryData object used by
        // the randomizer's get_inventory_id() call.
        const uintptr_t equipInventoryData =
            player + EQUIP_INVENTORY_DATA_OFFSET;

        int32_t queryId = itemId;

        const int32_t inventorySlot =
            g_getInventoryIdNative(
                equipInventoryData,
                &queryId);

        if (inventorySlot < 0)
        {
            Log(
                "[ERROR] Native equip could not find item " +
                std::to_string(itemId));

            return false;
        }

        EquipItemStruct equip{};

        equip.equipment_slot =
            static_cast<uint32_t>(equipmentSlot);

        equip.inventory_slot =
            static_cast<uint32_t>(inventorySlot);

        g_equipItemNative(&equip);

        Log(
            "[ACTION] Native auto-equip: item=" +
            std::to_string(itemId) +
            " slot=" +
            std::to_string(equipmentSlot) +
            " inventorySlot=" +
            std::to_string(inventorySlot));

        return true;
    }

    bool EquipWeaponByItemId(
        int32_t slot,
        int32_t itemId)
    {
        if (!g_equipGearAddress)
        {
            Log("[ERROR] equipGear function unavailable.");
            return false;
        }

        const uintptr_t player =
            GetPlayerGameData();

        if (!player)
        {
            Log("[ERROR] PlayerGameData unavailable.");
            return false;
        }

        const int32_t index =
            GetItemIndex(itemId);

        if (index < 0)
        {
            Log(
                "[ERROR] Item not found in inventory: " +
                std::to_string(itemId));

            return false;
        }

        const uint32_t itemData =
            GetItemDataByIndex(index);

        if (itemData == 0xFFFFFFFF)
        {
            Log("[ERROR] Invalid inventory item data.");
            return false;
        }

        const uintptr_t equipGameData =
            player + EQUIP_GAME_DATA_OFFSET;

        const uintptr_t equipInventoryData =
            player + EQUIP_INVENTORY_DATA_OFFSET;

        const int32_t tailDataIdx =
            *reinterpret_cast<int32_t*>(
                equipInventoryData + 0x1C);

        uint32_t itemBuffer = itemData;

        const bool success =
            CallEquipGearRaw(
                equipGameData,
                slot,
                reinterpret_cast<uintptr_t>(&itemBuffer),
                index + tailDataIdx,
                g_equipGearAddress);

        if (!success)
        {
            Log("[ERROR] Exception calling equipGear.");
            return false;
        }

        Log(
            "[ACTION] Proper equip call: item=" +
            std::to_string(itemId) +
            " slot=" +
            std::to_string(slot) +
            " idx=" +
            std::to_string(index) +
            " data=" +
            Hex(itemData));

        return true;
    }

    bool EquipGearByItemId(
        int32_t slot,
        int32_t itemId)
    {
        if (!g_equipGearAddress)
        {
            Log("[ERROR] equipGear function unavailable.");
            return false;
        }

        const uintptr_t player =
            GetPlayerGameData();

        if (!player)
            return false;

        const int32_t index =
            GetItemIndex(itemId);

        if (index < 0)
            return false;

        const uint32_t itemData =
            GetItemDataByIndex(index);

        if (itemData == 0xFFFFFFFF)
            return false;

        const uintptr_t equipGameData =
            player + EQUIP_GAME_DATA_OFFSET;

        const uintptr_t equipInventoryData =
            player + EQUIP_INVENTORY_DATA_OFFSET;

        const int32_t tailDataIdx =
            *reinterpret_cast<int32_t*>(
                equipInventoryData + 0x1C);

        uint32_t itemBuffer = itemData;

        const bool success =
            CallEquipGearRaw(
                equipGameData,
                slot,
                reinterpret_cast<uintptr_t>(&itemBuffer),
                index + tailDataIdx,
                g_equipGearAddress);

        if (success)
        {
            Log(
                "[ACTION] Gear equipped: item=" +
                Hex(static_cast<uint32_t>(itemId)) +
                " slot=" +
                std::to_string(slot));
        }

        return success;
    }

    bool EquipGoodsByItemId(
        int32_t slot,
        uint32_t itemId,
        bool keyItem = false)
    {
        if (!g_equipGoodsAddress)
        {
            Log("[ERROR] equipGoods function unavailable.");
            return false;
        }

        const uintptr_t player =
            GetPlayerGameData();

        if (!player)
        {
            Log("[ERROR] PlayerGameData unavailable.");
            return false;
        }

        const int32_t index =
            GetItemIndex(
                static_cast<int32_t>(itemId),
                keyItem);

        if (index < 0)
        {
            Log(
                "[ERROR] Goods item not found in inventory: " +
                Hex(itemId));

            return false;
        }

        const uint32_t itemData =
            GetItemDataByIndex(
                index,
                keyItem);

        if (itemData == 0xFFFFFFFF)
        {
            Log("[ERROR] Invalid goods inventory data.");
            return false;
        }

        const uintptr_t equipGameData =
            player + EQUIP_GAME_DATA_OFFSET;

        const uintptr_t equipInventoryData =
            player + EQUIP_INVENTORY_DATA_OFFSET;

        const int32_t tailDataIdx =
            *reinterpret_cast<int32_t*>(
                equipInventoryData + 0x1C);

        uint32_t itemBuffer = itemData;

        const int32_t goodsSlot =
            slot - 22;

        const int32_t finalIndex =
            keyItem
            ? index
            : index + tailDataIdx;

        const bool success =
            CallEquipGoodsRaw(
                equipGameData,
                goodsSlot,
                reinterpret_cast<uintptr_t>(&itemBuffer),
                finalIndex,
                g_equipGoodsAddress);

        if (!success)
        {
            Log("[ERROR] Failed calling equipGoods.");
            return false;
        }

        Log(
            "[ACTION] Goods equipped: item=" +
            Hex(itemId) +
            " slot=" +
            std::to_string(slot));

        return true;
    }

    bool UnequipWeaponSlot(int32_t slot)
    {
        return EquipWeaponByItemId(
            slot,
            FIST_WEAPON);
    }

    bool GiveItemBatch(
        const uint32_t* itemIds,
        size_t count)
    {
        if (!g_itemGiveAddress ||
            !g_mapItemManAddress ||
            count == 0 ||
            count > 10)
        {
            return false;
        }

        const uintptr_t mapItemMan =
            *reinterpret_cast<uintptr_t*>(
                g_mapItemManAddress);

        if (!mapItemMan)
        {
            Log("[ERROR] MapItemMan instance unavailable.");
            return false;
        }

        uint8_t itemGiveData[256]{};

        uint8_t* itemTable =
            itemGiveData + 32;

        *reinterpret_cast<uint32_t*>(
            itemTable) =
            static_cast<uint32_t>(count);

        for (size_t i = 0; i < count; ++i)
        {
            uint8_t* entry =
                itemTable + i * 16;

            *reinterpret_cast<uint32_t*>(
                entry + 4) =
                itemIds[i];

            *reinterpret_cast<uint32_t*>(
                entry + 8) =
                1;

            *reinterpret_cast<uint32_t*>(
                entry + 12) =
                0xFFFFFFFF;

            *reinterpret_cast<uint32_t*>(
                entry + 16) =
                0xFFFFFFFF;
        }

        auto itemGive =
            reinterpret_cast<ItemGiveFn>(
                g_itemGiveAddress);

        itemGive(
            mapItemMan,
            reinterpret_cast<uintptr_t>(itemTable),
            reinterpret_cast<uintptr_t>(itemGiveData),
            0);

        return true;
    }

    bool UnequipGearSlot(int32_t slot)
    {
        if (!g_equipGearAddress)
            return false;

        const uintptr_t player =
            GetPlayerGameData();

        if (!player)
            return false;

        const uintptr_t equipGameData =
            player + EQUIP_GAME_DATA_OFFSET;

        uint32_t emptyItem = 0xFFFFFFFF;

        return CallEquipGearRaw(
            equipGameData,
            slot,
            reinterpret_cast<uintptr_t>(&emptyItem),
            static_cast<int32_t>(0xFFFFFFFF),
            g_equipGearAddress);
    }

    // null talisman -------------------------------------------

    bool GiveNullMMTalisman()
    {
        if (GetItemIndex(
            static_cast<int32_t>(NULL_MM_TALISMAN)) >= 0)
        {
            return true;
        }

        const uint32_t item =
            NULL_MM_TALISMAN;

        if (!GiveItemBatch(&item, 1))
        {
            Log("[ERROR] Failed to grant MM Null Talisman.");
            return false;
        }

        Log("[TALISMAN] MM Null Talisman granted.");

        return true;
    }

    bool FlushMMTalismanWithNull()
    {
        if (!GiveNullMMTalisman())
            return false;

        Sleep(20);

        uint32_t before = 0xFFFFFFFF;

        GetEquippedItemInSlot(
            SLOT_TALISMAN_1,
            before);

        Log(
            "[NULL TEST] Slot 17 BEFORE equip = " +
            Hex(before));

        if (!EquipItemNativeByItemId(
            SLOT_TALISMAN_1,
            static_cast<int32_t>(
                NULL_MM_TALISMAN)))
        {
            Log(
                "[ERROR] Failed to native-equip "
                "MM Null Talisman.");

            return false;
        }

        // IMPORTANT:
        // Native EquipItem returns before Elden Ring necessarily
        // commits the equipment change.
        //
        // Do NOT start the 1000 ms lifetime yet.
        // MainThread will wait until slot 17 actually reads 6700.
        nullTalismanState =
            NullTalismanState::WaitingForEquip;

        Log(
            "[NULL] 6700 equip requested; "
            "waiting for slot 17 confirmation.");

        return true;
    }

    // null talisman -------------------------------------------

    bool RemoveInventoryItemByIndex(
        int32_t index)
    {
        if (!g_removeItemAddress)
            return false;

        if (index < 0)
            return false;

        const uintptr_t player =
            GetPlayerGameData();

        if (!player)
            return false;

        // TGA:
        // EquipInventoryData =
        // readPointer(PlayerGameData + 0x5D0)
        const uintptr_t equipInventoryData =
            *reinterpret_cast<uintptr_t*>(
                player + INVENTORY_PTR_OFFSET);

        if (!equipInventoryData)
            return false;

        // TGA:
        // tailDataIdx =
        // readInteger(EquipInventoryData + 0x1C)
        const int32_t tailDataIdx =
            *reinterpret_cast<int32_t*>(
                equipInventoryData + 0x1C);

        auto removeItem =
            reinterpret_cast<RemoveItemFn>(
                g_removeItemAddress);

        removeItem(
            equipInventoryData,
            index + tailDataIdx,
            1);

        Log(
            "[ITEM] Removed inventory index " +
            std::to_string(index));

        return true;
    }

    bool RemoveInventoryItemById(
        int32_t itemId)
    {
        const int32_t index =
            GetItemIndex(itemId);

        if (index < 0)
            return false;

        return RemoveInventoryItemByIndex(index);
    }

    bool RemoveAllInventoryCopies(
        int32_t itemId)
    {
        bool removedAny = false;

        // Safety limit.
        for (int i = 0; i < 16; ++i)
        {
            const int32_t index =
                GetItemIndex(itemId);

            if (index < 0)
                break;

            if (!RemoveInventoryItemByIndex(index))
                break;

            removedAny = true;
        }

        return removedAny;
    }

    bool HasLocalTalisman()
    {
        return
            GetItemIndex(
                static_cast<int32_t>(CONCEALING_VEIL)) >= 0
            ||
            GetItemIndex(
                static_cast<int32_t>(PEARLDRAKE_TALISMAN)) >= 0
            ||
            GetItemIndex(
                static_cast<int32_t>(CRUCIBLE_FEATHER_TALISMAN)) >= 0
            ||
            GetItemIndex(
                static_cast<int32_t>(ALLOYED_FIST)) >= 0;
    }

    bool RemoveLocalTalismanInventory()
    {
        // Callers must ensure MM talismans are no longer
        // occupying an equipment slot before reaching here.

        const bool hadConcealing =
            GetItemIndex(
                static_cast<int32_t>(
                    CONCEALING_VEIL)) >= 0;

        const bool hadPearldrake =
            GetItemIndex(
                static_cast<int32_t>(
                    PEARLDRAKE_TALISMAN)) >= 0;

        const bool hadCrucibleFeather =
            GetItemIndex(
                static_cast<int32_t>(
                    CRUCIBLE_FEATHER_TALISMAN)) >= 0;

        const bool hadAlloyedFist =
            GetItemIndex(
                static_cast<int32_t>(
                    ALLOYED_FIST)) >= 0;

        RemoveAllInventoryCopies(
            static_cast<int32_t>(
                CONCEALING_VEIL));

        RemoveAllInventoryCopies(
            static_cast<int32_t>(
                PEARLDRAKE_TALISMAN));

        RemoveAllInventoryCopies(
            static_cast<int32_t>(
                CRUCIBLE_FEATHER_TALISMAN));

        RemoveAllInventoryCopies(
            static_cast<int32_t>(
                ALLOYED_FIST));

        if (hadConcealing ||
            hadPearldrake ||
            hadCrucibleFeather ||
            hadAlloyedFist)
        {
            Log(
                "[TALISMAN] MM talisman inventory cleared.");
        }

        return true;
    }

    bool BreakLocalTalisman()
    {
        const uintptr_t player =
            GetLocalPlayerIns();

        if (!player)
            return false;

        if (!HasLocalTalisman())
        {
            Log(
                "[TALISMAN] Break requested, "
                "but no MM talisman is owned.");

            return false;
        }

        Log("[TALISMAN] Breaking local talisman.");

        // --------------------------------------------------------
        // Network-visible VFX request.
        //
        // Direct DLL ApplySpEffectToChrIns() does not replicate
        // this short-lived effect reliably in Seamless.
        // EMEVD SetSpEffect() has been multiplayer-tested and does.
        // --------------------------------------------------------

        ApplySpEffectToChrIns(
            player,
            SPEFFECT_TALISMAN_SHATTER_REQUEST);

        // --------------------------------------------------------
        // Mechanical effects
        // --------------------------------------------------------

        RemoveSpEffectFromChrIns(
            player,
            SPEFFECT_CONCEALING_VEIL);

        RemoveSpEffectFromChrIns(
            player,
            SPEFFECT_PEARLDRAKE);

        RemoveSpEffectFromChrIns(
            player,
            SPEFFECT_CRUCIBLE_FEATHER);

        RemoveSpEffectFromChrIns(
            player,
            SPEFFECT_CRUCIBLE_FEATHER_CYCLE);

        RemoveSpEffectFromChrIns(
            player,
            SPEFFECT_EQUIP_CONCEALING);

        RemoveSpEffectFromChrIns(
            player,
            SPEFFECT_EQUIP_PEARLDRAKE);

        RemoveSpEffectFromChrIns(
            player,
            SPEFFECT_EQUIP_CRUCIBLE_FEATHER);

        RemoveSpEffectFromChrIns(
            player,
            SPEFFECT_ALLOYED_FIST);

        RemoveSpEffectFromChrIns(
            player,
            SPEFFECT_ALLOYED_FIST_ARMED);

        RemoveSpEffectFromChrIns(
            player,
            SPEFFECT_ALLOYED_FIST_CONSUME);

        RemoveSpEffectFromChrIns(
            player,
            SPEFFECT_EQUIP_ALLOYED_FIST);

        // --------------------------------------------------------
        // Equipment first.
        //
        // TGA explicitly warns against removing an item while
        // it is still equipped.
        // --------------------------------------------------------

        if (!FlushMMTalismanWithNull())
        {
            Log(
                "[ERROR] Null talisman flush failed; "
                "falling back to local cleanup.");

            UnequipMMTalismanEverywhere();
            RemoveLocalTalismanInventory();
        }

        return true;
    }

    bool RemoveLocalTalismanForRoleChange()
    {
        const uintptr_t player =
            GetLocalPlayerIns();

        if (!player)
            return false;

        Log("[TALISMAN] Role transition cleanup.");

        // Role changes should still LOOK like the talisman shattered.
        // However, this is NOT a real gameplay consumption transaction,
        // so we do not use the 6700 Null Talisman here.
        ApplySpEffectToChrIns(
            player,
            SPEFFECT_TALISMAN_SHATTER_REQUEST);

        // Kill all gameplay effects silently.
        RemoveSpEffectFromChrIns(
            player,
            SPEFFECT_CONCEALING_VEIL);

        RemoveSpEffectFromChrIns(
            player,
            SPEFFECT_PEARLDRAKE);

        RemoveSpEffectFromChrIns(
            player,
            SPEFFECT_CRUCIBLE_FEATHER);

        RemoveSpEffectFromChrIns(
            player,
            SPEFFECT_CRUCIBLE_FEATHER_CYCLE);

        RemoveSpEffectFromChrIns(
            player,
            SPEFFECT_ALLOYED_FIST);

        RemoveSpEffectFromChrIns(
            player,
            SPEFFECT_ALLOYED_FIST_ARMED);

        RemoveSpEffectFromChrIns(
            player,
            SPEFFECT_ALLOYED_FIST_CONSUME);

        // Clear pending equip requests.
        RemoveSpEffectFromChrIns(
            player,
            SPEFFECT_EQUIP_CONCEALING);

        RemoveSpEffectFromChrIns(
            player,
            SPEFFECT_EQUIP_PEARLDRAKE);

        RemoveSpEffectFromChrIns(
            player,
            SPEFFECT_EQUIP_CRUCIBLE_FEATHER);

        RemoveSpEffectFromChrIns(
            player,
            SPEFFECT_EQUIP_ALLOYED_FIST);

        // NO shatter request.
        // NO 6700.
        // This state is disappearing because the role changed,
        // not because the talisman was consumed.

        UnequipMMTalismanEverywhere();
        RemoveLocalTalismanInventory();

        return true;
    }

    bool ConsumeAlloyedFist()
    {
        const uintptr_t player =
            GetLocalPlayerIns();

        if (!player)
            return false;

        if (GetItemIndex(
            static_cast<int32_t>(ALLOYED_FIST)) < 0)
        {
            return false;
        }

        Log("[ALLOYED FIST] Successful hit -> consuming talisman.");

        // --------------------------------------------------------
        // NO generic talisman shatter VFX here.
        //
        // 312511 already provides Alloyed Fist's impact SFX/VFX.
        // --------------------------------------------------------

        RemoveSpEffectFromChrIns(
            player,
            SPEFFECT_ALLOYED_FIST);

        RemoveSpEffectFromChrIns(
            player,
            SPEFFECT_ALLOYED_FIST_ARMED);

        RemoveSpEffectFromChrIns(
            player,
            SPEFFECT_ALLOYED_FIST_CONSUME);

        RemoveSpEffectFromChrIns(
            player,
            SPEFFECT_EQUIP_ALLOYED_FIST);

        // Perform the same network-visible valid accessory
        // replacement used by the other consumed talismans.
        if (!FlushMMTalismanWithNull())
        {
            Log(
                "[ERROR] Alloyed Fist null flush failed; "
                "falling back to local cleanup.");

            UnequipMMTalismanEverywhere();
            RemoveLocalTalismanInventory();
        }

        Log("[ALLOYED FIST] Consumed.");

        return true;
    }

    bool ClearGreed()
    {
        const uintptr_t player =
            GetLocalPlayerIns();

        if (!player)
            return false;

        RemoveSpEffectFromChrIns(
            player,
            SPEFFECT_MARK_OF_GREED);

        RemoveSpEffectFromChrIns(
            player,
            SPEFFECT_MARK_OF_GREED_DEATH);

        return true;
    }

    bool RequestRoundCleanup()
    {
        const uintptr_t player =
            GetLocalPlayerIns();

        if (!player)
            return false;

        Log("[ROUND] Cleanup requested.");

        // Make sure a player can never remain frozen
        // during / after round cleanup.
        RemoveSpEffectFromChrIns(
            player,
            SPEFFECT_ROUND_START_LOCK);

        // Immediate local state cleanup.
        UnequipMMTalismanEverywhere();

        RemoveSpEffectFromChrIns(
            player,
            SPEFFECT_CONCEALING_VEIL);

        RemoveSpEffectFromChrIns(
            player,
            SPEFFECT_PEARLDRAKE);

        RemoveSpEffectFromChrIns(
            player,
            SPEFFECT_CRUCIBLE_FEATHER);

        RemoveSpEffectFromChrIns(
            player,
            SPEFFECT_CRUCIBLE_FEATHER_CYCLE);

        RemoveSpEffectFromChrIns(
            player,
            SPEFFECT_EQUIP_CONCEALING);

        RemoveSpEffectFromChrIns(
            player,
            SPEFFECT_EQUIP_PEARLDRAKE);

        RemoveSpEffectFromChrIns(
            player,
            SPEFFECT_EQUIP_CRUCIBLE_FEATHER);

        RemoveSpEffectFromChrIns(
            player,
            SPEFFECT_ALLOYED_FIST);

        RemoveSpEffectFromChrIns(
            player,
            SPEFFECT_ALLOYED_FIST_ARMED);

        RemoveSpEffectFromChrIns(
            player,
            SPEFFECT_ALLOYED_FIST_CONSUME);

        RemoveSpEffectFromChrIns(
            player,
            SPEFFECT_EQUIP_ALLOYED_FIST);

        RemoveSpEffectFromChrIns(
            player,
            SPEFFECT_SHERIFF_ATTACK_TAGGER);

        ClearGreed();

        // Remove physical MM talisman inventory.
        RemoveLocalTalismanInventory();

        // Leave 500050 available as the broader future
        // app/server round-cleanup command.
        return ApplySpEffectToChrIns(
            player,
            SPEFFECT_ROUND_CLEANUP_REQUEST);
    }

    bool ShowItemPopup(
        uint32_t itemId,
        int32_t quantity = 1,
        uint32_t gem = 0)
    {
        if (!g_itemPopupAddress ||
            !g_mapItemManAddress)
        {
            return false;
        }

        const uintptr_t mapItemMan =
            *reinterpret_cast<uintptr_t*>(
                g_mapItemManAddress);

        if (!mapItemMan)
            return false;

        uint32_t data[4]{};

        data[0] = itemId;
        data[1] = static_cast<uint32_t>(quantity);
        data[2] = 0xFFFFFFFF;
        data[3] = gem;

        auto fn =
            reinterpret_cast<ItemPopupFn>(
                g_itemPopupAddress);

        fn(
            mapItemMan + 0xA0,
            reinterpret_cast<uintptr_t>(data));

        Log(
            "[UI] Item popup displayed: " +
            Hex(itemId));

        return true;
    }

    // -----------------------------------------------------------------


    // ============================================================
    // Murder Mystery - Full Armor Bootstrap
    // Base game: 559 armor pieces
    // SotE:      145 armor pieces
    // Total:     704 armor pieces
    // ============================================================

    bool GiveItemsBatched(
        const uint32_t* itemIds,
        size_t count)
    {
        constexpr size_t BATCH_SIZE = 10;

        size_t index = 0;

        while (index < count)
        {
            const size_t remaining = count - index;
            const size_t thisBatch =
                remaining > BATCH_SIZE ? BATCH_SIZE : remaining;

            if (!GiveItemBatch(itemIds + index, thisBatch))
            {
                Log(
                    "[ERROR] ItemGive batch failed at index " +
                    std::to_string(index));
                return false;
            }

            index += thisBatch;
        }

        return true;
    }

    static const uint32_t BASE_ARMOR[] =
    {
        0x10009C40, // Iron Helmet
        0x10009CA4, // Scale Armor
        0x10009D08, // Iron Gauntlets
        0x10009D6C, // Leather Trousers
        0x1000C350, // Kaiden Helm
        0x1000C3B4, // Kaiden Armor
        0x1000C418, // Kaiden Gauntlets
        0x1000C47C, // Kaiden Trousers
        0x1000EA60, // Drake Knight Helm
        0x1000EAC4, // Drake Knight Armor
        0x1000EB28, // Drake Knight Gauntlets
        0x1000EB8C, // Drake Knight Greaves
        0x1000EE48, // Drake Knight Helm (Altered)
        0x1000EEAC, // Drake Knight Armor (Altered)
        0x10013880, // Scaled Helm
        0x100138E4, // Scaled Armor
        0x10013948, // Scaled Gauntlets
        0x100139AC, // Scaled Greaves
        0x10013CCC, // Scaled Armor (Altered)
        0x10015F90, // Perfumer Hood
        0x10015FF4, // Perfumer Robe
        0x10016058, // Perfumer Gloves
        0x100160BC, // Perfumer Sarong
        0x100163DC, // Perfumer Robe (Altered)
        0x100186A0, // Traveler's Hat
        0x10018704, // Perfumer's Traveling Garb
        0x10018768, // Traveler's Gloves
        0x100187CC, // Traveler's Slops
        0x10018AEC, // Perfumer's Traveling Garb (Altered)
        0x1001D4C0, // Alberich's Pointed Hat
        0x1001D524, // Alberich's Robe
        0x1001D588, // Alberich's Bracers
        0x1001D5EC, // Alberich's Trousers
        0x1001D8A8, // Alberich's Pointed Hat (Altered)
        0x1001D90C, // Alberich's Robe (Altered)
        0x1001FBD0, // Spellblade's Pointed Hat
        0x1001FC34, // Spellblade's Traveling Attire
        0x1001FC98, // Spellblade's Gloves
        0x1001FCFC, // Spellblade's Trousers
        0x1002001C, // Spellblade's Traveling Attire (Altered)
        0x100222E0, // Bull-Goat Helm
        0x10022344, // Bull-Goat Armor
        0x100223A8, // Bull-Goat Gauntlets
        0x1002240C, // Bull-Goat Greaves
        0x100249F0, // Iron Kasa
        0x10024A54, // Ronin's Armor
        0x10024AB8, // Ronin's Gauntlets
        0x10024B1C, // Ronin's Greaves
        0x10024E3C, // Ronin's Armor (Altered)
        0x10027100, // Guilty Hood
        0x10027164, // Cloth Garb
        0x1002722C, // Cloth Trousers
        0x10029810, // Black Wolf Mask
        0x10029874, // Blaidd's Armor
        0x100298D8, // Blaidd's Gauntlets
        0x1002993C, // Blaidd's Greaves
        0x10029C5C, // Blaidd's Armor (Altered)
        0x1002BF20, // Black Knife Hood
        0x1002BF84, // Black Knife Armor
        0x1002BFE8, // Black Knife Gauntlets
        0x1002C04C, // Black Knife Greaves
        0x1002C36C, // Black Knife Armor (Altered)
        0x1002E630, // Exile Hood
        0x1002E694, // Exile Armor
        0x1002E6F8, // Exile Gauntlets
        0x1002E75C, // Exile Greaves
        0x10030D40, // Banished Knight Helm
        0x10030DA4, // Banished Knight Armor
        0x10030E08, // Banished Knight Gauntlets
        0x10030E6C, // Banished Knight Greaves
        0x10031128, // Banished Knight Helm (Altered)
        0x1003118C, // Banished Knight Armor (Altered)
        0x10033450, // Briar Helm
        0x100334B4, // Briar Armor
        0x10033518, // Briar Gauntlets
        0x1003357C, // Briar Greaves
        0x1003389C, // Briar Armor (Altered)
        0x10035B60, // Page Hood
        0x10035BC4, // Page Garb
        0x10035C8C, // Page Trousers
        0x10035FAC, // Page Garb (Altered)
        0x10038270, // Night's Cavalry Helm
        0x100382D4, // Night's Cavalry Armor
        0x10038338, // Night's Cavalry Gauntlets
        0x1003839C, // Night's Cavalry Greaves
        0x10038658, // Night's Cavalry Helm (Altered)
        0x100386BC, // Night's Cavalry Armor (Altered)
        0x1003A980, // Blue Silver Mail Hood
        0x1003A9E4, // Blue Silver Mail Armor
        0x1003AA48, // Blue Silver Bracelets
        0x1003AAAC, // Blue Silver Mail Skirt
        0x1003ADCC, // Blue Silver Mail Armor (Altered)
        0x1003D090, // Nomadic Merchant's Chapeau
        0x1003D0F4, // Nomadic Merchant's Finery
        0x1003D1BC, // Nomadic Merchant's Trousers
        0x1003D4DC, // Nomadic Merchant's Finery (Altered)
        0x1003F7A0, // Malformed Dragon Helm
        0x1003F804, // Malformed Dragon Armor
        0x1003F868, // Malformed Dragon Gauntlets
        0x1003F8CC, // Malformed Dragon Greaves
        0x10041EB0, // Tree Sentinel Helm
        0x10041F14, // Tree Sentinel Armor
        0x10041F78, // Tree Sentinel Gauntlets
        0x10041FDC, // Tree Sentinel Greaves
        0x100422FC, // Tree Sentinel Armor (Altered)
        0x100445C0, // Royal Knight Helm
        0x10044624, // Royal Knight Armor
        0x10044688, // Royal Knight Gauntlets
        0x100446EC, // Royal Knight Greaves
        0x10044A0C, // Royal Knight Armor (Altered)
        0x10046CD0, // Nox Monk Hood
        0x10046D34, // Nox Monk Armor
        0x10046D98, // Nox Monk Bracelets
        0x10046DFC, // Nox Monk Greaves
        0x100470B8, // Nox Monk Hood (Altered)
        0x1004711C, // Nox Monk Armor (Altered)
        0x100474A0, // Nox Swordstress Crown
        0x10047504, // Nox Swordstress Armor
        0x10047888, // Night Maiden Twin Crown
        0x100478EC, // Night Maiden Armor
        0x10047C70, // Nox Swordstress Crown (Altered)
        0x10047CD4, // Nox Swordstress Armor (Altered)
        0x100493E0, // Great Horned Headband
        0x10049444, // Fur Raiment
        0x1004950C, // Fur Leggings
        0x100497C8, // Shining Horned Headband
        0x1004982C, // Shaman Furs
        0x100498F4, // Shaman Leggings
        0x1004BAF0, // Duelist Helm
        0x1004BB54, // Gravekeeper Cloak
        0x1004BC1C, // Duelist Greaves
        0x1004BF3C, // Gravekeeper Cloak (Altered)
        0x1004E200, // Sanguine Noble Hood
        0x1004E264, // Sanguine Noble Robe
        0x1004E32C, // Sanguine Noble Waistcloth
        0x10050910, // Guardian Mask
        0x10050974, // Guardian Garb (Full Bloom)
        0x100509D8, // Guardian Bracers
        0x10050A3C, // Guardian Greaves
        0x10050D5C, // Guardian Garb
        0x10053020, // Cleanrot Helm
        0x10053084, // Cleanrot Armor
        0x100530E8, // Cleanrot Gauntlets
        0x1005314C, // Cleanrot Greaves
        0x10053408, // Cleanrot Helm (Altered)
        0x1005346C, // Cleanrot Armor (Altered)
        0x10055730, // Fire Monk Hood
        0x10055794, // Fire Monk Armor
        0x100557F8, // Fire Monk Gauntlets
        0x1005585C, // Fire Monk Greaves
        0x10055B18, // Blackflame Monk Hood
        0x10055B7C, // Blackflame Monk Armor
        0x10055BE0, // Blackflame Monk Gauntlets
        0x10055C44, // Blackflame Monk Greaves
        0x10057E40, // Fire Prelate Helm
        0x10057EA4, // Fire Prelate Armor
        0x10057F08, // Fire Prelate Gauntlets
        0x10057F6C, // Fire Prelate Greaves
        0x1005828C, // Fire Prelate Armor (Altered)
        0x1005A550, // Aristocrat Headband
        0x1005A5B4, // Aristocrat Garb
        0x1005A67C, // Aristocrat Boots
        0x1005A99C, // Aristocrat Garb (Altered)
        0x1005CC60, // Aristocrat Hat
        0x1005CCC4, // Aristocrat Coat
        0x1005F370, // Old Aristocrat Cowl
        0x1005F3D4, // Old Aristocrat Gown
        0x1005F49C, // Old Aristocrat Shoes
        0x100668A0, // Vulgar Militia Helm
        0x10066904, // Vulgar Militia Armor
        0x10066968, // Vulgar Militia Gauntlets
        0x100669CC, // Vulgar Militia Greaves
        0x10068FB0, // Sage Hood
        0x10069014, // Sage Robe
        0x100690DC, // Sage Trousers
        0x1006B6C0, // Pumpkin Helm
        0x100704E0, // Elden Lord Crown
        0x10070544, // Elden Lord Armor
        0x100705A8, // Elden Lord Bracers
        0x1007060C, // Elden Lord Greaves
        0x1007092C, // Elden Lord Armor (Altered)
        0x10072BF0, // Radahn's Redmane Helm
        0x10072C54, // Radahn's Lion Armor
        0x10072CB8, // Radahn's Gauntlets
        0x10072D1C, // Radahn's Greaves
        0x1007303C, // Radahn's Lion Armor (Altered)
        0x10075364, // Lord of Blood's Robe
        0x1007574C, // Lord of Blood's Robe (Altered)
        0x1007C830, // Queen's Crescent Crown
        0x1007C894, // Queen's Robe
        0x1007C8F8, // Queen's Bracelets
        0x1007C95C, // Queen's Leggings
        0x1007EF40, // Godskin Apostle Hood
        0x1007EFA4, // Godskin Apostle Robe
        0x1007F008, // Godskin Apostle Bracelets
        0x1007F06C, // Godskin Apostle Trousers
        0x10081650, // Godskin Noble Hood
        0x100816B4, // Godskin Noble Robe
        0x10081718, // Godskin Noble Bracelets
        0x1008177C, // Godskin Noble Trousers
        0x10083D60, // Depraved Perfumer Headscarf
        0x10083DC4, // Depraved Perfumer Robe
        0x10083E28, // Depraved Perfumer Gloves
        0x10083E8C, // Depraved Perfumer Trousers
        0x100841AC, // Depraved Perfumer Robe (Altered)
        0x1008B290, // Crucible Axe Helm
        0x1008B2F4, // Crucible Axe Armor
        0x1008B358, // Crucible Gauntlets
        0x1008B3BC, // Crucible Greaves
        0x1008B678, // Crucible Tree Helm
        0x1008B6DC, // Crucible Tree Armor
        0x1008BAC4, // Crucible Axe Armor (Altered)
        0x1008BEAC, // Crucible Tree Armor (Altered)
        0x1008D9A0, // Lusat's Glintstone Crown
        0x1008DA04, // Lusat's Robe
        0x1008DA68, // Lusat's Manchettes
        0x1008DACC, // Old Sorcerer's Legwraps
        0x1008DD88, // Azur's Glintstone Crown
        0x1008DDEC, // Azur's Glintstone Robe
        0x1008DE50, // Azur's Manchettes
        0x100900B0, // All-Knowing Helm
        0x10090114, // All-Knowing Armor
        0x10090178, // All-Knowing Gauntlets
        0x100901DC, // All-Knowing Greaves
        0x100904FC, // All-Knowing Armor (Altered)
        0x100927C0, // Twinned Helm
        0x10092824, // Twinned Armor
        0x10092888, // Twinned Gauntlets
        0x100928EC, // Twinned Greaves
        0x10092C0C, // Twinned Armor (Altered)
        0x100975E0, // Prophet Blindfold
        0x10097644, // Corhyn's Robe
        0x1009770C, // Prophet Trousers
        0x10097A2C, // Prophet Robe (Altered)
        0x10097E14, // Prophet Robe
        0x10099CF0, // Astrologer Hood
        0x10099D54, // Astrologer Robe
        0x10099DB8, // Astrologer Gloves
        0x10099E1C, // Astrologer Trousers
        0x1009A13C, // Astrologer Robe (Altered)
        0x1009C400, // Lionel's Helm
        0x1009C464, // Lionel's Armor
        0x1009C4C8, // Lionel's Gauntlets
        0x1009C52C, // Lionel's Greaves
        0x1009C84C, // Lionel's Armor (Altered)
        0x1009EB10, // Hoslow's Helm
        0x1009EB74, // Hoslow's Armor
        0x1009EBD8, // Hoslow's Gauntlets
        0x1009EC3C, // Hoslow's Greaves
        0x1009EEF8, // Diallos's Mask
        0x1009F344, // Hoslow's Armor (Altered)
        0x100A1220, // Vagabond Knight Helm
        0x100A1284, // Vagabond Knight Armor
        0x100A12E8, // Vagabond Knight Gauntlets
        0x100A134C, // Vagabond Knight Greaves
        0x100A166C, // Vagabond Knight Armor (Altered)
        0x100A3930, // Blue Cloth Cowl
        0x100A3994, // Blue Cloth Vest
        0x100A39F8, // Warrior Gauntlets
        0x100A3A5C, // Warrior Greaves
        0x100A6040, // White Mask
        0x100A60A4, // War Surgeon Gown
        0x100A6108, // War Surgeon Gloves
        0x100A616C, // War Surgeon Trousers
        0x100A648C, // War Surgeon Gown (Altered)
        0x100A8750, // Royal Remains Helm
        0x100A87B4, // Royal Remains Armor
        0x100A8818, // Royal Remains Gauntlets
        0x100A887C, // Royal Remains Greaves
        0x100AFC80, // Beast Champion Helm
        0x100AFCE4, // Beast Champion Armor
        0x100AFD48, // Beast Champion Gauntlets
        0x100AFDAC, // Beast Champion Greaves
        0x100B00CC, // Beast Champion Armor (Altered)
        0x100B2390, // Champion Headband
        0x100B23F4, // Champion Pauldron
        0x100B2458, // Champion Bracers
        0x100B24BC, // Champion Gaiters
        0x100B4AA0, // Crimson Hood
        0x100B4B04, // Noble's Traveling Garb
        0x100B4B68, // Noble's Gloves
        0x100B4BCC, // Noble's Trousers
        0x100B4E88, // Navy Hood
        0x100B98C0, // Maliketh's Helm
        0x100B9924, // Maliketh's Armor
        0x100B9988, // Maliketh's Gauntlets
        0x100B99EC, // Maliketh's Greaves
        0x100B9D0C, // Maliketh's Armor (Altered)
        0x100BBFD0, // Malenia's Winged Helm
        0x100BC034, // Malenia's Armor
        0x100BC098, // Malenia's Gauntlet
        0x100BC0FC, // Malenia's Greaves
        0x100BC41C, // Malenia's Armor (Altered)
        0x100BE6E0, // Veteran's Helm
        0x100BE744, // Veteran's Armor
        0x100BE7A8, // Veteran's Gauntlets
        0x100BE80C, // Veteran's Greaves
        0x100BEB2C, // Veteran's Armor (Altered)
        0x100C0DF0, // Bloodhound Knight Helm
        0x100C0E54, // Bloodhound Knight Armor
        0x100C0EB8, // Bloodhound Knight Gauntlets
        0x100C0F1C, // Bloodhound Knight Greaves
        0x100C123C, // Bloodhound Knight Armor (Altered)
        0x100C3500, // Festive Hood
        0x100C3564, // Festive Garb
        0x100C38E8, // Festive Hood (Altered)
        0x100C394C, // Festive Garb (Altered)
        0x100C3CD0, // Blue Festive Hood
        0x100C3D34, // Blue Festive Garb
        0x100C5C10, // Commoner's Headband
        0x100C5C74, // Commoner's Garb
        0x100C5D3C, // Commoner's Shoes
        0x100C5FF8, // Commoner's Headband (Altered)
        0x100C605C, // Commoner's Garb (Altered)
        0x100C63E0, // Commoner's Simple Garb
        0x100C6444, // Commoner's Simple Garb (Altered)
        0x100C8320, // Envoy Crown
        0x100CAA30, // Twinsage Glintstone Crown
        0x100CAA94, // Raya Lucarian Robe
        0x100CAAF8, // Sorcerer Manchettes
        0x100CAB5C, // Sorcerer Leggings
        0x100CAE18, // Olivinus Glintstone Crown
        0x100CB200, // Lazuli Glintstone Crown
        0x100CB5E8, // Karolos Glintstone Crown
        0x100CB9D0, // Witch's Glintstone Crown
        0x100CD140, // Marionette Soldier Helm
        0x100CD1A4, // Marionette Soldier Armor
        0x100CF850, // Marionette Soldier Birdhelm
        0x100D1F60, // Raging Wolf Helm
        0x100D1FC4, // Raging Wolf Armor
        0x100D2028, // Raging Wolf Gauntlets
        0x100D208C, // Raging Wolf Greaves
        0x100D23AC, // Raging Wolf Armor (Altered)
        0x100D4670, // Land of Reeds Helm
        0x100D46D4, // Land of Reeds Armor
        0x100D4738, // Land of Reeds Gauntlets
        0x100D479C, // Land of Reeds Greaves
        0x100D4ABC, // Land of Reeds Armor (Altered)
        0x100D4E40, // Okina Mask
        0x100D4EA4, // White Reed Armor
        0x100D4F08, // White Reed Gauntlets
        0x100D4F6C, // White Reed Greaves
        0x100D6D80, // Confessor Hood
        0x100D6DE4, // Confessor Armor
        0x100D6E48, // Confessor Gloves
        0x100D6EAC, // Confessor Boots
        0x100D7168, // Confessor Hood (Altered)
        0x100D71CC, // Confessor Armor (Altered)
        0x100D9490, // Prisoner Iron Mask
        0x100D94F4, // Prisoner Clothing
        0x100D95BC, // Prisoner Trousers
        0x100D9878, // Blackguard's Iron Mask
        0x100DBBA0, // Traveling Maiden Hood
        0x100DBC04, // Traveling Maiden Robe
        0x100DBC68, // Traveling Maiden Gloves
        0x100DBCCC, // Traveling Maiden Boots
        0x100DBFEC, // Traveling Maiden Robe (Altered)
        0x100DC370, // Finger Maiden Fillet
        0x100DC3D4, // Finger Maiden Robe
        0x100DC49C, // Finger Maiden Shoes
        0x100DC7BC, // Finger Maiden Robe (Altered)
        0x100DE2B0, // Preceptor's Big Hat
        0x100DE314, // Preceptor's Long Gown
        0x100DE378, // Preceptor's Gloves
        0x100DE3DC, // Preceptor's Trousers
        0x100DE698, // Mask of Confidence
        0x100DE6FC, // Preceptor's Long Gown (Altered)
        0x100E30D0, // Skeletal Mask
        0x100E3134, // Raptor's Black Feathers
        0x100E3198, // Bandit Manchettes
        0x100E31FC, // Bandit Boots
        0x100E351C, // Bandit Garb
        0x100E57E0, // Eccentric's Hood
        0x100E5844, // Eccentric's Armor
        0x100E58A8, // Eccentric's Manchettes
        0x100E590C, // Eccentric's Breeches
        0x100E5BC8, // Eccentric's Hood (Altered)
        0x100E7EF0, // Fingerprint Helm
        0x100E7F54, // Fingerprint Armor
        0x100E7FB8, // Fingerprint Gauntlets
        0x100E801C, // Fingerprint Greaves
        0x100E833C, // Fingerprint Armor (Altered)
        0x100EA600, // Consort's Mask
        0x100EA664, // Consort's Robe
        0x100EA72C, // Consort's Trousers
        0x100EA9E8, // Ruler's Mask
        0x100EAA4C, // Ruler's Robe
        0x100EAE34, // Upper-Class Robe
        0x100EB1B8, // Marais Mask
        0x100EB21C, // Marais Robe
        0x100EB280, // Bloodsoaked Manchettes
        0x100EB5A0, // Bloodsoaked Mask
        0x100EB604, // Official's Attire
        0x100ECD10, // Omen Helm
        0x100ECD74, // Omen Armor
        0x100ECDD8, // Omen Gauntlets
        0x100ECE3C, // Omen Greaves
        0x100EF420, // Carian Knight Helm
        0x100EF484, // Carian Knight Armor
        0x100EF4E8, // Carian Knight Gauntlets
        0x100EF54C, // Carian Knight Greaves
        0x100EF86C, // Carian Knight Armor (Altered)
        0x100F1B30, // Hierodas Glintstone Crown
        0x100F1B94, // Errant Sorcerer Robe
        0x100F1BF8, // Errant Sorcerer Manchettes
        0x100F1C5C, // Errant Sorcerer Boots
        0x100F1F7C, // Errant Sorcerer Robe (Altered)
        0x100F4240, // Haima Glintstone Crown
        0x100F42A4, // Battlemage Robe
        0x100F4308, // Battlemage Manchettes
        0x100F436C, // Battlemage Legwraps
        0x100F6950, // Snow Witch Hat
        0x100F69B4, // Snow Witch Robe
        0x100F6A7C, // Snow Witch Skirt
        0x100F6D9C, // Snow Witch Robe (Altered)
        0x100F90C4, // Traveler's Clothes
        0x100F9128, // Traveler's Manchettes
        0x100F918C, // Traveler's Boots
        0x100FB770, // Juvenile Scholar Cap
        0x100FB7D4, // Juvenile Scholar Robe
        0x100FDE80, // Radiant Gold Mask
        0x100FDEE4, // Goldmask's Rags
        0x100FDF48, // Gold Bracelets
        0x100FDFAC, // Gold Waistwrap
        0x101005F4, // Fell Omen Cloak
        0x10102CA0, // Albinauric Mask
        0x10102D04, // Dirty Chainmail
        0x101053B0, // Zamor Mask
        0x10105414, // Zamor Armor
        0x10105478, // Zamor Bracelets
        0x101054DC, // Zamor Legwraps
        0x10107AC0, // Imp Head (Cat)
        0x10107EA8, // Imp Head (Fanged)
        0x10108290, // Imp Head (Long-Tongued)
        0x10108678, // Imp Head (Corpse)
        0x10108A60, // Imp Head (Wolf)
        0x10108E48, // Imp Head (Elder)
        0x1010A1D0, // Silver Tear Mask
        0x1010C8E0, // Chain Coif
        0x1010C944, // Chain Armor
        0x1010C9A8, // Chain Gauntlets
        0x1010CA0C, // Chain Leggings
        0x1010CCC8, // Greathelm
        0x1010CD2C, // Eye Surcoat
        0x1010D114, // Tree Surcoat
        0x1010EFF0, // Octopus Head
        0x10111700, // Jar
        0x10113E10, // Mushroom Head
        0x10113E74, // Mushroom Body
        0x10113ED8, // Mushroom Arms
        0x10113F3C, // Mushroom Legs
        0x1013D620, // Nox Mirrorhelm
        0x1013DA08, // Iji's Mirrorhelm
        0x10155CC0, // Black Hood
        0x10155D24, // Leather Armor
        0x10155D88, // Leather Gloves
        0x10155DEC, // Leather Boots
        0x101560A8, // Bandit Mask
        0x1016E360, // Knight Helm
        0x1016E3C4, // Knight Armor
        0x1016E428, // Knight Gauntlets
        0x1016E48C, // Knight Greaves
        0x10186A00, // Greathood
        0x1019F0A0, // Godrick Soldier Helm
        0x1019F104, // Tree-and-Beast Surcoat
        0x1019F168, // Godrick Soldier Gauntlets
        0x1019F1CC, // Godrick Soldier Greaves
        0x101A17B0, // Raya Lucarian Helm
        0x101A1814, // Cuckoo Surcoat
        0x101A1878, // Raya Lucarian Gauntlets
        0x101A18DC, // Raya Lucarian Greaves
        0x101A3EC0, // Leyndell Soldier Helm
        0x101A3F24, // Erdtree Surcoat
        0x101A3F88, // Leyndell Soldier Gauntlets
        0x101A3FEC, // Leyndell Soldier Greaves
        0x101A65D0, // Radahn Soldier Helm
        0x101A6634, // Redmane Surcoat
        0x101A6698, // Radahn Soldier Gauntlets
        0x101A66FC, // Radahn Soldier Greaves
        0x101A8D44, // Mausoleum Surcoat
        0x101A8DA8, // Mausoleum Gauntlets
        0x101A8E0C, // Mausoleum Greaves
        0x101AB3F0, // Haligtree Helm
        0x101AB454, // Haligtree Crest Surcoat
        0x101AB4B8, // Haligtree Gauntlets
        0x101AB51C, // Haligtree Greaves
        0x101ADB00, // Gelmir Knight Helm
        0x101ADB64, // Gelmir Knight Armor
        0x101ADBC8, // Gelmir Knight Gauntlets
        0x101ADC2C, // Gelmir Knight Greaves
        0x101ADF4C, // Gelmir Knight Armor (Altered)
        0x101B0210, // Godrick Knight Helm
        0x101B0274, // Godrick Knight Armor
        0x101B02D8, // Godrick Knight Gauntlets
        0x101B033C, // Godrick Knight Greaves
        0x101B065C, // Godrick Knight Armor (Altered)
        0x101B2920, // Cuckoo Knight Helm
        0x101B2984, // Cuckoo Knight Armor
        0x101B29E8, // Cuckoo Knight Gauntlets
        0x101B2A4C, // Cuckoo Knight Greaves
        0x101B2D6C, // Cuckoo Knight Armor (Altered)
        0x101B5030, // Leyndell Knight Helm
        0x101B5094, // Leyndell Knight Armor
        0x101B50F8, // Leyndell Knight Gauntlets
        0x101B515C, // Leyndell Knight Greaves
        0x101B547C, // Leyndell Knight Armor (Altered)
        0x101B7740, // Redmane Knight Helm
        0x101B77A4, // Redmane Knight Armor
        0x101B7808, // Redmane Knight Gauntlets
        0x101B786C, // Redmane Knight Greaves
        0x101B7B8C, // Redmane Knight Armor (Altered)
        0x101B9EB4, // Mausoleum Knight Armor
        0x101B9F18, // Mausoleum Knight Gauntlets
        0x101B9F7C, // Mausoleum Knight Greaves
        0x101BA29C, // Mausoleum Knight Armor (Altered)
        0x101BC560, // Haligtree Knight Helm
        0x101BC5C4, // Haligtree Knight Armor
        0x101BC628, // Haligtree Knight Gauntlets
        0x101BC68C, // Haligtree Knight Greaves
        0x101BC9AC, // Haligtree Knight Armor (Altered)
        0x101BEC70, // Foot Soldier Cap
        0x101BECD4, // Chain-Draped Tabard
        0x101BED38, // Foot Soldier Gauntlets
        0x101BED9C, // Foot Soldier Greaves
        0x101C1380, // Foot Soldier Helmet
        0x101C13E4, // Foot Soldier Tabard
        0x101C3A90, // Gilded Foot Soldier Cap
        0x101C3AF4, // Leather-Draped Tabard
        0x101C61A0, // Foot Soldier Helm
        0x101C6204, // Scarlet Tabard
        0x101C8914, // Bloodsoaked Tabard
        0x101CAFC0, // Sacred Crown Helm
        0x101CB024, // Ivory-Draped Tabard
        0x101CD6D0, // Omensmirk Mask
        0x101CD734, // Omenkiller Robe
        0x101CD798, // Omenkiller Long Gloves
        0x101CD7FC, // Omenkiller Boots
        0x101CFDE0, // Ash-of-War Scarab
        0x101D01C8, // Incantation Scarab
        0x101D05B0, // Glintstone Scarab
        0x101D24F0, // Crimson Tear Scarab
        0x101D4C00, // Cerulean Tear Scarab
        0x101D7374, // Deathbed Dress
        0x101D9A20, // Fia's Hood
        0x101D9A84, // Fia's Robe
        0x101D9E6C, // Fia's Robe (Altered)
        0x101E3660, // Highwayman Hood
        0x101E36C4, // Highwayman Cloth Armor
        0x101E3728, // Highwayman Gauntlets
        0x101E5D70, // High Page Hood
        0x101E5DD4, // High Page Clothes
        0x101E61BC, // High Page Clothes (Altered)
        0x101E8480, // Rotten Duelist Helm
        0x101E84E4, // Rotten Gravekeeper Cloak
        0x101E85AC, // Rotten Duelist Greaves
        0x101E88CC, // Rotten Gravekeeper Cloak (Altered)
        0x101EAB90, // Mushroom Crown
        0x101ED2A0, // Black Dumpling
        0x101EF9B0, // Lazuli Robe
    };

    static const uint32_t DLC_ARMOR[] =
    {
        0x102DC6C0, // Dane's Hat
        0x102DC724, // Dryleaf Robe
        0x102DC788, // Dryleaf Arm Wraps
        0x102DC7EC, // Dryleaf Cuissardes
        0x102DCB0C, // Dryleaf Robe (Altered)
        0x102DEDD0, // Gaius's Helm
        0x102DEE34, // Gaius's Armor
        0x102DEE98, // Gaius's Gauntlets
        0x102DEEFC, // Gaius's Greaves
        0x104C4B40, // Oathseeker Knight Helm
        0x104C4BA4, // Leda's Armor
        0x104C4C08, // Oathseeker Knight Gauntlets
        0x104C4C6C, // Oathseeker Knight Greaves
        0x104C5374, // Oathseeker Knight Armor
        0x104C7250, // Verdigris Helm
        0x104C72B4, // Verdigris Armor
        0x104C7318, // Verdigris Gauntlets
        0x104C737C, // Verdigris Greaves
        0x104C9960, // Pelt of Ralva
        0x104C99C4, // Iron Rivet Armor
        0x104C9A28, // Iron Rivet Gauntlets
        0x104C9A8C, // Iron Rivet Greaves
        0x104C9D48, // Fang Helm
        0x104CC070, // Thiollier's Mask
        0x104CC0D4, // Thiollier's Garb
        0x104CC138, // Thiollier's Gloves
        0x104CC19C, // Thiollier's Trousers
        0x104CC4BC, // Thiollier's Garb (Altered)
        0x104D35A0, // High Priest Hat
        0x104D3604, // High Priest Robe
        0x104D3668, // High Priest Gloves
        0x104D36CC, // High Priest Undergarments
        0x104D3DD4, // Finger Robe
        0x104D5CB0, // Caterpillar Mask
        0x104D5D14, // Braided Cord Robe
        0x104D5D78, // Braided Arm Wraps
        0x104D5DDC, // Soiled Loincloth
        0x104D83C0, // Dancer's Hood
        0x104D8424, // Dancer's Dress
        0x104D8488, // Dancer's Bracer
        0x104D84EC, // Dancer's Trousers
        0x104D880C, // Dancer's Dress (Altered)
        0x104DAAD0, // Helm of Night
        0x104DAB34, // Armor of Night
        0x104DAB98, // Gauntlets of Night
        0x104DABFC, // Greaves of Night
        0x104DD1E0, // Igon's Helm
        0x104DD244, // Igon's Armor
        0x104DD2A8, // Igon's Gauntlets
        0x104DD30C, // Igon's Loincloth
        0x104DD5C8, // Igon's Helm (Altered)
        0x104DD62C, // Igon's Armor (Altered)
        0x104DF8F0, // Wise Man's Mask
        0x104DF954, // Ansbach's Attire
        0x104DF9B8, // Ansbach's Manchettes
        0x104DFA1C, // Ansbach's Boots
        0x104DFD3C, // Ansbach's Attire (Altered)
        0x104E2000, // Freyja's Helm
        0x104E2064, // Freyja's Armor
        0x104E20C8, // Freyja's Gauntlets
        0x104E212C, // Freyja's Greaves
        0x104E244C, // Freyja's Armor (Altered)
        0x104E4710, // Helm of Solitude
        0x104E4774, // Armor of Solitude
        0x104E47D8, // Gauntlets of Solitude
        0x104E483C, // Greaves of Solitude
        0x104E4B5C, // Armor of Solitude (Altered)
        0x104E6E20, // Messmer Soldier Helm
        0x104E6E84, // Messmer Soldier Armor
        0x104E6EE8, // Messmer Soldier Gauntlets
        0x104E6F4C, // Messmer Soldier Greaves
        0x104E726C, // Messmer Soldier Armor (Altered)
        0x104E9530, // Black Knight Helm
        0x104E9594, // Black Knight Armor
        0x104E95F8, // Black Knight Gauntlets
        0x104E965C, // Black Knight Greaves
        0x104EBC40, // Rakshasa Helm
        0x104EBCA4, // Rakshasa Armor
        0x104EBD08, // Rakshasa Gauntlets
        0x104EBD6C, // Rakshasa Greaves
        0x104F0A60, // Fire Knight Helm
        0x104F0AC4, // Fire Knight Armor
        0x104F0B28, // Fire Knight Gauntlets
        0x104F0B8C, // Fire Knight Greaves
        0x104F0EAC, // Fire Knight Armor (Altered)
        0x104F1230, // Death Mask Helm
        0x104F1618, // Winged Serpent Helm
        0x104F1A00, // Salza's Hood
        0x104F3170, // Leather Headband
        0x104F31D4, // Gloried Attire
        0x104F3238, // Leather Arm Wraps
        0x104F329C, // Leather Leg Wraps
        0x104F3558, // Leather Crown
        0x104F35BC, // Highland Attire
        0x104F5880, // Death Knight Helm
        0x104F58E4, // Death Knight Armor
        0x104F5948, // Death Knight Gauntlets
        0x104F59AC, // Death Knight Greaves
        0x104F7F90, // Curseblade Mask
        0x104F7FF4, // Ascetic's Loincloth
        0x104F8058, // Ascetic's Wrist Guards
        0x104F80BC, // Ascetic's Ankle Guards
        0x104FA6A0, // Messmer's Helm
        0x104FA704, // Messmer's Armor
        0x104FA768, // Messmer's Gauntlets
        0x104FA7CC, // Messmer's Greaves
        0x104FAA88, // Messmer's Helm (Altered)
        0x104FCDB0, // Gravebird Helm
        0x104FCE14, // Gravebird's Blackquill Armor
        0x104FCE78, // Gravebird Bracelets
        0x104FCEDC, // Gravebird Anklets
        0x104FD1FC, // Gravebird Armor
        0x104FF4C0, // Common Soldier Helm
        0x104FF524, // Common Soldier Cloth Armor
        0x104FF588, // Common Soldier Gauntlets
        0x104FF5EC, // Common Soldier Greaves
        0x10501BD0, // Horned Warrior Helm
        0x10501C34, // Horned Warrior Armor
        0x10501C98, // Horned Warrior Gauntlets
        0x10501CFC, // Horned Warrior Greaves
        0x105023A0, // Divine Beast Helm
        0x10502404, // Divine Beast Warrior Armor
        0x10502788, // Divine Bird Helm
        0x105027EC, // Divine Bird Warrior Armor
        0x10502850, // Divine Bird Warrior Gauntlets
        0x105028B4, // Divine Bird Warrior Greaves
        0x105042E0, // Rellana's Helm
        0x10504344, // Rellana's Armor
        0x105043A8, // Rellana's Gloves
        0x1050440C, // Rellana's Greaves
        0x105069F0, // Young Lion's Helm
        0x10506A54, // Young Lion's Armor
        0x10506AB8, // Young Lion's Gauntlets
        0x10506B1C, // Young Lion's Greaves
        0x10506E3C, // Young Lion's Armor (Altered)
        0x105071C0, // Circlet of Light
        0x10509100, // Shadow Militiaman Helm
        0x10509164, // Shadow Militiaman Armor
        0x105091C8, // Shadow Militiaman Gauntlets
        0x1050922C, // Shadow Militiaman Greaves
        0x1050B810, // Divine Beast Head
        0x1050DF20, // St. Trina's Blossom
        0x10510630, // Crucible Hammer-Helm
        0x10512D40, // Greatjar
        0x10515450, // Imp Head (Lion)
    };

    bool GiveBootstrapItems()
    {
        static const uint32_t roleRunes[] =
        {
            0x400000BF, // Godrick
            0x400000C1, // Morgott
            0x400000C3  // Mohg
        };

        if (!GiveItemsBatched(
            BASE_ARMOR,
            sizeof(BASE_ARMOR) / sizeof(BASE_ARMOR[0])))
        {
            Log("[ERROR] Base-game armor grant failed.");
            return false;
        }

        // Intentionally unconditional for now.
        // We are testing what Elden Ring does on non-DLC clients.
        if (!GiveItemsBatched(
            DLC_ARMOR,
            sizeof(DLC_ARMOR) / sizeof(DLC_ARMOR[0])))
        {
            Log("[ERROR] DLC armor grant failed.");
            return false;
        }

        if (!GiveItemsBatched(
            roleRunes,
            sizeof(roleRunes) / sizeof(roleRunes[0])))
        {
            Log("[ERROR] Great Rune grant failed.");
            return false;
        }

        Log("[BOOTSTRAP] All armor + role Great Runes granted.");
        return true;
    }

    // --------------------------------------------------------

    bool ApplyMurdererLoadout()
    {
        //if (HasLocalTalisman())
        //{
        //    BreakLocalTalisman();
        //}

        ClearRoleMagic();

        const bool weaponOk =
            EquipItemNativeByItemId(
                SLOT_SECONDARY_RIGHT,
                MISERICORDE);
     

        const bool veilOk =
            EquipGoodsByItemId(
                SLOT_QUICK_ITEM_1,
                MIMIC_VEIL);

        if (!veilOk)
        {
            Log("[ERROR] Murderer Mimic Veil failed.");
        }
        else
        {
            SetSelectedQuickItem(0);
        }

        const bool runeOk =
            EquipGoodsByItemId(
                SLOT_GREAT_RUNE,
                MOHG_GREAT_RUNE,
                true);

        if (!weaponOk || !veilOk || !runeOk)
        {
            Log("[ROLE] Murderer loadout not ready; will retry.");
            return false;
        }

        Log("[ROLE] Murderer loadout applied.");
        return true;
    }

    bool ApplySheriffLoadout()
    {
        /*if (HasLocalTalisman())
        {
            BreakLocalTalisman();
        }*/

        UnequipGoodsSlot(
            SLOT_QUICK_ITEM_1);
        UnequipGoodsItemEverywhere(
            MIMIC_VEIL);

        const bool staffOk =
            EquipItemNativeByItemId(
                SLOT_SECONDARY_RIGHT,
                SHERIFF_STAFF);

        const bool runeOk =
            EquipGoodsByItemId(
                SLOT_GREAT_RUNE,
                MORGOTT_GREAT_RUNE,
                true);

        const bool magicOk =
            SetSheriffMagic();

        if (!staffOk || !runeOk || !magicOk)
        {
            Log("[ROLE] Sheriff loadout not ready; will retry.");
            return false;
        }

        const uintptr_t player =
            GetLocalPlayerIns();

        if (player &&
            !HasSpEffect(
                SPEFFECT_SHERIFF_ATTACK_TAGGER))
        {
            ApplySpEffectToChrIns(
                player,
                SPEFFECT_SHERIFF_ATTACK_TAGGER);
        }

        Log("[ROLE] Sheriff loadout applied.");
        return true;
    }

    bool ApplyInnocentLoadout()
    {
        ClearGreed();

        UnequipWeaponSlot(
            SLOT_SECONDARY_RIGHT);

        UnequipGoodsSlot(
            SLOT_QUICK_ITEM_1);
        UnequipGoodsItemEverywhere(
            MIMIC_VEIL);

        ClearRoleMagic();

        const bool runeOk =
            EquipGoodsByItemId(
                SLOT_GREAT_RUNE,
                GODRICK_GREAT_RUNE,
                true);

        if (!runeOk)
        {
            Log("[ROLE] Innocent loadout not ready; will retry.");
            return false;
        }

        Log("[ROLE] Innocent loadout applied.");
        return true;
    }

    bool SetLocalRole(Role role)
    {
        const uintptr_t player =
            GetLocalPlayerIns();

        if (!player)
        {
            Log("[ROLE] Local player unavailable.");
            return false;
        }

        const Role oldRole =
            GetCurrentRole();

        // --------------------------------------------------------
        // New Sheriff starts with a fresh punishment state.
        // --------------------------------------------------------

        if (role == Role::Sheriff &&
            oldRole != Role::Sheriff)
        {
            sheriffWrongKills = 0;

            for (int i = 0; i < MAX_MM_PLAYERS; ++i)
            {
                sheriffTrackedAlive[i] = false;
                sheriffTrackedInitialized[i] = false;

                sheriffTargetWasInnocent[i] = false;
                sheriffTargetHitBySheriff[i] = false;
            }

            Log(
                "[SHERIFF] New Sheriff -> "
                "wrong-kill tracking reset.");
        }

        // --------------------------------------------------------
        // Leaving Sheriff: remove Sheriff-only attack tagging.
        // --------------------------------------------------------

        if (oldRole == Role::Sheriff &&
            role != Role::Sheriff)
        {
            RemoveSpEffectFromChrIns(
                player,
                SPEFFECT_SHERIFF_ATTACK_TAGGER);
        }

        // --------------------------------------------------------
        // Leaving Innocent while carrying a talisman.
        // --------------------------------------------------------

        if (oldRole == Role::Innocent &&
            role != Role::Innocent &&
            HasLocalTalisman())
        {
            // Prevent Veil's normal lifecycle tracker from
            // interpreting forced removal as consumption.
            concealingTracking = false;
            mmTalismanWasEquipped = false;

            RemoveLocalTalismanForRoleChange();
        }

        // Innocent can NEVER retain Mark of Greed.
        if (role == Role::Innocent)
        {
            ClearGreed();
        }

        // Clear role identity.
        RemoveSpEffectFromChrIns(
            player,
            SPEFFECT_MURDERER);

        RemoveSpEffectFromChrIns(
            player,
            SPEFFECT_SHERIFF);

        RemoveSpEffectFromChrIns(
            player,
            SPEFFECT_INNOCENT);

        bool roleEffectOk = false;

        switch (role)
        {
        case Role::Murderer:
            Log("[ROLE] Setting MURDERER.");

            roleEffectOk =
                ApplySpEffectToChrIns(
                    player,
                    SPEFFECT_MURDERER);

            break;

        case Role::Sheriff:
            Log("[ROLE] Setting SHERIFF.");

            roleEffectOk =
                ApplySpEffectToChrIns(
                    player,
                    SPEFFECT_SHERIFF);

            break;

        case Role::Innocent:
            Log("[ROLE] Setting INNOCENT.");

            roleEffectOk =
                ApplySpEffectToChrIns(
                    player,
                    SPEFFECT_INNOCENT);

            break;

        case Role::None:
            Log("[ROLE] Clearing role.");
            roleEffectOk = true;
            break;
        }

        if (!roleEffectOk)
            return false;

        return true;
    }

    bool ExecuteCommand(MMCommand command)
    {
        switch (command)
        {
        case MMCommand::SetInnocent:
            Log("[COMMAND] SetInnocent");
            return SetLocalRole(Role::Innocent);

        case MMCommand::SetSheriff:
            Log("[COMMAND] SetSheriff");
            return SetLocalRole(Role::Sheriff);

        case MMCommand::SetMurderer:
            Log("[COMMAND] SetMurderer");
            return SetLocalRole(Role::Murderer);

        }

        return false;
    }

    // Parses one line received over MMTcpClient from the local Electron app (which in
    // turn relays it from the VPS) and dispatches it the same way a hotkey does below
    // in MainThread. Wire format: "cmd=<name> [key=value ...]" - matches the plain
    // key=value line style the rest of this project's DLL<->app protocol already uses
    // (see dll-protocol.ts on the Electron side). Params past cmd= are optional and
    // command-specific (e.g. StartGame's role=).
    void HandleNetworkLine(const std::string& line,MMTcpClient& mmTcpClient)
    {
        constexpr const char* kCmdPrefix = "cmd=";
        constexpr size_t kCmdPrefixLen = 4;

        std::istringstream tokens(line);
        std::string firstToken;
        tokens >> firstToken;

        if (firstToken.compare(0, kCmdPrefixLen, kCmdPrefix) != 0)
        {
            Log("[NET] Ignoring malformed line: " + line);
            return;
        }

        const std::string commandName = firstToken.substr(kCmdPrefixLen);

        std::map<std::string, std::string> params;
        std::string paramToken;
        while (tokens >> paramToken)
        {
            const size_t eq = paramToken.find('=');
            if (eq != std::string::npos)
            {
                params[paramToken.substr(0, eq)] = paramToken.substr(eq + 1);
            }
        }

        if (commandName == "SetMurderer")
        {
            ExecuteCommand(MMCommand::SetMurderer);
        }
        else if (commandName == "SetSheriff")
        {
            ExecuteCommand(MMCommand::SetSheriff);
        }
        else if (commandName == "SetInnocent")
        {
            ExecuteCommand(MMCommand::SetInnocent);
        }
        else if (commandName == "StartGame")
        {
            const auto roundParam =
                params.find("round");

            const auto spawnParam =
                params.find("spawn");

            if (roundParam == params.end() ||
                roundParam->second.empty() ||
                spawnParam == params.end())
            {
                Log(
                    "[NET] StartGame missing round/spawn params: " +
                    line);

                return;
            }

            int spawnIndex = 0;

            try
            {
                spawnIndex =
                    std::stoi(spawnParam->second);
            }
            catch (...)
            {
                Log(
                    "[NET] StartGame invalid spawn param: " +
                    line);

                return;
            }

            if (spawnIndex < 1 ||
                spawnIndex > 11)
            {
                Log(
                    "[NET] StartGame spawn must be 1-11: " +
                    std::to_string(spawnIndex));

                return;
            }

            // Ignore a duplicate delivery for the current round.
            if (g_activeRoundId == roundParam->second &&
                g_waitingForVolcanoLoad)
            {
                Log(
                    "[NET] Duplicate StartGame ignored for round=" +
                    roundParam->second);

                return;
            }

            // A player who returned naturally through death may have an
            // unconsumed return request from an older round. It must be
            // cleared before requesting the new Volcano spawn.
            if (!SetEventFlag(
                FLAG_RETURN_TO_HUB_REQUEST,
                false))
            {
                Log(
                    "[ERROR] Could not clear stale "
                    "Roundtable return flag.");

                return;
            }

            // Remove the arrival signal left by the previous round.
            if (!SetEventFlag(
                FLAG_VOLCANO_LOADED,
                false))
            {
                Log(
                    "[ERROR] Could not clear Volcano loaded flag.");

                return;
            }

            g_activeRoundId =
                roundParam->second;

            g_waitingForVolcanoLoad = true;
            g_volcanoLoadReported = false;
            g_roundActive = false;
            g_playerDeathReported = false;

            // The server selects this index. Do not call
            // RequestRandomSpawn() here.
            if (!RequestSpawn(
                spawnIndex))
            {
                Log(
                    "[ERROR] StartGame failed to request spawn " +
                    std::to_string(spawnIndex));

                g_waitingForVolcanoLoad = false;
                return;
            }

            Log(
                "[ROUND] StartGame round=" +
                g_activeRoundId +
                " spawn=" +
                std::to_string(spawnIndex));
        }
        else if (commandName == "SetRole")
        {
            const auto roundParam =
                params.find("round");

            const auto roleParam =
                params.find("role");

            if (roundParam == params.end() ||
                roleParam == params.end())
            {
                Log(
                    "[NET] SetRole missing round/role params: " +
                    line);

                return;
            }

            if (roundParam->second !=
                g_activeRoundId)
            {
                Log(
                    "[NET] Ignoring SetRole for stale round=" +
                    roundParam->second);

                return;
            }

            Role role = Role::None;

            if (roleParam->second == "Murderer")
            {
                role = Role::Murderer;
            }
            else if (roleParam->second == "Sheriff")
            {
                role = Role::Sheriff;
            }
            else if (roleParam->second == "Innocent")
            {
                role = Role::Innocent;
            }

            if (role == Role::None)
            {
                Log(
                    "[NET] SetRole invalid role: " +
                    roleParam->second);

                return;
            }

            if (!SetLocalRole(role))
            {
                Log(
                    "[ERROR] SetRole failed for round=" +
                    g_activeRoundId);

                return;
            }

            Log(
                "[ROLE] Server role applied: " +
                roleParam->second);
        }
        else if (commandName == "Commence")
        {
            const auto roundParam =
                params.find("round");

            if (roundParam == params.end() ||
                roundParam->second !=
                    g_activeRoundId)
            {
                Log(
                    "[NET] Ignoring Commence with invalid/stale round.");

                return;
            }

            const uintptr_t player =
                GetLocalPlayerIns();

            if (player)
            {
                RemoveSpEffectFromChrIns(
                    player,
                    SPEFFECT_ROUND_START_LOCK);

                Log(
                    "[ROUND] Commence -> movement unlocked.");
            }

            if (SetEventFlag(
                FLAG_COMMENCE_REQUEST,
                true))
            {
                g_roundActive = true;
                g_playerDeathReported = false;

                Log(
                    "[ROUND] Commence requested for round=" +
                    g_activeRoundId);
            }
            else
            {
                Log(
                    "[ERROR] Failed to request Commence banner.");
            }
        }
        else if (commandName == "RoundResult")
        {
            const auto roundParam = params.find("round");
            const auto resultParam = params.find("result");

            if (roundParam == params.end() ||
                resultParam == params.end() ||
                roundParam->second != g_activeRoundId)
            {
                Log("[NET] Ignoring RoundResult with invalid/stale round.");
                return;
            }

            uint32_t resultFlag = 0;

            if (resultParam->second == "InnocentWin")
                resultFlag = FLAG_VICTORY_REQUEST;
            else if (resultParam->second == "MurdererWin")
                resultFlag = FLAG_DEFEAT_REQUEST;
            else if (resultParam->second == "Stalemate")
                resultFlag = FLAG_STALEMATE_REQUEST;

            if (resultFlag == 0)
            {
                Log("[NET] RoundResult has invalid result: " + resultParam->second);
                return;
            }

            g_roundActive = false;

            if (SetEventFlag(resultFlag, true))
            {
                Log("[ROUND] Result banner requested: " + resultParam->second);
            }
            else
            {
                Log("[ERROR] Failed to request result banner.");
            }
        }
        else if (commandName == "EndGame")
        {
            g_roundActive = false;
            g_playerDeathReported = false;
            g_waitingForVolcanoLoad = false;
            g_volcanoLoadReported = false;

            const uintptr_t player =
                GetLocalPlayerIns();

            if (player)
            {
                RemoveSpEffectFromChrIns(
                    player,
                    SPEFFECT_ROUND_START_LOCK);

                Log(
                    "[ROUND] EndGame -> movement unlocked.");
            }

            SetLocalRole(Role::None);
            ClearRoleMagic();
            RequestRoundCleanup();

            // Same flag as the F9 test hotkey - see FLAG_RETURN_TO_HUB_REQUEST below.
            bool shouldReturnToHub = true;

            const auto returnParam =
                params.find("returnToHub");

            if (returnParam != params.end() &&
                returnParam->second == "0")
            {
                shouldReturnToHub = false;
            }

            if (shouldReturnToHub)
            {
                if (SetEventFlag(
                    FLAG_RETURN_TO_HUB_REQUEST,
                    true))
                {
                    Log(
                        "[NET] EndGame -> "
                        "Return to Roundtable requested.");
                }
                else
                {
                    Log(
                        "[ERROR] EndGame -> Failed to request "
                        "return to Roundtable.");
                }
            }
            else
            {
                // The player already returned naturally through death.
                // Clear any older request instead of creating another one.
                SetEventFlag(
                    FLAG_RETURN_TO_HUB_REQUEST,
                    false);

                Log(
                    "[ROUND] EndGame cleanup completed; "
                    "player already returned through death.");
            }
        }
        else
        {
            Log("[NET] Unknown command: " + commandName);
        }
    }

    bool UnequipGoodsSlot(int32_t slot)
    {
        if (!g_equipGoodsAddress)
            return false;

        const uintptr_t player =
            GetPlayerGameData();

        if (!player)
            return false;

        const uintptr_t equipGameData =
            player + EQUIP_GAME_DATA_OFFSET;

        uint32_t emptyItem = 0xFFFFFFFF;

        const int32_t goodsSlot =
            slot - 22;

        return CallEquipGoodsRaw(
            equipGameData,
            goodsSlot,
            reinterpret_cast<uintptr_t>(&emptyItem),
            static_cast<int32_t>(0xFFFFFFFF),
            g_equipGoodsAddress);
    }

    bool GetEquippedItemInSlot(
        int32_t slot,
        uint32_t& itemId)
    {
        itemId = 0xFFFFFFFF;

        if (slot < 17 || slot > 38)
            return false;

        const uintptr_t player =
            GetPlayerGameData();

        if (!player)
            return false;

        // ChrAsm2:
        // slot 17 (Accessory 1) = +0x63C
        // each subsequent slot = +4 bytes.
        const uintptr_t offset =
            0x63C +
            static_cast<uintptr_t>(
                slot - 17) * 4;

        itemId =
            *reinterpret_cast<uint32_t*>(
                player + offset);

        return true;
    }

    bool UnequipGoodsItemEverywhere(
        uint32_t itemId)
    {
        bool found = false;

        // Quick Items 1-10 + Pouch 1-6.
        for (int32_t slot = 22;
            slot <= 37;
            ++slot)
        {
            uint32_t equipped = 0xFFFFFFFF;

            if (!GetEquippedItemInSlot(
                slot,
                equipped))
            {
                continue;
            }

            if (equipped == itemId)
            {
                UnequipGoodsSlot(slot);
                found = true;

                Log(
                    "[EQUIP] Removed goods item " +
                    Hex(itemId) +
                    " from slot " +
                    std::to_string(slot));
            }
        }

        return found;
    }

    bool SetSheriffMagic()
    {
        const uintptr_t player =
            GetPlayerGameData();

        if (!player)
            return false;

        const uintptr_t magic =
            *reinterpret_cast<uintptr_t*>(
                player + MAGIC_DATA_PTR_OFFSET);

        if (!magic)
            return false;

        // Clear all 14 memory slots first.
        for (int i = 0; i < 14; ++i)
        {
            *reinterpret_cast<int32_t*>(
                magic + 0x10 + i * 0x08) = -1;
        }

        // Slot 1
        *reinterpret_cast<int32_t*>(
            magic + 0x10) =
            SWIFT_GLINTSTONE_SHARD;

        // Slot 2
        *reinterpret_cast<int32_t*>(
            magic + 0x18) =
            CRYSTAL_BURST;

        // Selected spell = first memory slot.
        *reinterpret_cast<int32_t*>(
            magic + 0x80) = 0;

        Log("[ACTION] Sheriff magic configured.");

        return true;
    }

    bool ClearRoleMagic()
    {
        const uintptr_t player =
            GetPlayerGameData();

        if (!player)
            return false;

        const uintptr_t magic =
            *reinterpret_cast<uintptr_t*>(
                player + MAGIC_DATA_PTR_OFFSET);

        if (!magic)
            return false;

        for (int i = 0; i < 14; ++i)
        {
            *reinterpret_cast<int32_t*>(
                magic + 0x10 + i * 0x08) = -1;
        }

        *reinterpret_cast<int32_t*>(
            magic + 0x80) = -1;

        Log("[ACTION] Role magic cleared.");

        return true;
    }

    bool RunProfileBootstrap()
    {
        Log("[BOOTSTRAP] Beginning profile initialization.");

        bool success = true;

        if (!GiveBootstrapItems())
        {
            Log("[ERROR] GiveBootstrapItems failed.");
            success = false;
        }

        if (!UnlockAllGestures())
        {
            Log("[ERROR] UnlockAllGestures failed.");
            success = false;
        }

        if (success)
        {
            Log("[BOOTSTRAP] Profile initialization successful.");
        }

        return success;
    }

    bool UnlockAllGestures()
    {
        const uintptr_t player =
            GetPlayerGameData();

        if (!player)
            return false;

        const uintptr_t gestureData =
            *reinterpret_cast<uintptr_t*>(
                player + 0x8D8);

        if (!gestureData)
            return false;

        static const int32_t gestures[] =
        {
            1, 3, 5, 7, 9, 11, 13, 15, 17, 19,
            21, 41, 43, 45, 47, 49, 51, 61,
            81, 83, 101, 103, 105, 107, 109,
            121, 141, 143, 145, 147, 161, 181,
            183, 185, 187, 189, 191, 195, 197,
            201, 203, 205, 207, 209, 211, 213
        };

        const size_t gestureCount =
            sizeof(gestures) / sizeof(gestures[0]);

        for (size_t i = 0; i < gestureCount; ++i)
        {
            *reinterpret_cast<int32_t*>(
                gestureData + 4 + (i + 1) * 4) =
                gestures[i];
        }

        Log("[BOOTSTRAP] Gestures unlocked silently.");

        return true;
    }

    // --------------------------------------------------------
    // Hotkey thread
    // --------------------------------------------------------

    DWORD WINAPI MainThread(LPVOID)
    {
        Log("");
        Log("===============================");
        Log("MurderMystery.dll v0.1 loaded");
        Log("===============================");

        bool bootstrapHandled = false;
        Role lastAppliedLoadoutRole = Role::None;

        // Give Elden Ring time to finish initial setup.
        Sleep(3000);

        bool lastAliveState = true;
        bool aliveStateInitialized = false;
        int pearldrakeLastHp = -1;
        bool pearldrakeTracking = false;
        bool pearldrakeBreakRequested = false;
        bool alloyedHitPulseHandled = false;

        // Connects out to the local Electron app's command listener (see
        // dllmain.cpp/main.ts on the Electron side) - same lazy-connect-with-cooldown
        // shape as ProximityChatMod's TcpTestClient, just read-oriented instead
        // of write-oriented since this channel carries inbound commands.
        MMTcpClient mmTcpClient("127.0.0.1", 9192);

        while (g_running)
        {
            std::vector<std::string> mmLines;

            mmTcpClient.poll(mmLines);

            for (const std::string& mmLine : mmLines)
            {
                HandleNetworkLine(mmLine,mmTcpClient);
            }

            if (!g_gameDataManAddress)
            {
                ResolveGameDataMan();

                if (!g_gameDataManAddress)
                {
                    Sleep(2000);
                    continue;
                }
            }

            if (!g_worldChrManAddress)
            {
                ResolveWorldChrMan();

                if (!g_worldChrManAddress)
                {
                    Sleep(2000);
                    continue;
                }
            }

            if (!g_equipGearAddress)
            {
                ResolveEquipGear();

                if (!g_equipGearAddress)
                {
                    Sleep(2000);
                    continue;
                }
            }

            if (!g_equipGoodsAddress)
            {
                ResolveEquipGoods();

                if (!g_equipGoodsAddress)
                {
                    Sleep(2000);
                    continue;
                }
            }

            if (!g_mapItemManAddress)
            {
                ResolveMapItemMan();

                if (!g_mapItemManAddress)
                {
                    Sleep(2000);
                    continue;
                }
            }

            if (!g_itemGiveAddress)
            {
                ResolveItemGive();

                if (!g_itemGiveAddress)
                {
                    Sleep(2000);
                    continue;
                }
            }

            if (!g_applySpEffectAddress)
            {
                ResolveApplySpEffect();

                if (!g_applySpEffectAddress)
                {
                    Sleep(2000);
                    continue;
                }
            }

            if (!g_removeSpEffectAddress)
            {
                ResolveRemoveSpEffect();

                if (!g_removeSpEffectAddress)
                {
                    Sleep(2000);
                    continue;
                }
            }

            if (!g_entityToChrInsAddress)
            {
                ResolveEntityToChrIns();

                if (!g_entityToChrInsAddress)
                {
                    Sleep(2000);
                    continue;
                }
            }

            if (!g_eventFlagManAddress)
            {
                ResolveEventFlagMan();

                if (!g_eventFlagManAddress)
                {
                    Sleep(2000);
                    continue;
                }
            }

            if (!g_itemPopupAddress)
            {
                ResolveItemPopup();

                if (!g_itemPopupAddress)
                {
                    Sleep(2000);
                    continue;
                }
            }

            if (!g_removeItemAddress)
            {
                ResolveRemoveItem();

                if (!g_removeItemAddress)
                {
                    Sleep(2000);
                    continue;
                }
            }

            if (!g_getInventoryIdNative)
            {
                ResolveNativeGetInventoryId();

                if (!g_getInventoryIdNative)
                {
                    Sleep(2000);
                    continue;
                }
            }

            if (!g_equipItemNative)
            {
                ResolveNativeEquipItem();

                if (!g_equipItemNative)
                {
                    Sleep(2000);
                    continue;
                }
            }

            // The m11_10_00_00 warp event clears its spawn flag the instant it
            // fires WarpPlayer (see m11_10_00_00.emevd), so watching that flag
            // go back OFF is a reliable "the player has landed at Volcano
            // Manor" signal - there's no PlayerInMap-equivalent memory read on
            // the C++ side to check this directly.
            if (g_waitingForVolcanoLoad &&
                !g_volcanoLoadReported)
            {
                bool volcanoLoaded = false;

                if (GetEventFlag(
                    FLAG_VOLCANO_LOADED,
                    volcanoLoaded) &&
                    volcanoLoaded)
                {
                    const uintptr_t player =
                        GetLocalPlayerIns();

                    if (player &&
                        !HasSpEffect(
                            SPEFFECT_ROUND_START_LOCK))
                    {
                        ApplySpEffectToChrIns(
                            player,
                            SPEFFECT_ROUND_START_LOCK);

                        Log(
                            "[ROUND] Volcano loaded -> "
                            "movement locked while waiting for Commence.");
                    }

                    const std::string eventLine =
                        "event=PlayerLoaded round=" +
                        g_activeRoundId;

                    if (mmTcpClient.sendLine(
                        eventLine))
                    {
                        Log(
                            "[ROUND] Reported PlayerLoaded for round=" +
                            g_activeRoundId);

                        g_volcanoLoadReported = true;
                        g_waitingForVolcanoLoad = false;
                    }
                }
            }

            const bool bootstrapRequested =
                HasSpEffect(SPEFFECT_BOOTSTRAP);

            if (bootstrapRequested && !bootstrapHandled)
            {
                Log("[BOOTSTRAP] Request detected.");

                if (RunProfileBootstrap())
                {
                    Log("[BOOTSTRAP] Completed successfully.");
                    bootstrapHandled = true;
                }
                else
                {
                    Log("[BOOTSTRAP] Failed; will retry.");
                }
            }

            if (!bootstrapRequested)
            {
                bootstrapHandled = false;
            }

            // &1 catches only the key-down transition,
            // rather than executing every frame it is held.

            const Role currentRole =
                GetCurrentRole();

            if (currentRole != lastAppliedLoadoutRole)
            {
                bool applied = false;

                switch (currentRole)
                {
                case Role::Murderer:
                    applied =
                        ApplyMurdererLoadout();
                    break;

                case Role::Sheriff:
                    applied =
                        ApplySheriffLoadout();
                    break;

                case Role::Innocent:
                    applied =
                        ApplyInnocentLoadout();
                    break;

                case Role::None:
                    applied = true;
                    break;
                }

                // IMPORTANT:
                // If the ItemLot hasn't granted the role item yet,
                // applied remains false and this tries again next loop.
                if (applied)
                {
                    lastAppliedLoadoutRole =
                        currentRole;

                    Log(
                        "[ROLE] Loadout successfully applied.");
                }
            }

            if (nullTalismanState ==
                NullTalismanState::WaitingForEquip)
            {
                uint32_t equipped = 0xFFFFFFFF;

                if (GetEquippedItemInSlot(
                    SLOT_TALISMAN_1,
                    equipped))
                {
                    if (equipped ==
                        NULL_MM_TALISMAN)
                    {
                        Log(
                            "[NULL] SUCCESS: slot 17 now contains 6700.");

                        // Now that 6700 is physically equipped,
                        // remove the consumed real MM talisman from inventory.
                        RemoveLocalTalismanInventory();

                        // Start the 1000 ms visible test duration NOW.
                        nullTalismanEquipTime =
                            GetTickCount64();

                        nullTalismanState =
                            NullTalismanState::Equipped;
                    }
                    else
                    {
                        // Native equip is asynchronous / can fail to commit
                        // on the first request, so keep requesting 6700
                        // until slot 17 actually changes.
                        EquipItemNativeByItemId(
                            SLOT_TALISMAN_1,
                            static_cast<int32_t>(
                                NULL_MM_TALISMAN));
                    }
                }
            }
            else if (
                nullTalismanState ==
                NullTalismanState::Equipped)
            {
                const ULONGLONG elapsed =
                    GetTickCount64() -
                    nullTalismanEquipTime;

                if (elapsed >=
                    NULL_TALISMAN_TEST_DURATION_MS)
                {
                    Log(
                        "[NULL] 6700 confirmed equipped for "
                        "1000 ms -> cleaning up.");

                    uint32_t equipped = 0xFFFFFFFF;

                    if (GetEquippedItemInSlot(
                        SLOT_TALISMAN_1,
                        equipped) &&
                        equipped == NULL_MM_TALISMAN)
                    {
                        UnequipGearSlot(
                            SLOT_TALISMAN_1);
                    }

                    RemoveAllInventoryCopies(
                        static_cast<int32_t>(
                            NULL_MM_TALISMAN));

                    nullTalismanState =
                        NullTalismanState::Idle;

                    concealingBreakInProgress = false;

                    Log(
                        "[NULL] 6700 cleanup complete.");
                }
            }

            if (HasSpEffect(
                SPEFFECT_TALISMAN_BREAK_REQUEST))
            {
                const uintptr_t player =
                    GetLocalPlayerIns();

                if (player)
                {
                    const bool concealingEarlyCancel =
                        concealingTracking &&
                        HasSpEffect(
                            SPEFFECT_CONCEALING_VEIL) &&
                        GetItemIndex(
                            static_cast<int32_t>(
                                CONCEALING_VEIL)) >= 0;

                    if (concealingEarlyCancel)
                    {
                        Log(
                            "[CONCEALING] Early uncrouch -> "
                            "single shatter + 6700 replacement.");

                        concealingBreakInProgress = true;

                        ApplySpEffectToChrIns(
                            player,
                            SPEFFECT_TALISMAN_SHATTER_REQUEST);

                        if (!FlushMMTalismanWithNull())
                        {
                            Log(
                                "[ERROR] Concealing early-cancel "
                                "6700 replacement failed.");

                            UnequipMMTalismanEverywhere();
                            RemoveLocalTalismanInventory();

                            RemoveSpEffectFromChrIns(
                                player,
                                SPEFFECT_CONCEALING_VEIL);
                        }

                        RemoveSpEffectFromChrIns(
                            player,
                            SPEFFECT_EQUIP_CONCEALING);

                        concealingTracking = false;
                        mmTalismanWasEquipped = false;

                        Log(
                            "[CONCEALING] Early uncrouch handled.");
                    }
                    else
                    {
                        BreakLocalTalisman();

                        mmTalismanWasEquipped = false;
                    }

                    // Consume 500051 AFTER the talisman transaction.
                    RemoveSpEffectFromChrIns(
                        player,
                        SPEFFECT_TALISMAN_BREAK_REQUEST);

                    Log(
                        "[TALISMAN] Break request handled by DLL.");
                }
            }

            const bool hasConcealing =
                HasSpEffect(
                    SPEFFECT_CONCEALING_VEIL);

            if (!concealingTracking)
            {
                if (hasConcealing &&
                    !concealingBreakInProgress)
                {
                    concealingTracking = true;

                    Log(
                        "[CONCEALING] Veil armed.");
                }
            }
            else
            {
                if (!hasConcealing)
                {
                    concealingTracking = false;

                    Log(
                        "[CONCEALING] Veil effect ended.");
                }
            }

            if (HasSpEffect(
                SPEFFECT_SHOW_GREED_POPUP_REQUEST))
            {
                const uintptr_t player =
                    GetLocalPlayerIns();

                if (player)
                {
                    ShowItemPopup(
                        MARK_OF_GREED_ITEM,
                        1,
                        0);

                    RemoveSpEffectFromChrIns(
                        player,
                        SPEFFECT_SHOW_GREED_POPUP_REQUEST);

                    Log(
                        "[GREED] Acquisition popup displayed.");
                }
            }

            bool alloyedHitPulse = false;

            if (GetEventFlag(
                16009886,
                alloyedHitPulse))
            {
                if (alloyedHitPulse)
                {
                    if (!alloyedHitPulseHandled &&
                        HasSpEffect(
                            SPEFFECT_ALLOYED_FIST_ARMED) &&
                        GetItemIndex(
                            static_cast<int32_t>(
                                ALLOYED_FIST)) >= 0)
                    {
                        Log(
                            "[ALLOYED FIST] Network hit confirmed.");

                        // Give the victim's damage/HKS reaction time to finish
                        // before destroying the attacker's armed talisman state.
                        Sleep(150);

                        ConsumeAlloyedFist();

                        mmTalismanWasEquipped = false;
                    }

                    alloyedHitPulseHandled = true;
                }
                else
                {
                    alloyedHitPulseHandled = false;
                }
            }

            if (HasSpEffect(
                SPEFFECT_SHERIFF_PICKUP_REQUEST))
            {
                const uintptr_t player =
                    GetLocalPlayerIns();

                if (player)
                {
                    RemoveSpEffectFromChrIns(
                        player,
                        SPEFFECT_SHERIFF_PICKUP_REQUEST);

                    if (GetCurrentRole() ==
                        Role::Innocent)
                    {
                        Log(
                            "[SHERIFF] Innocent claimed "
                            "dropped Sheriff kit.");

                        SetLocalRole(
                            Role::Sheriff);
                    }
                }
            }

            if (GetAsyncKeyState(VK_F1) & 1)
            {
                SetEventFlag(
                    16009984,
                    true);

                Log(
                    "[TEST] F1 -> Sheriff kit pickup spawned.");
            }

            if (GetAsyncKeyState(VK_F2) & 1)
            {
                ExecuteCommand(MMCommand::SetInnocent);
            }

            if (GetAsyncKeyState(VK_F3) & 1)
            {
                ExecuteCommand(MMCommand::SetSheriff);
            }

            if (GetAsyncKeyState(VK_F4) & 1)
            {
                ExecuteCommand(MMCommand::SetMurderer);
            }

            if (GetAsyncKeyState(VK_F5) & 1)
            {
                if (SetEventFlag(
                    FLAG_VICTORY_REQUEST,
                    true))
                {
                    Log(
                        "[BANNER TEST] F5 -> Victory requested.");
                }
                else
                {
                    Log(
                        "[ERROR] Failed to request Victory banner.");
                }
            }

            if (GetAsyncKeyState(VK_F6) & 1)
            {
                if (SetEventFlag(
                    FLAG_DEFEAT_REQUEST,
                    true))
                {
                    Log(
                        "[BANNER TEST] F6 -> Defeat requested.");
                }
                else
                {
                    Log(
                        "[ERROR] Failed to request Defeat banner.");
                }
            }

            if (GetAsyncKeyState(VK_F7) & 1)
            {
                if (SetEventFlag(
                    FLAG_STALEMATE_REQUEST,
                    true))
                {
                    Log(
                        "[BANNER TEST] F7 -> Stalemate requested.");
                }
                else
                {
                    Log(
                        "[ERROR] Failed to request Stalemate banner.");
                }
            }


            // F8 = COMMENCE
            if (GetAsyncKeyState(VK_F8) & 1)
            {
                if (SetEventFlag(
                    FLAG_COMMENCE_REQUEST,
                    true))
                {
                    Log(
                        "[BANNER TEST] F8 -> Start requested.");
                }
                else
                {
                    Log(
                        "[ERROR] Failed to request Start banner.");
                }
            }


            // F9 = return to Roundtable
            if (GetAsyncKeyState(VK_F9) & 1)
            {
                const uintptr_t player =
                    GetLocalPlayerIns();

                if (player)
                {
                    RemoveSpEffectFromChrIns(
                        player,
                        SPEFFECT_ROUND_START_LOCK);
                }

                if (SetEventFlag(
                    FLAG_RETURN_TO_HUB_REQUEST,
                    true))
                {
                    Log(
                        "[ROUND] F9 -> Return to Roundtable requested.");
                }
                else
                {
                    Log(
                        "[ERROR] Failed to request return to Roundtable.");
                }
            }

            if (GetAsyncKeyState(VK_F10) & 1)
            {
                RequestRandomSpawn();
            }

            if (GetAsyncKeyState(VK_F11) & 1)
            {
                RequestRandomItemSpawn();
            }

            if (GetAsyncKeyState(VK_F12) & 1)
            {
                Log("[TEST] F12 -> Round cleanup.");

                RequestRoundCleanup();
            }

            if (currentRole == Role::Innocent)
            {
                const uintptr_t player =
                    GetLocalPlayerIns();

                if (player)
                {
                    if (HasSpEffect(SPEFFECT_EQUIP_CONCEALING))
                    {
                        if (EquipItemNativeByItemId(
                            SLOT_TALISMAN_1,
                            static_cast<int32_t>(CONCEALING_VEIL)))
                        {
                            RemoveSpEffectFromChrIns(
                                player,
                                SPEFFECT_EQUIP_CONCEALING);

                            Log(
                                "[TALISMAN] Concealing Veil "
                                "native auto-equipped to slot 1.");
                        }
                    }

                    if (HasSpEffect(SPEFFECT_EQUIP_PEARLDRAKE))
                    {
                        if (EquipItemNativeByItemId(
                            SLOT_TALISMAN_1,
                            static_cast<int32_t>(PEARLDRAKE_TALISMAN)))
                        {
                            RemoveSpEffectFromChrIns(
                                player,
                                SPEFFECT_EQUIP_PEARLDRAKE);

                            Log(
                                "[TALISMAN] Pearldrake "
                                "native auto-equipped to slot 1.");
                        }
                    }

                    if (HasSpEffect(SPEFFECT_EQUIP_CRUCIBLE_FEATHER))
                    {
                        if (EquipItemNativeByItemId(
                            SLOT_TALISMAN_1,
                            static_cast<int32_t>(CRUCIBLE_FEATHER_TALISMAN)))
                        {
                            RemoveSpEffectFromChrIns(
                                player,
                                SPEFFECT_EQUIP_CRUCIBLE_FEATHER);

                            Log(
                                "[TALISMAN] Crucible Feather "
                                "native auto-equipped to slot 1.");
                        }
                    }

                    if (HasSpEffect(SPEFFECT_EQUIP_ALLOYED_FIST))
                    {
                        if (EquipItemNativeByItemId(
                            SLOT_TALISMAN_1,
                            static_cast<int32_t>(ALLOYED_FIST)))
                        {
                            RemoveSpEffectFromChrIns(
                                player,
                                SPEFFECT_EQUIP_ALLOYED_FIST);

                            Log(
                                "[TALISMAN] Alloyed Fist "
                                "native auto-equipped to slot 1.");
                        }
                    }
                }
            }

            if (currentRole == Role::Innocent)
            {
                const bool ownsMMTalisman =
                    HasLocalTalisman();

                const bool equippedMMTalisman =
                    IsMMTalismanEquipped();

                const bool autoEquipPending =
                    HasSpEffect(SPEFFECT_EQUIP_CONCEALING) ||
                    HasSpEffect(SPEFFECT_EQUIP_PEARLDRAKE) ||
                    HasSpEffect(SPEFFECT_EQUIP_CRUCIBLE_FEATHER) ||
                    HasSpEffect(SPEFFECT_EQUIP_ALLOYED_FIST);

                // --------------------------------------------------------
                // IMPORTANT:
                // A real talisman -> 6700 replacement is NOT a manual
                // unequip. Ignore equipment changes for the entire null
                // replacement transaction.
                // --------------------------------------------------------

                const bool nullReplacementInProgress =
                    nullTalismanState !=
                    NullTalismanState::Idle;

                if (nullReplacementInProgress ||
                    concealingBreakInProgress)
                {
                    // Do NOT let the still-equipped 6010 re-arm
                    // mmTalismanWasEquipped while we wait for 6700.
                    mmTalismanWasEquipped = false;
                }
                else if (!ownsMMTalisman)
                {
                    mmTalismanWasEquipped = false;
                }
                else if (equippedMMTalisman)
                {
                    mmTalismanWasEquipped = true;
                }
                else if (
                    mmTalismanWasEquipped &&
                    !autoEquipPending)
                {
                    Log(
                        "[TALISMAN] Manual unequip detected.");

                    const uintptr_t player =
                        GetLocalPlayerIns();

                    if (player)
                    {
                        RemoveSpEffectFromChrIns(
                            player,
                            SPEFFECT_CONCEALING_VEIL);

                        RemoveSpEffectFromChrIns(
                            player,
                            SPEFFECT_PEARLDRAKE);

                        RemoveSpEffectFromChrIns(
                            player,
                            SPEFFECT_CRUCIBLE_FEATHER);

                        RemoveSpEffectFromChrIns(
                            player,
                            SPEFFECT_CRUCIBLE_FEATHER_CYCLE);

                        RemoveSpEffectFromChrIns(
                            player,
                            SPEFFECT_ALLOYED_FIST);

                        RemoveSpEffectFromChrIns(
                            player,
                            SPEFFECT_ALLOYED_FIST_ARMED);

                        RemoveSpEffectFromChrIns(
                            player,
                            SPEFFECT_ALLOYED_FIST_CONSUME);

                        RemoveSpEffectFromChrIns(
                            player,
                            SPEFFECT_EQUIP_ALLOYED_FIST);

                        ApplySpEffectToChrIns(
                            player,
                            SPEFFECT_TALISMAN_SHATTER_REQUEST);
                    }

                    RemoveLocalTalismanInventory();

                    mmTalismanWasEquipped = false;

                    Log(
                        "[TALISMAN] Manual unequip consumed normally.");
                }
            }
            else
            {
                mmTalismanWasEquipped = false;
            }

            PlayerState localPlayer{};

            if (TryReadPlayerSlot(0, localPlayer))
            {
                if (g_roundActive &&
                    !g_playerDeathReported &&
                    !localPlayer.alive)
                {
                    const std::string eventLine =
                        "event=PlayerDied round=" +
                        g_activeRoundId;

                    if (mmTcpClient.sendLine(eventLine))
                    {
                        g_playerDeathReported = true;
                        Log(
                            "[ROUND] Reported PlayerDied for round=" +
                            g_activeRoundId);
                    }
                }

                if (!aliveStateInitialized)
                {
                    lastAliveState = localPlayer.alive;
                    aliveStateInitialized = true;

                    Log(
                        std::string("[PLAYER] Initial state: ") +
                        (localPlayer.alive ? "ALIVE" : "DEAD"));
                }
                else if (localPlayer.alive != lastAliveState)
                {
                    if (localPlayer.alive)
                    {
                        Log("[PLAYER] Local player became ALIVE.");
                    }
                    else
                    {
                        Log("[PLAYER] Local player DIED.");

                        if (currentRole == Role::Sheriff)
                        {
                            Log(
                                "[SHERIFF] Sheriff died -> "
                                "requesting dropped kit.");

                            SetEventFlag(
                                16009980,
                                true);
                        }

                    }

                    lastAliveState = localPlayer.alive;
                }
                const bool hasPearldrake =
                    HasSpEffect(SPEFFECT_PEARLDRAKE);

                if (hasPearldrake)
                {
                    if (!pearldrakeTracking)
                    {
                        // Pearldrake just became active.
                        // Establish a fresh HP baseline so damage that happened
                        // before equipping it cannot accidentally count.
                        pearldrakeLastHp = localPlayer.hp;
                        pearldrakeTracking = true;
                        pearldrakeBreakRequested = false;

                        Log(
                            "[PEARLDRAKE] Armed at HP " +
                            std::to_string(localPlayer.hp) +
                            "/" +
                            std::to_string(localPlayer.maxHp));
                    }
                    else
                    {
                        const int hpLost =
                            pearldrakeLastHp - localPlayer.hp;

                        if (hpLost > 0 && !pearldrakeBreakRequested)
                        {
                            const float threshold =
                                static_cast<float>(localPlayer.maxHp) *
                                PEARLDRAKE_MAJOR_HIT_RATIO *
                                PEARLDRAKE_DAMAGE_MULTIPLIER;

                            Log(
                                "[PEARLDRAKE] Damage received=" +
                                std::to_string(hpLost) +
                                " Break threshold=" +
                                std::to_string(threshold));

                            if (static_cast<float>(hpLost) >= threshold)
                            {
                                const uintptr_t player =
                                    GetLocalPlayerIns();

                                if (player)
                                {
                                    // Play the shatter effect immediately.
                                    BreakLocalTalisman();

                                    pearldrakeBreakRequested = true;

                                    Log(
                                        "[PEARLDRAKE] Major hit detected -> "
                                        "shield removed and talisman break requested.");
                                }
                            }
                        }

                        // IMPORTANT:
                        // Always update after evaluating THIS individual HP loss.
                        // This prevents multiple small hits from accumulating.
                        pearldrakeLastHp = localPlayer.hp;
                    }
                }
                else
                {
                    pearldrakeTracking = false;
                    pearldrakeBreakRequested = false;
                    pearldrakeLastHp = -1;
                }
            }

            if (currentRole == Role::Sheriff)
            {
                for (int slot = 1;
                    slot < MAX_MM_PLAYERS;
                    ++slot)
                {
                    PlayerState target{};

                    if (!TryReadPlayerSlot(
                        slot,
                        target))
                    {
                        sheriffTrackedInitialized[slot] = false;
                        sheriffTargetWasInnocent[slot] = false;
                        sheriffTargetHitBySheriff[slot] = false;

                        continue;
                    }

                    // ----------------------------------------------------
                    // Latch important information WHILE the target
                    // still exists normally in multiplayer.
                    // ----------------------------------------------------

                    if (ChrInsHasSpEffect(
                        target.chrIns,
                        SPEFFECT_INNOCENT))
                    {
                        sheriffTargetWasInnocent[slot] = true;
                    }

                    if (ChrInsHasSpEffect(
                        target.chrIns,
                        SPEFFECT_SHERIFF_HIT_MARKER))
                    {
                        sheriffTargetHitBySheriff[slot] = true;
                    }

                    if (!sheriffTrackedInitialized[slot])
                    {
                        sheriffTrackedAlive[slot] =
                            target.alive;

                        sheriffTrackedInitialized[slot] =
                            true;

                        continue;
                    }

                    const bool justDied =
                        sheriffTrackedAlive[slot] &&
                        !target.alive;

                    if (justDied)
                    {
                        Log(
                            "[SHERIFF DEBUG] Target died. "
                            "innocentLatched=" +
                            std::string(
                                sheriffTargetWasInnocent[slot]
                                ? "YES"
                                : "NO") +
                            " sheriffHitLatched=" +
                            std::string(
                                sheriffTargetHitBySheriff[slot]
                                ? "YES"
                                : "NO"));

                        if (sheriffTargetWasInnocent[slot] &&
                            sheriffTargetHitBySheriff[slot])
                        {
                            ++sheriffWrongKills;

                            Log(
                                "[SHERIFF] Innocent kill detected. "
                                "Wrong kills=" +
                                std::to_string(
                                    sheriffWrongKills));

                            if (sheriffWrongKills == 1)
                            {
                                PlayerState sheriff{};

                                if (TryReadPlayerSlot(
                                    0,
                                    sheriff))
                                {
                                    const int halfHp =
                                        (sheriff.maxHp + 1) / 2;

                                    if (sheriff.hp > halfHp)
                                    {
                                        if (WritePlayerHp(
                                            sheriff.chrIns,
                                            halfHp))
                                        {
                                            Log(
                                                "[SHERIFF] First wrong kill -> "
                                                "HP reduced to 50%.");
                                        }
                                    }
                                }
                            }
                            else
                            {
                                SetEventFlag(
                                    16009979,
                                    true);
                            }
                        }

                        // Death consumed this victim's attribution.
                        sheriffTargetWasInnocent[slot] = false;
                        sheriffTargetHitBySheriff[slot] = false;
                    }

                    // If they are alive again, start with clean hit attribution.
                    if (!sheriffTrackedAlive[slot] &&
                        target.alive)
                    {
                        sheriffTargetWasInnocent[slot] =
                            ChrInsHasSpEffect(
                                target.chrIns,
                                SPEFFECT_INNOCENT);

                        sheriffTargetHitBySheriff[slot] = false;
                    }

                    sheriffTrackedAlive[slot] =
                        target.alive;
                }
            }

            Sleep(20);
        }

        return 0;
    }
}

// ============================================================
// DLL Entry Point
// ============================================================

BOOL APIENTRY DllMain(
    HMODULE hModule,
    DWORD reason,
    LPVOID)
{
    switch (reason)
    {
    case DLL_PROCESS_ATTACH:
    {
        MM::g_module = hModule;

        DisableThreadLibraryCalls(hModule);

        HANDLE thread =
            CreateThread(
                nullptr,
                0,
                MM::MainThread,
                nullptr,
                0,
                nullptr);

        if (thread)
            CloseHandle(thread);

        break;
    }

    case DLL_PROCESS_DETACH:
    {
        MM::g_running = false;
        break;
    }
    }

    return TRUE;
}
