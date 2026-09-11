#include <iostream>
#include <windows.h>
#include <cstdio>
#include <thread>
#include <atomic>
#include <fstream>
#include <string>
#include <chrono>
#include <iomanip>
#include <tlhelp32.h>
#include <vector>
#include <conio.h>
#include <algorithm>
#include  <vector>

#include <unordered_set>
#include "offsets.hpp"

// ==================== Math ====================
#include <cmath>



struct Vector3 {
    float x, y, z;
};

struct ViewAngles {
    float pitch;
    float yaw;
    float roll;
};

ViewAngles CalcAngle(Vector3 src, Vector3 dst) {
    Vector3 delta = { dst.x - src.x, dst.y - src.y, dst.z - src.z };
    float hyp = sqrtf(delta.x * delta.x + delta.y * delta.y);
    ViewAngles angles;
    angles.pitch = -atan2f(delta.z, hyp) * (180.0f / 3.14159265f);
    angles.yaw   =  atan2f(delta.y, delta.x) * (180.0f / 3.14159265f);
    angles.roll  = 0.0f;
    return angles;
}

float AngleDiff(float a, float b) {
    float diff = a - b;
    while (diff > 180.0f)  diff -= 360.0f;
    while (diff < -180.0f) diff += 360.0f;
    return diff;
}

// ==================== DLL ====================
typedef bool (*fnLoadDriver)(HANDLE*);
typedef void (*fnUnloadDriver)(HANDLE);

// ==================== IOCTL ====================
#define IOCTL_MEM_READ   CTL_CODE(0x8BFE, 0xB08, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_MEM_WRITE  CTL_CODE(0x8BFE, 0xB09, METHOD_BUFFERED, FILE_ANY_ACCESS)

#pragma pack(push, 1)
struct MEM_REQUEST {
    ULONG  ProcessId;
    ULONG  _pad;
    UINT64 Address;
    UINT64 Buffer;
    UINT64 Size;
};
#pragma pack(pop)

// ==================== types ====================
DWORD     gPid    = 0;
uintptr_t gClient = 0;
HANDLE    g_hDev  = INVALID_HANDLE_VALUE;
bool      g_wh    = false;

template<typename T>
T Read(uintptr_t addr) {
    T v{};
    if (!g_hDev || !addr) return v;
    MEM_REQUEST req{ gPid, 0, (UINT64)addr, (UINT64)(uintptr_t)&v, sizeof(T) };
    DeviceIoControl(g_hDev, IOCTL_MEM_READ, &req, sizeof(req), nullptr, 0, nullptr, nullptr);
    return v;
}

template<typename T>
void Write(uintptr_t addr, T val) {
    if (!g_hDev || !addr) return;
    MEM_REQUEST req{ gPid, 0, (UINT64)addr, (UINT64)(uintptr_t)&val, sizeof(T) };
    DeviceIoControl(g_hDev, IOCTL_MEM_WRITE, &req, sizeof(req), nullptr, 0, nullptr, nullptr);
}

// ==================== Стан ====================
std::atomic<bool> debug_view = false;
std::atomic<bool> F1press    = false;
std::atomic<bool> F2press    = false;
std::atomic<bool> F3press    = false;
std::atomic<bool> F4press    = false;

//glow alpha
std::atomic<uint8_t> glowAlpha = 130;


// trigger
int g_triggerReact    = 0;  // затримка реакції перед пострілом (мс)
int g_triggerDelay    = 50;  // затримка між пострілами (мс)
int g_triggerHold     = 10;  // як довго тримати кнопку (мс)

#pragma pack(push, 1)
struct GlowBlock {
    int32_t mode;
    uint8_t pad1[12];
    uint8_t r;
    uint8_t g;
    uint8_t b;
    uint8_t a;
    uint8_t pad2[13];
    uint8_t enable;
};
#pragma pack(pop)

