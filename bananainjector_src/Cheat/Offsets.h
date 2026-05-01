// ============================================================
// Banana Republic — Core + Memory Layer
// Memory.h + Offsets.h
// ============================================================

// ──────────────────────────────────────────
// Offsets.h — актуальные оффсеты CS2
// Источник: cs2-dumper / github.com/a2x/cs2-dumper
// Обновлено под билд ~2025-2026
// ──────────────────────────────────────────
#pragma once
#include <cstdint>

namespace Offsets
{
    // client.dll
    namespace Client
    {
        // Глобальный указатель на EntitySystem
        inline constexpr uintptr_t dwEntityList          = 0x19DEC38;
        // ViewMatrix (float[4][4]) в client.dll
        inline constexpr uintptr_t dwViewMatrix          = 0x19D2C00;
        // Локальный контроллер игрока
        inline constexpr uintptr_t dwLocalPlayerController = 0x1A2A810;
        // Локальный пешка (pawn) игрока
        inline constexpr uintptr_t dwLocalPlayerPawn     = 0x173C418;
    }

    // Поля C_BaseEntity / C_CSPlayerPawn
    namespace Pawn
    {
        inline constexpr uintptr_t m_iHealth             = 0x344;   // int32
        inline constexpr uintptr_t m_iTeamNum            = 0x3E3;   // uint8
        inline constexpr uintptr_t m_lifeState           = 0x348;   // uint8, 256 = alive
        inline constexpr uintptr_t m_vecOrigin           = 0xD38;   // Vector3 (game coords)
        inline constexpr uintptr_t m_pGameSceneNode      = 0x328;   // ptr -> CGameSceneNode
        inline constexpr uintptr_t m_boneArray           = 0x80;    // ptr -> bone matrix в CGameSceneNode
        inline constexpr uintptr_t m_angEyeAngles        = 0x1510;  // QAngle (pitch, yaw, roll)
        inline constexpr uintptr_t m_fFlags              = 0x3EC;   // int32, FL_ONGROUND = (1<<0)
        inline constexpr uintptr_t m_hPlayerPawn         = 0x7E4;   // CHandle -> C_CSPlayerPawn
        inline constexpr uintptr_t m_sSanitizedPlayerName = 0x640;  // char[128]
    }

    // Поля C_CSPlayerController
    namespace Controller
    {
        inline constexpr uintptr_t m_hPlayerPawn         = 0x7E4;
        inline constexpr uintptr_t m_sSanitizedPlayerName = 0x640;
        inline constexpr uintptr_t m_iTeamNum            = 0x3E3;
    }

    // Кости скелета (индексы)
    namespace Bones
    {
        inline constexpr int HEAD  = 6;
        inline constexpr int NECK  = 5;
        inline constexpr int CHEST = 4;
        inline constexpr int PELVIS = 0;
    }
}