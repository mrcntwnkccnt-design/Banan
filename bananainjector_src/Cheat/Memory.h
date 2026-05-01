// ============================================================
// Memory.h — безопасное чтение памяти внутри процесса
// ============================================================
#pragma once
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <cstdint>

// Базовые адреса модулей — заполняются при DLL attach
extern uintptr_t g_ClientBase;

// ──────────────────────────────────────────
// Безопасное чтение — SEH-обёртка
// Внутри процесса читаем напрямую,
// но оборачиваем в __try на случай
// невалидных указателей
// ──────────────────────────────────────────
template<typename T>
inline T SafeRead(uintptr_t address)
{
    T result{};
    __try
    {
        result = *reinterpret_cast<T*>(address);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        // Невалидный адрес — возвращаем дефолт
    }
    return result;
}

template<typename T>
inline void SafeWrite(uintptr_t address, const T& value)
{
    __try
    {
        *reinterpret_cast<T*>(address) = value;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {}
}

// Читаем цепочку указателей: base + off1 + off2 + ...
inline uintptr_t ResolvePtr(uintptr_t base, std::initializer_list<uintptr_t> offsets)
{
    uintptr_t current = base;
    for (uintptr_t off : offsets)
    {
        current = SafeRead<uintptr_t>(current);
        if (!current) return 0;
        current += off;
    }
    return current;
}

// ──────────────────────────────────────────
// Структуры
// ──────────────────────────────────────────
struct Vector3
{
    float x, y, z;

    Vector3 operator-(const Vector3& o) const { return { x - o.x, y - o.y, z - o.z }; }
    Vector3 operator+(const Vector3& o) const { return { x + o.x, y + o.y, z + o.z }; }
    float   Length() const { return sqrtf(x * x + y * y + z * z); }
    float   Length2D() const { return sqrtf(x * x + y * y); }
};

struct QAngle { float pitch, yaw, roll; };

// Матрица вью 4x4
struct ViewMatrix { float m[4][4]; };

// Данные об одном игроке (собранные за один проход)
struct PlayerData
{
    uintptr_t pawnPtr    = 0;
    bool      valid      = false;
    bool      isLocal    = false;
    bool      isAlive    = false;
    int       team       = 0;
    int       health     = 0;
    Vector3   origin{};       // позиция ног
    Vector3   headPos{};      // позиция головы (из кости)
    char      name[128]{};
};