void WriteGlow(uintptr_t pawn, uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    if (!pawn || !g_hDev) return;
    GlowBlock block = {};
    block.mode   = 1;
    block.r      = r;
    block.g      = g;
    block.b      = b;
    block.a      = a;
    block.enable = 1;
    MEM_REQUEST req{};
    req.ProcessId = gPid;
    req._pad      = 0;
    req.Address   = (UINT64)(pawn + client_dll::m_Glow + 0x30);
    req.Buffer    = (UINT64)(uintptr_t)&block;
    req.Size      = sizeof(GlowBlock);
    DeviceIoControl(g_hDev, IOCTL_MEM_WRITE,
        &req, sizeof(req), nullptr, 0, nullptr, nullptr);
}

void ClearGlow(uintptr_t pawn) {
    if (!pawn || !g_hDev) return;
    GlowBlock block = {};
    MEM_REQUEST req{};
    req.ProcessId = gPid;
    req._pad      = 0;
    req.Address   = (UINT64)(pawn + client_dll::m_Glow + 0x30);
    req.Buffer    = (UINT64)(uintptr_t)&block;
    req.Size      = sizeof(GlowBlock);
    DeviceIoControl(g_hDev, IOCTL_MEM_WRITE,
        &req, sizeof(req), nullptr, 0, nullptr, nullptr);
}

// ==================== CONSOLE ====================
bool ShowLoading(const std::string& name, int durationMs, bool stepSuccess) {
    const int total      = 20;
    const int labelWidth = 16;
    for (int i = 0; i < total; ++i) {
        std::cout << "\r[+] " << std::left << std::setw(labelWidth) << name << " [";
        for (int j = 0; j < total; ++j) {
            if (j < i) std::cout << "#";
            else       std::cout << " ";
        }
        std::cout << "] " << (i * 100 / total) << "%";
        std::cout.flush();
        std::this_thread::sleep_for(std::chrono::milliseconds(durationMs / total));
    }
    std::cout << "\r[+] " << std::left << std::setw(labelWidth) << name << " [";
    for (int j = 0; j < total; ++j) {
        if (stepSuccess) std::cout << "#";
        else             std::cout << "X";
    }
    if (stepSuccess) { std::cout << "] 100%\n"; return true; }
    else             { std::cout << "] ERROR\n"; return false; }
}

// ==================== Пошук процесу ====================
DWORD GetProcessIdByName(const std::wstring& processName) {
    DWORD pid = 0;
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) return 0;
    PROCESSENTRY32W pe;
    pe.dwSize = sizeof(pe);
    if (Process32FirstW(snapshot, &pe)) {
        do {
            if (_wcsicmp(pe.szExeFile, processName.c_str()) == 0) {
                pid = pe.th32ProcessID; break;
            }
        } while (Process32NextW(snapshot, &pe));
    }
    CloseHandle(snapshot);
    return pid;
}

// ==================== База модуля ====================
uintptr_t GetMod(DWORD pid, const wchar_t* name) {
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid);
    MODULEENTRY32W e{};
    e.dwSize = sizeof(e);
    uintptr_t result = 0;
    if (Module32FirstW(snap, &e)) {
        do {
            if (!_wcsicmp(e.szModule, name)) { result = (uintptr_t)e.modBaseAddr; break; }
        } while (Module32NextW(snap, &e));
    }
    CloseHandle(snap);
    return result;
}

