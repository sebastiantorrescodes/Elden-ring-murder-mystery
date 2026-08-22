#include "GameVersion.h"

#include <windows.h>
#include <vector>

uint64_t GetGameVersion() {
    wchar_t exePath[MAX_PATH];
    if (GetModuleFileNameW(nullptr, exePath, MAX_PATH) == 0) {
        return 0;
    }

    DWORD handle = 0;
    DWORD size = GetFileVersionInfoSizeW(exePath, &handle);
    if (size == 0) {
        return 0;
    }

    std::vector<uint8_t> buffer(size);
    if (!GetFileVersionInfoW(exePath, handle, size, buffer.data())) {
        return 0;
    }

    VS_FIXEDFILEINFO* fileInfo = nullptr;
    UINT len = 0;
    if (!VerQueryValueW(buffer.data(), L"\\", reinterpret_cast<LPVOID*>(&fileInfo), &len) || fileInfo == nullptr) {
        return 0;
    }

    return (static_cast<uint64_t>(fileInfo->dwFileVersionMS) << 32) | fileInfo->dwFileVersionLS;
}
