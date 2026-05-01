// ============================================================
// Banana Republic — Entity Scanner + Features
// Features.h / Features.cpp
// ============================================================
#pragma once
#include "Memory.h"
#include "Offsets.h"
#include <vector>
#include <cmath>
#include <numbers>

// ──────────────────────────────────────────
// Глобали — заполняются в главном потоке
// ──────────────────────────────────────────
extern uintptr_t g_ClientBase;

// Список игроков для рендер-потока
// (atomic-обмен через двойной буфер)
extern std::vector<PlayerData> g_Players;
extern std::mutex              g_PlayersMtx;

// Настройки (управляются из ImGui)
struct CheatConfig
{
    // Wallhack
    bool  wh_enabled    = false;
    bool  wh_showAllies = true;
    bool  wh_showBox    = true;
    bool  wh_showHealth = true;
    bool  wh_showName   = true;

    // Aimbot
    bool  ab_enabled    = false;
    float ab_fov        = 8.0f;   // градусы
    float ab_smooth     = 3.0f;   // делитель — 1.0 = мгновенно, выше = плавнее
    bool  ab_visCheck   = false;  // проверка видимости (заглушка — расширяемо)
    bool  ab_aimHead    = true;   // целиться в голову

    // Bunnyhop
    bool  bh_enabled    = false;
};

inline CheatConfig g_Config;

// ──────────────────────────────────────────
// WorldToScreen — проецируем 3D в 2D
// Возвращает false если точка за камерой
// ──────────────────────────────────────────
inline bool WorldToScreen(const Vector3& world, float& sx, float& sy,
                           const ViewMatrix& vm, float screenW, float screenH)
{
    // Умножаем на матрицу вью-проекции
    float w = vm.m[3][0] * world.x + vm.m[3][1] * world.y +
              vm.m[3][2] * world.z + vm.m[3][3];

    if (w < 0.001f) return false; // За камерой

    float x = vm.m[0][0] * world.x + vm.m[0][1] * world.y +
              vm.m[0][2] * world.z + vm.m[0][3];
    float y = vm.m[1][0] * world.x + vm.m[1][1] * world.y +
              vm.m[1][2] * world.z + vm.m[1][3];

    // NDC -> пиксели
    sx = (screenW / 2.0f) + (x / w) * (screenW / 2.0f);
    sy = (screenH / 2.0f) - (y / w) * (screenH / 2.0f);

    return true;
}

// ──────────────────────────────────────────
// Получить позицию кости из массива матриц
// Каждая кость — float[3][4] (row-major 3x4)
// ──────────────────────────────────────────
inline Vector3 GetBonePosition(uintptr_t boneArrayPtr, int boneIndex)
{
    // Размер одной кости: 3 * 4 float = 48 байт
    uintptr_t boneAddr = boneArrayPtr + static_cast<uintptr_t>(boneIndex) * 32u;
    // Трансляционная часть матрицы — последний столбец (x,y,z)
    float bx = SafeRead<float>(boneAddr + 0x00);
    float by = SafeRead<float>(boneAddr + 0x04);
    float bz = SafeRead<float>(boneAddr + 0x08);
    return { bx, by, bz };
}