// ==================== Головна консольна послідовність ====================
void console_view() {
    std::cout << R"(
 ___  ____   ________  _______     ____  _____  ________  _____                         _____
|_  ||_  _| |_   __  ||_   __ \   |_   \|_   _||_   __  ||_   _|                       / ___ `.
  | |_/ /     | |_ \_|  | |__) |    |   \ | |    | |_ \_|  | |           .---.  .--.  |_/___) |
  |  __'.     |  _| _   |  __ /     | |\ \| |    |  _| _   | |   _      / /'`\]( (`\]  .'____.'
 _| |  \ \_  _| |__/ | _| |  \ \_  _| |_\   |_  _| |__/ | _| |__/ |     | \__.  `'.'. / /_____
|____||____||________||____| |___||_____|\____||________||________|     '.___.'[\__) )|_______|
                 )" << '\n';



    // 1. LOADING DLL
    HMODULE hDll = LoadLibraryW(L"libloadDriver.dll");
    bool dllLoaded = (hDll != NULL);
    if (!ShowLoading("LOADING DLL", 1200, dllLoaded)) {
        if (!dllLoaded) printf("[-] DLL load failed, error: %lu\n", GetLastError());
        return;
    }

    auto pLoadDriver   = (fnLoadDriver)  GetProcAddress(hDll, "LoadDriver");
    auto pUnloadDriver = (fnUnloadDriver)GetProcAddress(hDll, "UnloadDriver");

    if (!pLoadDriver || !pUnloadDriver) {
        ShowLoading("EXPORTS CHECK", 500, false);
        printf("[-] Exports not found\n");
        FreeLibrary(hDll);
        return;
    }
    ShowLoading("EXPORTS CHECK", 500, true);

    // 2. LOADING DRIVER
    HANDLE hDev     = INVALID_HANDLE_VALUE;
    const char* tempLog = "kdmapper_debug.log";
    freopen(tempLog, "w", stdout);
    freopen(tempLog, "a", stderr);

    bool driverLoaded = pLoadDriver(&hDev);

    freopen("CONOUT$", "w", stdout);
    freopen("CONOUT$", "w", stderr);
    freopen("CONERR$", "w", stderr);

    if (!driverLoaded) {
        std::ifstream logFile(tempLog);
        if (logFile) {
            std::string line;
            while (std::getline(logFile, line)) std::cout << line << '\n';
            logFile.close();
        }
        DeleteFileA(tempLog);
        ShowLoading("LOADING DRIVER", 800, false);
        printf("[-] Driver load failed\n");
        FreeLibrary(hDll);
        return;
    }
    DeleteFileA(tempLog);
    if (!ShowLoading("LOADING DRIVER", 800, true)) return;

    // 3. WAITING FOR CS2
    DWORD cs2Pid  = 0;
    bool  cs2Found = false;
    while (!cs2Found) {
        cs2Pid   = GetProcessIdByName(L"cs2.exe");
        cs2Found = (cs2Pid != 0);
        if (!cs2Found) {
            std::cout << "\r[+] WAITING FOR CS2..." << std::flush;
            Sleep(2000);
        }
    }

    g_hDev = hDev;
    gPid   = cs2Pid;
    ShowLoading("CS 2 CHECK", 1000, true);

    // 4. WRITE MEMORY TEST
    unsigned char testBuf[4] = {0};
    MEM_REQUEST req;
    req.ProcessId = GetCurrentProcessId();
    req._pad      = 0;
    req.Address   = (UINT64)(uintptr_t)&testBuf;
    req.Buffer    = (UINT64)(uintptr_t)"\x90\x90\x90\x90";
    req.Size      = 4;
    DWORD ret     = 0;
    BOOL writeOk  = DeviceIoControl(hDev, IOCTL_MEM_WRITE,
                        &req, sizeof(req), nullptr, 0, &ret, nullptr);
    bool memWritten = (writeOk != 0);

    if (!ShowLoading("WRITE MEMORY", 700, memWritten)) {
        printf("[-] Write memory failed: %lu\n", GetLastError());
        return;
    }

    std::cout << "\n[+] Ready.\n\n F1 = WallHack\n F2 = TriggerBot \n F3 = BunnyHop \n F4 = Aimbot";
    Sleep(1000);

    CONSOLE_CURSOR_INFO cursorInfo;
    GetConsoleCursorInfo(GetStdHandle(STD_OUTPUT_HANDLE), &cursorInfo);
    cursorInfo.bVisible = false;
    SetConsoleCursorInfo(GetStdHandle(STD_OUTPUT_HANDLE), &cursorInfo);
}

