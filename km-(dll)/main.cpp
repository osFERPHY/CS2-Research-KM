#include <windows.h>
#include <cstdio>

// Оголошення функцій DLL
using fnLoadDriver = bool(*)(HANDLE*);
using fnUnloadDriver = void(*)(HANDLE);

// IOCTL коди (повинні збігатися)
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

int main()
{
    // 1. Завантажуємо DLL
    HMODULE hDll = LoadLibraryW(L"DriverLoader.dll");
    if (!hDll)
    {
        printf("LoadLibrary failed\n");
        return 1;
    }

    auto pLoadDriver = (fnLoadDriver)GetProcAddress(hDll, "LoadDriver");
    auto pUnloadDriver = (fnUnloadDriver)GetProcAddress(hDll, "UnloadDriver");

    if (!pLoadDriver || !pUnloadDriver)
    {
        printf("GetProcAddress failed\n");
        FreeLibrary(hDll);
        return 1;
    }

    // 2. Запускаємо драйвер
    HANDLE hDevice = INVALID_HANDLE_VALUE;
    if (!pLoadDriver(&hDevice))
    {
        printf("LoadDriver failed\n");
        FreeLibrary(hDll);
        return 1;
    }

    // 3. Тепер можемо взаємодіяти з драйвером через hDevice
    DWORD pid = GetCurrentProcessId();
    unsigned char buffer[16] = {0};
    ULONG_PTR address = (ULONG_PTR)&main;

    MEM_REQUEST req{ pid, 0, (UINT64)address, (UINT64)(uintptr_t)buffer, sizeof(buffer) };
    DWORD returned = 0;
    BOOL ok = DeviceIoControl(hDevice, IOCTL_MEM_READ, &req, sizeof(req),
        nullptr, 0, &returned, nullptr);
    if (ok)
    {
        printf("Read OK: ");
        for (int i = 0; i < 16; i++) printf("%02X ", buffer[i]);
        printf("\n");
    }
    else
    {
        printf("DeviceIoControl failed\n");
    }

    // 4. Вивантажуємо драйвер (закриваємо пристрій)
    pUnloadDriver(hDevice);

    FreeLibrary(hDll);
    system("pause");
    return 0;
}