// ──────────────────────────────────────────
// Сканируем EntityList, собираем PlayerData
// Вызывается из фонового потока ~60 раз/сек
// ──────────────────────────────────────────
inline void ScanEntities(uintptr_t localPawnPtr)
{
    uintptr_t entityList = SafeRead<uintptr_t>(g_ClientBase + Offsets::Client::dwEntityList);
    if (!entityList) return;

    std::vector<PlayerData> players;
    players.reserve(64);

    // CS2 EntityList: список блоков по 512 байт,
    // первый блок по offset 0x10, внутри — указатели с шагом 0x78
    uintptr_t listEntry = SafeRead<uintptr_t>(entityList + 0x10);
    if (!listEntry) return;

    for (int i = 0; i < 64; i++)
    {
        // Получаем указатель на контроллер
        uintptr_t controllerPtr = SafeRead<uintptr_t>(listEntry + (i * 0x78));
        if (!controllerPtr) continue;

        // Из контроллера — хендл pawn
        uint32_t pawnHandle = SafeRead<uint32_t>(controllerPtr + Offsets::Controller::m_hPlayerPawn);
        if (pawnHandle == 0xFFFFFFFF) continue;

        // Resolving handle -> pawn pointer через EntityList
        // В CS2 handle содержит индекс: (handle >> 9) & 0x7FFF
        int pawnIndex = (pawnHandle >> 9) & 0x7FFF;

        // Получаем блок для этого индекса
        uintptr_t listEntry2 = SafeRead<uintptr_t>(entityList + 0x10 + (8 * (pawnIndex >> 9)));
        if (!listEntry2) continue;

        uintptr_t pawnPtr = SafeRead<uintptr_t>(listEntry2 + (0x78 * (pawnIndex & 0x1FF)));
        if (!pawnPtr) continue;

        PlayerData pd{};
        pd.pawnPtr = pawnPtr;

        // Живой?
        uint8_t lifeState = SafeRead<uint8_t>(pawnPtr + Offsets::Pawn::m_lifeState);
        pd.isAlive = (lifeState == 256 || lifeState == 0); // 0 = LIFE_ALIVE в Source 2

        if (!pd.isAlive) continue;

        pd.health  = SafeRead<int32_t>(pawnPtr + Offsets::Pawn::m_iHealth);
        pd.team    = SafeRead<uint8_t>(pawnPtr + Offsets::Pawn::m_iTeamNum);
        pd.isLocal = (pawnPtr == localPawnPtr);

        // Имя из контроллера
        SafeRead<char[128]>; // placeholder syntax — читаем побайтово через memcpy
        uintptr_t nameAddr = controllerPtr + Offsets::Controller::m_sSanitizedPlayerName;
        for (int c = 0; c < 127; c++)
            pd.name[c] = SafeRead<char>(nameAddr + c);
        pd.name[127] = '\0';

        // Позиция
        pd.origin = SafeRead<Vector3>(pawnPtr + Offsets::Pawn::m_vecOrigin);

        // Голова из скелета
        uintptr_t gameSceneNode = SafeRead<uintptr_t>(pawnPtr + Offsets::Pawn::m_pGameSceneNode);
        if (gameSceneNode)
        {
            uintptr_t boneArray = SafeRead<uintptr_t>(gameSceneNode + Offsets::Pawn::m_boneArray);
            if (boneArray)
                pd.headPos = GetBonePosition(boneArray, Offsets::Bones::HEAD);
        }

        // Если голова не получилась — аппроксимация
        if (pd.headPos.x == 0.0f && pd.headPos.y == 0.0f)
            pd.headPos = { pd.origin.x, pd.origin.y, pd.origin.z + 75.0f };

        pd.valid = true;
        players.push_back(pd);
    }

    // Атомарный обмен буфера
    std::lock_guard<std::mutex> lk(g_PlayersMtx);
    g_Players = std::move(players);
}