// // ==================== ДЕБАГ (видалити після фіксу) ====================
// void debug_status() {
//     while (true) {
//         Sleep(500);
//         bool      f1          = F1press.load();
//         uintptr_t localPlayer = 0;
//         uint8_t   myTeam      = 0;
//         uintptr_t entityList  = 0;
//         uintptr_t listEntry   = 0;
//         int       enemyCount  = 0;
//
//         if (g_hDev != INVALID_HANDLE_VALUE && gPid != 0 && gClient != 0) {
//             localPlayer = Read<uintptr_t>(gClient + offsets::dwLocalPlayerPawn);
//             if (localPlayer) {
//                 myTeam     = Read<uint8_t>(localPlayer + client_dll::m_iTeamNum);
//                 entityList = Read<uintptr_t>(gClient + offsets::dwEntityList);
//                 if (entityList) {
//                     listEntry = Read<uintptr_t>(entityList + 0x10);
//                     if (listEntry) {
//                         for (int i = 0; i < 64; i++) {
//                             uintptr_t ctrl = Read<uintptr_t>(listEntry + (uintptr_t)0x70 * i);
//                             if (!ctrl) continue;
//                             int handle = Read<int32_t>(ctrl + client_dll::m_hPlayerPawn);
//                             if (!handle) continue;
//                             int index      = handle & 0x7FFF;
//                             int list_index = index >> 9;
//                             uintptr_t le2  = Read<uintptr_t>(entityList + 0x10 + (uintptr_t)0x8 * list_index);
//                             if (!le2) continue;
//                             uintptr_t pawn = Read<uintptr_t>(le2 + (uintptr_t)0x70 * (index & 0x1FF));
//                             if (!pawn || pawn == localPlayer) continue;
//                             int     hp        = Read<int32_t>(pawn + client_dll::m_iHealth);
//                             uint8_t team      = Read<uint8_t>(pawn + client_dll::m_iTeamNum);
//                             uint8_t lifeState = Read<uint8_t>(pawn + client_dll::m_lifeState);
//                             if (hp < 1 || hp > 100 || lifeState != 0) continue;
//                             if (team == myTeam) continue;
//                             enemyCount++;
//                         }
//                     }
//                 }
//             }
//         }
//
//         std::cout
//             << "\r"
//             << "F1="      << (f1           ? "ON " : "OFF") << " | "
//             << "pid="     << gPid                            << " | "
//             << "client="  << (gClient      ? "OK"  : "NO")  << " | "
//             << "lp="      << (localPlayer  ? "OK"  : "NO")  << " | "
//             << "team="    << (int)myTeam                     << " | "
//             << "eList="   << (entityList   ? "OK"  : "NO")  << " | "
//             << "lEntry="  << (listEntry    ? "OK"  : "NO")  << " | "
//             << "enemies=" << enemyCount
//             << "    " << std::flush;
//     }
// }
// ==================== кінець дебагу ====================

// ==================== потік клавіш ====================
void keylistener() {
    bool f1Prev = false;
    bool f2Prev = false;
    bool f3Prev = false;
    bool f4Prev = false;


    while (true) {
        bool f4Now = (GetAsyncKeyState(VK_F4) & 0x8000) != 0;
        bool f1Now = (GetAsyncKeyState(VK_F1) & 0x8000) != 0;
        bool f2Now = (GetAsyncKeyState(VK_F2) & 0x8000) != 0;
        bool f3Now = (GetAsyncKeyState(VK_F3) & 0x8000) != 0;

        bool pgUpNow   = (GetAsyncKeyState(VK_PRIOR) & 0x8000) != 0;
        bool pgDnNow   = (GetAsyncKeyState(VK_NEXT)  & 0x8000) != 0;

        if (f1Now && !f1Prev) {
            F1press = !F1press;
        }
        if (f2Now && !f2Prev) {
            F2press = !F2press;

        }
        if (f3Now && !f3Prev) {
            F3press = !F3press;
        }

        if (f4Now && !f4Prev) F4press = !F4press;
        f4Prev = f4Now;


        if (pgUpNow) {
            uint8_t cur = glowAlpha.load();
            if (cur < 255) glowAlpha.store(cur + 5);
        }
        if (pgDnNow) {
            uint8_t cur = glowAlpha.load();
            if (cur > 5) glowAlpha.store(cur - 5);
        }


        f1Prev = f1Now;
        f2Prev = f2Now;
        f3Prev = f3Now;
        Sleep(50);
    }
}

