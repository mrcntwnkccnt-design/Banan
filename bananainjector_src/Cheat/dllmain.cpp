// ============================================================
// Banana Republic — DX11 Hook + ImGui Render + Menu
// Render.cpp / dllmain.cpp
// 
// Зависимости:
//   - ImGui (docking branch или обычный)
//   - kiero (минимальный хукер DX11/DX12)
//     github.com/Rebzzel/kiero
//   - MinHook для перехвата Present
//
// Сборка: добавить в проект imgui/*.cpp, kiero/kiero.cpp
// ============================================================

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <d3d11.h>
#include <dxgi.h>
#include <mutex>
#include <thread>
#include <vector>
#include <string>

// ImGui
#include "imgui/imgui.h"
#include "imgui/imgui_impl_win32.h"
#include "imgui/imgui_impl_dx11.h"

// Kiero — хукает vtable DX11
#include "kiero/kiero.h"

// Наш код
#include "Memory.h"
#include "Offsets.h"
#include "Features.h"

// ──────────────────────────────────────────
// Глобали
// ──────────────────────────────────────────
uintptr_t g_ClientBase = 0;
std::vector<PlayerData> g_Players;
std::mutex              g_PlayersMtx;

static ID3D11Device*           g_D3DDevice    = nullptr;
static ID3D11DeviceContext*    g_D3DContext   = nullptr;
static ID3D11RenderTargetView* g_MainRTV      = nullptr;
static HWND                    g_GameHwnd     = nullptr;
static WNDPROC                 g_OrigWndProc  = nullptr;
static bool                    g_ImGuiInited  = false;
static bool                    g_MenuOpen     = false;

// Тип оригинального Present
typedef HRESULT(__stdcall* Present_t)(IDXGISwapChain*, UINT, UINT);
static Present_t g_OrigPresent = nullptr;

// ──────────────────────────────────────────
// WndProc хук — перехватываем тильду для меню
// и передаём ввод в ImGui
// ──────────────────────────────────────────
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(
    HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

static LRESULT CALLBACK HookedWndProc(HWND hWnd, UINT msg,
                                       WPARAM wParam, LPARAM lParam)
{
    // Тильда — переключаем меню
    if (msg == WM_KEYDOWN && wParam == VK_OEM_3) // VK_OEM_3 = `~`
    {
        g_MenuOpen = !g_MenuOpen;
        return 0;
    }

    // Если меню открыто — ImGui забирает ввод
    if (g_MenuOpen)
    {
        ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam);
        // Блокируем мышь от игры когда меню открыто
        if (msg == WM_MOUSEMOVE || msg == WM_LBUTTONDOWN ||
            msg == WM_RBUTTONDOWN || msg == WM_MOUSEWHEEL)
            return 0;
    }

    return CallWindowProcW(g_OrigWndProc, hWnd, msg, wParam, lParam);
}

