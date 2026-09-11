#include "loader.h"
#include "driver_bytes.hpp"          // твій масив kernel_driver_bytes
#include "kdmapper/kdmapper.hpp"
#include "kdmapper/intel_driver.hpp"

#include <cstdio>
#include <cstring>

// ─── IOCTL коди (повинні збігатися з драйвером) ───
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

// ─── Глобальні змінні для зручності (не обов'язково) ───
static volatile bool g_running = false;

// ─── LoadDriver ────────────────────────────────────────────
bool LoadDriver(HANDLE* phDevice)
{
    if (!phDevice)
        return false;

    *phDevice = INVALID_HANDLE_VALUE;
    g_running = false;

    // 1. Завантажуємо вразливий Intel драйвер
    printf("[DLL] Loading Intel driver...\n");
    NTSTATUS status = intel_driver::Load();
    if (!NT_SUCCESS(status))
    {
        printf("[DLL] Intel load failed: 0x%lX\n", status);
        return false;
    }

    // 2. Мапимо наш драйвер
    printf("[DLL] Mapping driver...\n");
    NTSTATUS exitCode = 0;
    uint64_t result = kdmapper::MapDriver(
        (BYTE*)kernel_driver_bytes,
        0, 0,
        false,   // не звільняти образ після DriverEntry
        true,
        kdmapper::AllocationMode::AllocatePool,
        false, nullptr, &exitCode
    );

    // 3. Вивантажуємо Intel (він більше не потрібен)
    intel_driver::Unload();

    if (!result)
    {
        printf("[DLL] MapDriver failed (exitCode=0x%lX)\n", exitCode);
        return false;
    }

    printf("[DLL] Driver mapped at 0x%llX\n", result);

    // 4. Відкриваємо пристрій, перебираючи слоти 0..9
    for (int attempt = 0; attempt < 30; attempt++)
    {
        for (int slot = 0; slot < 10; slot++)
        {
            WCHAR name[32];
            swprintf_s(name, L"\\\\.\\AcpiDrvFlt%d", slot);
            HANDLE h = CreateFileW(name,
                GENERIC_READ | GENERIC_WRITE,
                0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (h != INVALID_HANDLE_VALUE)
            {
                *phDevice = h;
                g_running = true;
                printf("[DLL] Device opened (slot=%d)\n", slot);
                return true;
            }
        }
        Sleep(100);
    }

    printf("[DLL] Could not open any device\n");
    return false;
}

// ─── UnloadDriver ──────────────────────────────────────────
void UnloadDriver(HANDLE hDevice)
{
    if (hDevice != INVALID_HANDLE_VALUE)
    {
        CloseHandle(hDevice);
    }

    g_running = false;

    // Сам драйвер, завантажений через kdmapper, не може бути
    // вивантажений без перезавантаження системи.
    printf("[DLL] Device handle closed. Driver remains in kernel memory until reboot.\n");
}