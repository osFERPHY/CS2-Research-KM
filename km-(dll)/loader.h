#pragma once

#include <windows.h>

// Макрос для експорту/імпорту
#ifdef DRIVERLOADER_EXPORTS
#define DRIVERLOADER_API __declspec(dllexport)
#else
#define DRIVERLOADER_API __declspec(dllimport)
#endif

extern "C" {
// Завантажує драйвер через kdmapper і відкриває пристрій.
// Повертає true при успіху, а через phDevice записує HANDLE.
DRIVERLOADER_API bool LoadDriver(HANDLE* phDevice);

// Закриває HANDLE і повідомляє, що драйвер залишається в пам'яті.
DRIVERLOADER_API void UnloadDriver(HANDLE hDevice);
}