// ──────────────────────────────────────────
// Рендер ESP — вызывается каждый Present
// Рисует через ImGui DrawList поверх игры
// ──────────────────────────────────────────
static void RenderESP()
{
    if (!g_Config.wh_enabled) return;

    // Читаем ViewMatrix каждый кадр
    ViewMatrix vm = SafeRead<ViewMatrix>(g_ClientBase + Offsets::Client::dwViewMatrix);

    ImGuiIO& io = ImGui::GetIO();
    float sw = io.DisplaySize.x;
    float sh = io.DisplaySize.y;

    // Рисуем поверх всего — берём фоновый DrawList
    ImDrawList* dl = ImGui::GetBackgroundDrawList();

    std::lock_guard<std::mutex> lk(g_PlayersMtx);

    // Получаем наш тим
    uintptr_t localPawn = SafeRead<uintptr_t>(g_ClientBase + Offsets::Client::dwLocalPlayerPawn);
    int myTeam = localPawn ? SafeRead<uint8_t>(localPawn + Offsets::Pawn::m_iTeamNum) : 0;

    for (auto& pd : g_Players)
    {
        if (!pd.valid || pd.isLocal || !pd.isAlive) continue;

        bool isEnemy = (pd.team != myTeam);

        // Если не показываем союзников — пропускаем
        if (!isEnemy && !g_Config.wh_showAllies) continue;

        // Цвет: красный — враг, зелёный — союзник
        ImU32 color = isEnemy
            ? IM_COL32(255, 60, 60, 220)
            : IM_COL32(60, 255, 60, 220);

        // Проецируем ноги и голову
        float footX, footY, headX, headY;
        bool fVisible = WorldToScreen(pd.origin,  footX, footY, vm, sw, sh);
        bool hVisible = WorldToScreen(pd.headPos, headX, headY, vm, sw, sh);

        if (!fVisible || !hVisible) continue;

        // Высота и ширина бокса
        float height = footY - headY;
        float width  = height * 0.45f;

        float bLeft  = headX - width / 2.0f;
        float bRight = headX + width / 2.0f;
        float bTop   = headY;
        float bBot   = footY;

        if (g_Config.wh_showBox)
        {
            // Основной бокс
            dl->AddRect(
                ImVec2(bLeft,  bTop),
                ImVec2(bRight, bBot),
                color, 0.0f, 0, 1.5f
            );

            // Угловой акцент (corner-box стиль)
            float cw = width  * 0.25f;
            float ch = height * 0.20f;

            // Верхний левый угол
            dl->AddLine({bLeft,      bTop      }, {bLeft + cw, bTop      }, color, 2.0f);
            dl->AddLine({bLeft,      bTop      }, {bLeft,      bTop + ch }, color, 2.0f);
            // Верхний правый угол
            dl->AddLine({bRight,     bTop      }, {bRight-cw,  bTop      }, color, 2.0f);
            dl->AddLine({bRight,     bTop      }, {bRight,     bTop + ch }, color, 2.0f);
            // Нижний левый угол
            dl->AddLine({bLeft,      bBot      }, {bLeft + cw, bBot      }, color, 2.0f);
            dl->AddLine({bLeft,      bBot      }, {bLeft,      bBot - ch }, color, 2.0f);
            // Нижний правый угол
            dl->AddLine({bRight,     bBot      }, {bRight-cw,  bBot      }, color, 2.0f);
            dl->AddLine({bRight,     bBot      }, {bRight,     bBot - ch }, color, 2.0f);
        }

        // Полоска здоровья слева от бокса
        if (g_Config.wh_showHealth)
        {
            float hpFrac = static_cast<float>(pd.health) / 100.0f;
            if (hpFrac > 1.0f) hpFrac = 1.0f;
            if (hpFrac < 0.0f) hpFrac = 0.0f;

            float barX    = bLeft - 5.0f;
            float barFull = bBot - bTop;

            // Фон (серый)
            dl->AddRectFilled(
                ImVec2(barX - 2.0f, bTop),
                ImVec2(barX,        bBot),
                IM_COL32(40, 40, 40, 200)
            );

            // НР — цвет от зелёного к красному
            float r = (1.0f - hpFrac) * 255.0f;
            float g2 = hpFrac * 255.0f;
            ImU32 hpColor = IM_COL32((int)r, (int)g2, 30, 220);

            dl->AddRectFilled(
                ImVec2(barX - 2.0f, bTop + barFull * (1.0f - hpFrac)),
                ImVec2(barX,        bBot),
                hpColor
            );

            // Текст НР
            char hpText[8];
            snprintf(hpText, sizeof(hpText), "%d", pd.health);
            dl->AddText(
                ImVec2(barX - 16.0f, bTop + barFull * (1.0f - hpFrac) - 6.0f),
                IM_COL32(255, 255, 255, 200),
                hpText
            );
        }

        // Имя над боксом
        if (g_Config.wh_showName && pd.name[0] != '\0')
        {
            ImVec2 textSize = ImGui::CalcTextSize(pd.name);
            dl->AddText(
                ImVec2(headX - textSize.x / 2.0f, headY - textSize.y - 3.0f),
                IM_COL32(255, 255, 255, 230),
                pd.name
            );
        }

        // Линия от низа экрана к ногам (snapline)
        dl->AddLine(
            ImVec2(sw / 2.0f, sh),
            ImVec2(footX, footY),
            IM_COL32(255, 255, 0, 80),
            1.0f
        );
    }
}