// ==================== Wallhack ====================
void HPtoRGB(int hp, uint8_t& r, uint8_t& g, uint8_t& b) {
    hp = (hp < 1) ? 1 : (hp > 100) ? 100 : hp;
    float t = (hp - 1) / 99.0f; // 0.0 = 1hp, 1.0 = 100hp

    if (t < 0.5f) {
        // червоний -> жовтий
        r = 255;
        g = (uint8_t)(t * 2.0f * 255);
    } else {
        // жовтий -> зелений
        r = (uint8_t)((1.0f - t) * 2.0f * 255);
        g = 255;
    }
    b = 0;
}
void wallhack() {
    while (g_hDev == INVALID_HANDLE_VALUE || gPid == 0) Sleep(1);
    while (!gClient) {
        gClient = GetMod(gPid, L"client.dll");
        if (!gClient) Sleep(1);
    }

    std::vector<uintptr_t> prevGlowed;
    uint8_t prevMyTeam = 0;

    while (true) {
        bool f1Now = F1press.load();

        if (!f1Now) {
            if (!prevGlowed.empty()) {
                for (uintptr_t addr : prevGlowed) ClearGlow(addr);
                prevGlowed.clear();
            }
            prevMyTeam = 0;
            continue;
        }

        uintptr_t localPlayer = Read<uintptr_t>(gClient + offsets::dwLocalPlayerPawn);
        if (!localPlayer) continue;
        uint8_t myTeam = Read<uint8_t>(localPlayer + client_dll::m_iTeamNum);

        if (prevMyTeam != 0 && myTeam != prevMyTeam) {
            for (uintptr_t addr : prevGlowed) ClearGlow(addr);
            prevGlowed.clear();
        }
        prevMyTeam = myTeam;

        uintptr_t entityList = Read<uintptr_t>(gClient + offsets::dwEntityList);
        if (!entityList) continue;
        uintptr_t listEntry = Read<uintptr_t>(entityList + 0x10);
        if (!listEntry) continue;

        std::vector<uintptr_t> currentGlowed;
        currentGlowed.reserve(64);

        for (int i = 0; i < 64; i++) {
            uintptr_t ctrl = Read<uintptr_t>(listEntry + (uintptr_t)0x70 * i);
            if (!ctrl) continue;
            int handle = Read<int32_t>(ctrl + client_dll::m_hPlayerPawn);
            if (!handle) continue;
            int index      = handle & 0x7FFF;
            int list_index = index >> 9;
            uintptr_t le2  = Read<uintptr_t>(entityList + 0x10 + (uintptr_t)0x8 * list_index);
            if (!le2) continue;
            uintptr_t pawn = Read<uintptr_t>(le2 + (uintptr_t)0x70 * (index & 0x1FF));
            if (!pawn || pawn == localPlayer) continue;

            int     hp        = Read<int32_t>(pawn + client_dll::m_iHealth);
            uint8_t team      = Read<uint8_t>(pawn + client_dll::m_iTeamNum);
            uint8_t lifeState = Read<uint8_t>(pawn + client_dll::m_lifeState);

            if (hp < 1 || hp > 100 || lifeState != 0) continue;
            if (team == myTeam) continue;

            currentGlowed.push_back(pawn);
            uint8_t r, g, b;
            HPtoRGB(hp, r, g, b);
            WriteGlow(pawn, r, g, b, glowAlpha.load());
        }

        for (uintptr_t oldAddr : prevGlowed) {
            bool stillHere = std::find(
                currentGlowed.begin(), currentGlowed.end(), oldAddr
            ) != currentGlowed.end();
            if (!stillHere) ClearGlow(oldAddr);
        }

        prevGlowed = std::move(currentGlowed);
    }
}
// ==================== TrigerBot ====================
void Thread_Triggerbot() {
    while (true) {
        if (!F2press.load()) { Sleep(50); continue; }

        if (!gClient) { Sleep(50); continue; }

        uintptr_t localPlayer = Read<uintptr_t>(gClient + offsets::dwLocalPlayerPawn);
        if (!localPlayer) { Sleep(10); continue; }

        // m_iIDEntIndex читається з controller а не з pawn
        // dwLocalPlayerController -> m_iIDEntIndex
        int entIndex = Read<int32_t>(localPlayer + client_dll::m_iIDEntIndex);
        if (entIndex <= 0) { Sleep(5); continue; }

        uintptr_t entityList = Read<uintptr_t>(gClient + offsets::dwEntityList);
        if (!entityList) { Sleep(5); continue; }

        int index      = entIndex & 0x7FFF;
        int list_index = index >> 9;
        uintptr_t le2  = Read<uintptr_t>(entityList + 0x10 + (uintptr_t)0x8 * list_index);
        if (!le2) { Sleep(5); continue; }

        uintptr_t pawn = Read<uintptr_t>(le2 + (uintptr_t)0x70 * (index & 0x1FF));
        if (!pawn) { Sleep(5); continue; }

        int     hp        = Read<int32_t>(pawn + client_dll::m_iHealth);
        uint8_t team      = Read<uint8_t>(pawn + client_dll::m_iTeamNum);
        uint8_t lifeState = Read<uint8_t>(pawn + client_dll::m_lifeState);
        uint8_t myTeam    = Read<uint8_t>(localPlayer + client_dll::m_iTeamNum);

        if (hp <= 0 || hp > 100 || lifeState != 0 || team == myTeam) { Sleep(5); continue; }

        float velX  = Read<float>(localPlayer + client_dll::m_vecVelocity);
        float velY  = Read<float>(localPlayer + client_dll::m_vecVelocity + 0x4);
        float speed = velX * velX + velY * velY;

        if (speed > 10000.0f) { Sleep(5); continue; }

        Sleep(g_triggerReact + rand() % 30);
        mouse_event(MOUSEEVENTF_LEFTDOWN, 0, 0, 0, 0);
        Sleep(g_triggerHold);
        mouse_event(MOUSEEVENTF_LEFTUP, 0, 0, 0, 0);
        Sleep(g_triggerDelay);
    }
}


