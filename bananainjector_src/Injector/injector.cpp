// ============================================================
// Banana Republic — Injector
// Injector.cpp
// Компиляция: x64, Release, статический CRT
// Использование: BananaInjector.exe <путь_к_cheat.dll>
// ============================================================

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <TlHelp32.h>
#include <iostream>
#include <string>
#include <filesystem>

namespace fs = std::filesystem;

// ──────────────────────────────────────────────
// Найти PID процесса по имени
// ──────────────────────────────────────────────
static DWORD FindProcessId(const std::wstring& processName)
{
    DWORD pid = 0;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE)
        return 0;

    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);

    if (Process32FirstW(snap, &entry))
    {
        do
        {
            if (_wcsicmp(entry.szExeFile, processName.c_str()) == 0)
            {
                pid = entry.th32ProcessID;
                break;
            }
        } while (Process32NextW(snap, &entry));
    }

    CloseHandle(snap);
    return pid;
}

// ──────────────────────────────────────────────
// Инжектировать DLL в процесс
// ──────────────────────────────────────────────
static bool InjectDLL(DWORD pid, const std::string& dllPath)
{
    // Открываем процесс с нужными правами
    HANDLE hProcess = OpenProcess(
        PROCESS_CREATE_THREAD |
        PROCESS_QUERY_INFORMATION |
        PROCESS_VM_OPERATION |
        PROCESS_VM_WRITE |
        PROCESS_VM_READ,
        FALSE,
        pid
    );

    if (!hProcess)
    {
        std::cerr << "[!] OpenProcess failed: " << GetLastError() << "\n";
        return false;
    }

    // Выделяем память под строку с путём к DLL
    SIZE_T pathLen = dllPath.size() + 1;
    LPVOID remoteMem = VirtualAllocEx(
        hProcess,
        nullptr,
        pathLen,
        MEM_COMMIT | MEM_RESERVE,
        PAGE_READWRITE
    );

    if (!remoteMem)
    {
        std::cerr << "[!] VirtualAllocEx failed: " << GetLastError() << "\n";
        CloseHandle(hProcess);
        return false;
    }

    // Записываем путь к DLL в память процесса
    if (!WriteProcessMemory(hProcess, remoteMem, dllPath.c_str(), pathLen, nullptr))
    {
        std::cerr << "[!] WriteProcessMemory failed: " << GetLastError() << "\n";
        VirtualFreeEx(hProcess, remoteMem, 0, MEM_RELEASE);
        CloseHandle(hProcess);
        return false;
    }

    // Получаем адрес LoadLibraryA — он одинаков во всех процессах x64
    LPVOID loadLib = reinterpret_cast<LPVOID>(
        GetProcAddress(GetModuleHandleA("kernel32.dll"), "LoadLibraryA")
    );

    if (!loadLib)
    {
        std::cerr << "[!] GetProcAddress(LoadLibraryA) failed\n";
        VirtualFreeEx(hProcess, remoteMem, 0, MEM_RELEASE);
        CloseHandle(hProcess);
        return false;
    }

    // Запускаем поток в целевом процессе — он вызовет LoadLibraryA(dllPath)
    HANDLE hThread = CreateRemoteThread(
        hProcess,
        nullptr,
        0,
        reinterpret_cast<LPTHREAD_START_ROUTINE>(loadLib),
        remoteMem,
        0,
        nullptr
    );

    if (!hThread)
    {
        std::cerr << "[!] CreateRemoteThread failed: " << GetLastError() << "\n";
        VirtualFreeEx(hProcess, remoteMem, 0, MEM_RELEASE);
        CloseHandle(hProcess);
        return false;
    }

    // Ждём завершения потока (DLL загружена, DllMain отработал)
    WaitForSingleObject(hThread, 8000);

    DWORD exitCode = 0;
    GetExitCodeThread(hThread, &exitCode);

    CloseHandle(hThread);
    VirtualFreeEx(hProcess, remoteMem, 0, MEM_RELEASE);
    CloseHandle(hProcess);

    // exitCode == адрес загруженного модуля (ненулевой = успех)
    return exitCode != 0;
}

// ──────────────────────────────────────────────
// main
// ──────────────────────────────────────────────
int main(int argc, char* argv[])
{
    SetConsoleOutputCP(CP_UTF8);

    std::cout << "╔══════════════════════════════════════╗\n";
    std::cout << "║  Banana Republic Injector            ║\n";
    std::cout << "║  by Iamminiturtlepirate              ║\n";
    std::cout << "╚══════════════════════════════════════╝\n\n";

    if (argc < 2)
    {
        std::cerr << "Usage: BananaInjector.exe <path_to_cheat.dll>\n";
        return 1;
    }

    std::string dllPath = argv[1];

    // Проверяем что файл существует
    if (!fs::exists(dllPath))
    {
        std::cerr << "[!] DLL not found: " << dllPath << "\n";
        return 1;
    }

    // Получаем абсолютный путь
    dllPath = fs::absolute(dllPath).string();

    std::cout << "[*] DLL: " << dllPath << "\n";
    std::cout << "[*] Waiting for cs2.exe...\n";

    DWORD pid = 0;

    // Ждём запуска CS2 если ещё не запущен
    while (pid == 0)
    {
        pid = FindProcessId(L"cs2.exe");
        if (pid == 0)
            Sleep(1000);
    }

    std::cout << "[+] cs2.exe found, PID: " << pid << "\n";
    std::cout << "[*] Injecting...\n";

    // Небольшая задержка — ждём инициализации игры
    Sleep(3000);

    if (InjectDLL(pid, dllPath))
    {
        std::cout << "[+] Injection successful!\n";
        std::cout << "[*] Press ~ in-game to open menu\n";
    }
    else
    {
        std::cerr << "[!] Injection failed\n";
        return 1;
    }

    std::cout << "\nPress Enter to exit...";
    std::cin.get();
    return 0;
}