// ──────────────────────────────────────────
// Меню — ImGui окно
// ──────────────────────────────────────────
static void RenderMenu()
{
    if (!g_MenuOpen) return;

    // Стиль — тёмный с жёлтым акцентом (banana theme)
    ImGui::PushStyleColor(ImGuiCol_WindowBg,      ImVec4(0.08f, 0.08f, 0.08f, 0.95f));
    ImGui::PushStyleColor(ImGuiCol_TitleBg,       ImVec4(0.15f, 0.12f, 0.02f, 1.00f));
    ImGui::PushStyleColor(ImGuiCol_TitleBgActive, ImVec4(0.25f, 0.20f, 0.02f, 1.00f));
    ImGui::PushStyleColor(ImGuiCol_CheckMark,     ImVec4(0.99f, 0.85f, 0.10f, 1.00f));
    ImGui::PushStyleColor(ImGuiCol_SliderGrab,    ImVec4(0.99f, 0.85f, 0.10f, 1.00f));
    ImGui::PushStyleColor(ImGuiCol_SliderGrabActive, ImVec4(1.00f, 0.95f, 0.30f, 1.00f));
    ImGui::PushStyleColor(ImGuiCol_FrameBg,       ImVec4(0.15f, 0.15f, 0.15f, 1.00f));
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered,ImVec4(0.25f, 0.25f, 0.10f, 1.00f));
    ImGui::PushStyleColor(ImGuiCol_Header,        ImVec4(0.20f, 0.17f, 0.03f, 1.00f));
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.30f, 0.25f, 0.05f, 1.00f));

    ImGui::SetNextWindowSize(ImVec2(340.0f, 420.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImVec2(50.0f, 50.0f),    ImGuiCond_FirstUseEver);

    ImGui::Begin("🍌 Banana Republic  |  by Iamminiturtlepirate", nullptr,
                 ImGuiWindowFlags_NoCollapse);

    // ── WALLHACK ─────────────────────────────
    if (ImGui::CollapsingHeader("  WALLHACK", ImGuiTreeNodeFlags_DefaultOpen))
    {
        ImGui::Indent(10.0f);
        ImGui::Checkbox("Enable Wallhack",    &g_Config.wh_enabled);

        if (g_Config.wh_enabled)
        {
            ImGui::Checkbox("Show Allies",    &g_Config.wh_showAllies);
            ImGui::Checkbox("Show Box",       &g_Config.wh_showBox);
            ImGui::Checkbox("Show Health Bar",&g_Config.wh_showHealth);
            ImGui::Checkbox("Show Name",      &g_Config.wh_showName);
        }
        ImGui::Unindent(10.0f);
    }

    ImGui::Separator();

    // ── AIMBOT ───────────────────────────────
    if (ImGui::CollapsingHeader("  AIMBOT", ImGuiTreeNodeFlags_DefaultOpen))
    {
        ImGui::Indent(10.0f);
        ImGui::Checkbox("Enable Aimbot",      &g_Config.ab_enabled);

        if (g_Config.ab_enabled)
        {
            ImGui::SliderFloat("FOV",         &g_Config.ab_fov,    1.0f, 45.0f,  "%.1f°");
            ImGui::SliderFloat("Smoothness",  &g_Config.ab_smooth, 1.0f, 15.0f, "%.1f");
            ImGui::Checkbox("Aim at Head",    &g_Config.ab_aimHead);
            ImGui::TextDisabled("Hold RMB to aim");
        }
        ImGui::Unindent(10.0f);
    }

    ImGui::Separator();

    // ── BUNNYHOP ─────────────────────────────
    if (ImGui::CollapsingHeader("  BUNNYHOP", ImGuiTreeNodeFlags_DefaultOpen))
    {
        ImGui::Indent(10.0f);
        ImGui::Checkbox("Enable Bunnyhop",    &g_Config.bh_enabled);
        if (g_Config.bh_enabled)
            ImGui::TextDisabled("Hold SPACE to bunny hop");
        ImGui::Unindent(10.0f);
    }

    ImGui::Separator();

    // Футер
    ImGui::Spacing();
    ImGui::TextDisabled("~ to toggle menu");
    ImGui::TextDisabled("Banana Republic v1.0");

    ImGui::End();

    ImGui::PopStyleColor(10);
}