// ==================== bunnyhop ====================
void bunnyhop() {
    while (true) {
        if (!F3press.load()) {
            if (gClient) Write<DWORD>(gClient + offsets::jump, 256);
            Sleep(50);
            continue;
        }

        if (!gClient) { Sleep(1); continue; }

        uintptr_t localPlayer = Read<uintptr_t>(gClient + offsets::dwLocalPlayerPawn);
        if (!localPlayer) { Sleep(1); continue; }

        uint32_t flags = Read<uint32_t>(localPlayer + client_dll::m_fFlags);
        bool onGround = (flags & (1 << 0));

        if (!(GetAsyncKeyState(VK_SPACE) & 0x8000)) {
            Write<DWORD>(gClient + offsets::jump, 256);
            Sleep(1);
            continue;
        }

        if (onGround) {
            Write<DWORD>(gClient + offsets::jump, 65537);
            Sleep(1);
            continue;
        }
        // В повітрі — скидаємо
        Write<DWORD>(gClient + offsets::jump, 256);
        Sleep(1);
    }
}
// ==================== Aimbot ====================

float g_aimSmooth = 8.0f;
float g_aimFov    = 180.0f;

Vector3 GetBonePos(uintptr_t pawn, int boneIndex) {
    uintptr_t gameSceneNode = Read<uintptr_t>(pawn + client_dll::m_pGameSceneNode);
    if (!gameSceneNode) return {};
    uintptr_t boneArray = Read<uintptr_t>(gameSceneNode + 0x1C0);
    if (!boneArray) return {};
    return Read<Vector3>(boneArray + boneIndex * 32);
}