// ──────────────────────────────────────────
// АИМБОТ
// Вызывается из главного потока после сканирования
// ──────────────────────────────────────────
inline void AimbotTick(uintptr_t localPawnPtr)
{
    if (!g_Config.ab_enabled) return;
    if (!(GetAsyncKeyState(VK_RBUTTON) & 0x8000)) return; // Активация ПКМ

    // Читаем наши углы
    QAngle myAngles = SafeRead<QAngle>(localPawnPtr + Offsets::Pawn::m_angEyeAngles);
    Vector3 myPos   = SafeRead<Vector3>(localPawnPtr + Offsets::Pawn::m_vecOrigin);
    myPos.z += 64.0f; // Примерная высота глаз

    // Получаем наш тим
    int myTeam = SafeRead<uint8_t>(localPawnPtr + Offsets::Pawn::m_iTeamNum);

    float bestFov = g_Config.ab_fov;
    uintptr_t bestPawn = 0;
    Vector3 bestTarget{};

    {
        std::lock_guard<std::mutex> lk(g_PlayersMtx);
        for (auto& pd : g_Players)
        {
            if (!pd.valid || pd.isLocal) continue;
            if (pd.team == myTeam) continue; // Не стреляем в союзников
            if (!pd.isAlive || pd.health <= 0) continue;

            // Цель — голова или центр
            Vector3 target = g_Config.ab_aimHead ? pd.headPos : pd.origin;
            target.z += g_Config.ab_aimHead ? 0.0f : 40.0f;

            // Вектор к цели
            Vector3 delta = target - myPos;
            float dist = delta.Length();
            if (dist < 0.001f) continue;

            // Вычисляем угол к цели
            float targetPitch = -atan2f(delta.z, delta.Length2D()) *
                                (180.0f / static_cast<float>(std::numbers::pi));
            float targetYaw   =  atan2f(delta.y, delta.x) *
                                (180.0f / static_cast<float>(std::numbers::pi));

            // FOV — угловое расстояние от нашего взгляда до цели
            float dPitch = targetPitch - myAngles.pitch;
            float dYaw   = targetYaw   - myAngles.yaw;

            // Нормализуем yaw в [-180, 180]
            while (dYaw >  180.0f) dYaw -= 360.0f;
            while (dYaw < -180.0f) dYaw += 360.0f;

            float fovDist = sqrtf(dPitch * dPitch + dYaw * dYaw);

            if (fovDist < bestFov)
            {
                bestFov  = fovDist;
                bestPawn = pd.pawnPtr;
                bestTarget = { targetPitch, targetYaw, 0.0f };
            }
        }
    }

    if (!bestPawn) return;

    // Плавность — делим дельту на smooth коэффициент
    float dPitch = bestTarget.x - myAngles.pitch;
    float dYaw   = bestTarget.y - myAngles.yaw;

    while (dYaw >  180.0f) dYaw -= 360.0f;
    while (dYaw < -180.0f) dYaw += 360.0f;

    float smooth = g_Config.ab_smooth;
    myAngles.pitch += dPitch / smooth;
    myAngles.yaw   += dYaw   / smooth;

    // Клампим pitch в [-89, 89]
    if (myAngles.pitch >  89.0f) myAngles.pitch =  89.0f;
    if (myAngles.pitch < -89.0f) myAngles.pitch = -89.0f;

    // Записываем углы обратно в pawn
    SafeWrite<QAngle>(localPawnPtr + Offsets::Pawn::m_angEyeAngles, myAngles);
}

// ──────────────────────────────────────────
// БАННИ-ХОП
// Зажимаешь пробел — автоматически прыгает
// каждый раз при приземлении
// ──────────────────────────────────────────
inline void BhopTick(uintptr_t localPawnPtr)
{
    if (!g_Config.bh_enabled) return;
    if (!(GetAsyncKeyState(VK_SPACE) & 0x8000)) return;

    int32_t flags = SafeRead<int32_t>(localPawnPtr + Offsets::Pawn::m_fFlags);

    bool onGround = (flags & (1 << 0)) != 0; // FL_ONGROUND

    if (onGround)
    {
        // Симулируем нажатие пробела через SendInput
        INPUT input[2]{};

        input[0].type       = INPUT_KEYBOARD;
        input[0].ki.wVk     = VK_SPACE;
        input[0].ki.dwFlags = 0; // KeyDown

        input[1].type       = INPUT_KEYBOARD;
        input[1].ki.wVk     = VK_SPACE;
        input[1].ki.dwFlags = KEYEVENTF_KEYUP; // KeyUp

        SendInput(2, input, sizeof(INPUT));
    }
}