// ──────────────────────────────────────────
// Перехваченный Present — здесь весь рендер
// ──────────────────────────────────────────
static HRESULT __stdcall HookedPresent(IDXGISwapChain* pSwapChain,
                                        UINT SyncInterval, UINT Flags)
{
    // Инициализируем ImGui один раз
    if (!g_ImGuiInited)
    {
        pSwapChain->GetDevice(__uuidof(ID3D11Device),
                              reinterpret_cast<void**>(&g_D3DDevice));
        g_D3DDevice->GetImmediateContext(&g_D3DContext);

        // Создаём RenderTargetView на back buffer
        ID3D11Texture2D* backBuffer = nullptr;
        pSwapChain->GetBuffer(0, __uuidof(ID3D11Texture2D),
                              reinterpret_cast<void**>(&backBuffer));
        if (backBuffer)
        {
            g_D3DDevice->CreateRenderTargetView(backBuffer, nullptr, &g_MainRTV);
            backBuffer->Release();
        }

        // Находим окно игры
        DXGI_SWAP_CHAIN_DESC scDesc{};
        pSwapChain->GetDesc(&scDesc);
        g_GameHwnd = scDesc.OutputWindow;

        // Хукаем WndProc
        g_OrigWndProc = reinterpret_cast<WNDPROC>(
            SetWindowLongPtrW(g_GameHwnd, GWLP_WNDPROC,
                              reinterpret_cast<LONG_PTR>(HookedWndProc))
        );

        // ImGui инит
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;

        ImGui::StyleColorsDark();
        ImGui_ImplWin32_Init(g_GameHwnd);
        ImGui_ImplDX11_Init(g_D3DDevice, g_D3DContext);

        g_ImGuiInited = true;
    }

    // Устанавливаем наш RenderTarget
    g_D3DContext->OMSetRenderTargets(1, &g_MainRTV, nullptr);

    // Новый ImGui-кадр
    ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();

    // Рисуем ESP
    RenderESP();

    // Рисуем меню
    RenderMenu();

    ImGui::Render();
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

    return g_OrigPresent(pSwapChain, SyncInterval, Flags);
}

// ──────────────────────────────────────────
// Фоновый поток — сканирование энтитей + фичи
// ──────────────────────────────────────────
static void CheatThread()
{
    // Ждём загрузки client.dll
    while (!g_ClientBase)
    {
        HMODULE hClient = GetModuleHandleW(L"client.dll");
        if (hClient)
            g_ClientBase = reinterpret_cast<uintptr_t>(hClient);
        else
            Sleep(500);
    }

    // Инициализируем kiero и хукаем Present
    if (kiero::init(kiero::RenderType::D3D11) == kiero::Status::Success)
    {
        // Слот 8 в vtable IDXGISwapChain — Present
        kiero::bind(8, reinterpret_cast<void**>(&g_OrigPresent), HookedPresent);
    }

    // Основной цикл чита
    while (true)
    {
        uintptr_t localPawn = SafeRead<uintptr_t>(
            g_ClientBase + Offsets::Client::dwLocalPlayerPawn
        );

        if (localPawn)
        {
            ScanEntities(localPawn);
            AimbotTick(localPawn);
            BhopTick(localPawn);
        }

        Sleep(8); // ~120 Hz
    }
}

// ──────────────────────────────────────────
// DLL Entry Point
// ──────────────────────────────────────────
BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH)
    {
        DisableThreadLibraryCalls(hModule);
        HANDLE hThread = CreateThread(
            nullptr, 0,
            [](LPVOID param) -> DWORD {
                CheatThread();
                return 0;
            },
            nullptr, 0, nullptr
        );
        if (hThread) CloseHandle(hThread);
    }
    else if (reason == DLL_PROCESS_DETACH)
    {
        if (g_ImGuiInited)
        {
            ImGui_ImplDX11_Shutdown();
            ImGui_ImplWin32_Shutdown();
            ImGui::DestroyContext();
        }
        kiero::shutdown();
        if (g_OrigWndProc && g_GameHwnd)
            SetWindowLongPtrW(g_GameHwnd, GWLP_WNDPROC,
                              reinterpret_cast<LONG_PTR>(g_OrigWndProc));
        if (g_MainRTV) g_MainRTV->Release();
    }
    return TRUE;
}