void Thread_Aimbot() {
    while (true) {
        if (GetAsyncKeyState(VK_NUMPAD7) & 1) g_aimSmooth = std::max(1.0f, g_aimSmooth - 1.0f);
        if (GetAsyncKeyState(VK_NUMPAD8) & 1) g_aimSmooth += 1.0f;

        if (!F4press.load()) { Sleep(50); continue; }
        if (!gClient)        { Sleep(50); continue; }

        if (!(GetAsyncKeyState(VK_RBUTTON) & 0x8000)) { Sleep(1); continue; }

        uintptr_t localPlayer = Read<uintptr_t>(gClient + offsets::dwLocalPlayerPawn);
        if (!localPlayer) { Sleep(10); continue; }

        uint8_t myTeam = Read<uint8_t>(localPlayer + client_dll::m_iTeamNum);

        uintptr_t mySceneNode = Read<uintptr_t>(localPlayer + client_dll::m_pGameSceneNode);
        if (!mySceneNode) { Sleep(5); continue; }

        Vector3 myOrigin = Read<Vector3>(mySceneNode + client_dll::m_vecAbsOrigin);
        myOrigin.z += 64.0f;

        // Перевіряємо чи гравець присідає
        uint32_t myFlags = Read<uint32_t>(localPlayer + client_dll::m_fFlags);
        bool crouching = (myFlags & (1 << 1)); // біт присідання
        if (crouching) myOrigin.z -= 18.0f;    // корекція висоти очей

        ViewAngles myAngles = Read<ViewAngles>(gClient + offsets::dwViewAngles);

        uintptr_t entityList = Read<uintptr_t>(gClient + offsets::dwEntityList);
        if (!entityList) { Sleep(5); continue; }
        uintptr_t listEntry = Read<uintptr_t>(entityList + 0x10);
        if (!listEntry) { Sleep(5); continue; }

        float     bestFov  = g_aimFov;
        uintptr_t bestPawn = 0;

        for (int i = 0; i < 64; i++) {
            uintptr_t ctrl = Read<uintptr_t>(listEntry + (uintptr_t)0x70 * i);
            if (!ctrl) continue;
            int handle = Read<int32_t>(ctrl + client_dll::m_hPlayerPawn);
            if (!handle) continue;
            int index      = handle & 0x7FFF;
            int list_index = index >> 9;
            uintptr_t le2  = Read<uintptr_t>(entityList + 0x10 + (uintptr_t)0x8 * list_index);
            if (!le2) continue;
            uintptr_t pawn = Read<uintptr_t>(le2 + (uintptr_t)0x70 * (index & 0x1FF));
            if (!pawn || pawn == localPlayer) continue;

            int     hp        = Read<int32_t>(pawn + client_dll::m_iHealth);
            uint8_t team      = Read<uint8_t>(pawn + client_dll::m_iTeamNum);
            uint8_t lifeState = Read<uint8_t>(pawn + client_dll::m_lifeState);
            if (hp < 1 || hp > 100 || lifeState != 0 || team == myTeam) continue;

            Vector3 head = GetBonePos(pawn, 7);
            if (head.x == 0 && head.y == 0) continue;

            ViewAngles toEnemy = CalcAngle(myOrigin, head);
            float fovX = AngleDiff(toEnemy.yaw,   myAngles.yaw);
            float fovY = AngleDiff(toEnemy.pitch,  myAngles.pitch);
            float fov  = sqrtf(fovX * fovX + fovY * fovY);

            if (fov < bestFov) {
                bestFov  = fov;
                bestPawn = pawn;
            }
        }

        if (!bestPawn) { Sleep(1); continue; }

        Vector3 boneHead = GetBonePos(bestPawn, 7);
        if (boneHead.x == 0 && boneHead.y == 0) { Sleep(1); continue; }

        // Перевіряємо чи ворог присідає — читаємо його флаги
        uint32_t enemyFlags = Read<uint32_t>(bestPawn + client_dll::m_fFlags);
        bool enemyCrouching = (enemyFlags & (1 << 1));

        Vector3 aimPoint = boneHead;
        aimPoint.z += 3.0f; // підіймаємо завжди, підбирай число
        if (enemyCrouching) aimPoint.z += 4.0f;

        ViewAngles toEnemy = CalcAngle(myOrigin, aimPoint);

        float newPitch = myAngles.pitch + AngleDiff(toEnemy.pitch, myAngles.pitch) / g_aimSmooth;
        float newYaw   = myAngles.yaw   + AngleDiff(toEnemy.yaw,   myAngles.yaw)   / g_aimSmooth;

        ViewAngles newAngles;
        newAngles.pitch = newPitch;
        newAngles.yaw   = newYaw;
        newAngles.roll  = 0.0f;

        Write<ViewAngles>(gClient + offsets::dwViewAngles, newAngles);

        Sleep(1);
    }
}
// ==================== Точка входу ====================
int main() {
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);

    std::thread keyThread(keylistener);
    keyThread.detach();

    // std::thread dbg(debug_status);
    // dbg.detach();
    std::thread wh(wallhack);
    wh.detach();
    std::thread trigger(Thread_Triggerbot);
    trigger.detach();

    std::thread aim(Thread_Aimbot);
    aim.detach();

    std::thread bhop(bunnyhop);
    bhop.detach();



    console_view();

    while (true) Sleep(100);
    system("pause");
    return 0;
}