#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "data.hpp"

#include <atomic>
#include <cmath>
#include <cstdio>
#include <thread>
#include <windows.h>
#include <gdiplus.h>
#include <map>
#include <memory>
#include <string>
#include <algorithm>

#pragma comment(lib, "gdiplus.lib")

using namespace Gdiplus;

extern EROverlayAPI *api;

namespace er::minimap::overlay_window {

static std::atomic_bool gRunning = false;
static std::thread gThread;
static HWND gHwnd = nullptr;

static bool gOverlayVisible = true;

// Hotkeys:
// F11 = toggle overlay visible/invisible
// F10 = close overlay window thread
static constexpr int HOTKEY_TOGGLE = 1;
static constexpr int HOTKEY_CLOSE = 2;

// Same Roundtable bounds used by render.cpp
static constexpr float roundTableMapX0 = 2740.f;
static constexpr float roundTableMapY0 = 7510.f;
static constexpr float roundTableMapX1 = 2940.f;
static constexpr float roundTableMapY1 = 7710.f;

// Color-key transparency color.
// Anything painted this exact color becomes invisible/click-through visually.
static constexpr COLORREF TRANSPARENT_KEY = RGB(1, 1, 1);

// map stuff
static ULONG_PTR gGdiPlusToken = 0;

static constexpr int TILE_SIZE = 1024;
static float gMapZoom = 1.0f;

// Same offsets as original render.cpp
static constexpr float dlcMapOffsetX = 3035.f;
static constexpr float dlcMapOffsetY = 1864.f;

// Tile cache so we do not reload PNGs every frame.
static std::map<std::wstring, std::unique_ptr<Image>> gTileCache;

// enemy & icons
static std::unique_ptr<Image> gAtlasImage;
static bool gAtlasTriedLoad = false;

static float gTargetRadarScale = 1.0f;
static float gNpcRadarRadius = 80.0f;
static float gOverlayPlayerIconScale = 1.0f;
static float gOverlayEnemyDotScale = 1.0f;
static float gOverlayBossDotScale = 1.5f;

static bool isRoundtable() {
    const auto &location = er::minimap::gData.location();

    return location.mapId == 0 && location.x >= roundTableMapX0 && location.x < roundTableMapX1 && location.y >= roundTableMapY0 && location.y < roundTableMapY1;
}

static bool shouldDrawOverlay() {
    const auto &location = er::minimap::gData.location();

    if (!gOverlayVisible) {
        return false;
    }

    // Same idea as original plugin:
    // ESC/menu/inventory/map/etc. should hide overlay.
    if (er::minimap::gData.onGUI()) {
        return false;
    }

    if (location.x == 0.f) {
        return false;
    }

    if (isRoundtable()) {
        return false;
    }

    return true;
}

static HWND findEldenRingWindow() {
    DWORD currentPid = GetCurrentProcessId();
    HWND found = nullptr;

    EnumWindows(
        [](HWND hwnd, LPARAM lParam) -> BOOL {
            DWORD pid = 0;
            GetWindowThreadProcessId(hwnd, &pid);

            if (pid != GetCurrentProcessId()) {
                return TRUE;
            }

            if (!IsWindowVisible(hwnd)) {
                return TRUE;
            }

            wchar_t title[256] = {};
            GetWindowTextW(hwnd, title, 256);

            // Avoid grabbing our own overlay window.
            if (wcsstr(title, L"ER Map Only Overlay Test") != nullptr) {
                return TRUE;
            }

            if (wcsstr(title, L"ELDEN RING") != nullptr) {
                *reinterpret_cast<HWND *>(lParam) = hwnd;
                return FALSE;
            }

            return TRUE;
        },
        reinterpret_cast<LPARAM>(&found));

    return found;
}

static void makeFullscreenBorderless(HWND hwnd) {
    HWND gameWindow = findEldenRingWindow();
    HMONITOR monitor = MonitorFromWindow(gameWindow ? gameWindow : hwnd, MONITOR_DEFAULTTONEAREST);

    MONITORINFO mi = {};
    mi.cbSize = sizeof(mi);
    GetMonitorInfoW(monitor, &mi);

    // rcMonitor = full physical monitor, including taskbar area.
    RECT r = mi.rcMonitor;

    SetWindowLongW(hwnd, GWL_STYLE, WS_POPUP | WS_VISIBLE);

    SetWindowLongW(hwnd, GWL_EXSTYLE, WS_EX_TOPMOST | WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_NOACTIVATE | WS_EX_APPWINDOW);

    SetLayeredWindowAttributes(hwnd, TRANSPARENT_KEY, 255, LWA_COLORKEY | LWA_ALPHA);

    SetWindowPos(hwnd, HWND_TOPMOST, r.left, r.top, r.right - r.left, r.bottom - r.top, SWP_FRAMECHANGED | SWP_SHOWWINDOW | SWP_NOACTIVATE);
}

static std::wstring getLayerFolder(int layer) {
    switch (layer) {
        case 0:
            return L"00";
        case 1:
            return L"01";
        case 2:
            return L"02";
        default:
            return L"00";
    }
}

static bool getMapDrawState(int &outLayer, float &outMapX, float &outMapY) {
    const auto &location = er::minimap::gData.location();

    switch (location.mapId) {
        case 0:
            outLayer = (location.underground & 1) ? 1 : 0;
            outMapX = location.x;
            outMapY = location.y;
            return true;

        case 10:
            outLayer = 2;
            outMapX = location.x - dlcMapOffsetX;
            outMapY = location.y - dlcMapOffsetY;
            return true;

        default:
            return false;
    }
}

static Image *loadTileImage(int layer, int tileX, int tileY) {
    if (tileX < 0 || tileX > 9 || tileY < 0 || tileY > 9) {
        return nullptr;
    }

    wchar_t path[MAX_PATH];

    const std::wstring layerFolder = getLayerFolder(layer);

    swprintf_s(path, L"%ls\\data\\map\\%s\\%d_%d.png", ::api->getModulePath(), layerFolder.c_str(), tileX, tileY);

    std::wstring key = path;

    auto it = gTileCache.find(key);
    if (it != gTileCache.end()) {
        return it->second.get();
    }

    auto img = std::make_unique<Image>(path);

    if (img->GetLastStatus() != Ok) {
        return nullptr;
    }

    Image *raw = img.get();
    gTileCache.emplace(key, std::move(img));
    return raw;
}

static void drawRotatedImage(Graphics &graphics, Image *image, float centerX, float centerY, float tileOffsetX, float tileOffsetY, float drawW, float drawH, float cosRot,
                             float sinRot) {
    PointF dest[3];

    // Top-left
    dest[0] = PointF(centerX + tileOffsetX * cosRot - tileOffsetY * sinRot, centerY + tileOffsetX * sinRot + tileOffsetY * cosRot);

    // Top-right
    dest[1] = PointF(centerX + (tileOffsetX + drawW) * cosRot - tileOffsetY * sinRot, centerY + (tileOffsetX + drawW) * sinRot + tileOffsetY * cosRot);

    // Bottom-left
    dest[2] = PointF(centerX + tileOffsetX * cosRot - (tileOffsetY + drawH) * sinRot, centerY + tileOffsetX * sinRot + (tileOffsetY + drawH) * cosRot);

    graphics.DrawImage(image, dest, 3);
}

static Image *loadAtlasImage() {
    if (gAtlasImage) {
        return gAtlasImage.get();
    }

    if (gAtlasTriedLoad) {
        return nullptr;
    }

    gAtlasTriedLoad = true;

    wchar_t path[MAX_PATH];

    swprintf_s(path, L"%ls\\data\\map\\atlas.png", ::api->getModulePath());

    auto img = std::make_unique<Image>(path);

    if (img->GetLastStatus() != Ok) {
        return nullptr;
    }

    gAtlasImage = std::move(img);
    return gAtlasImage.get();
}

static void drawAtlasSprite(Graphics &graphics, const er::minimap::SpriteInfo *sprite, float centerX, float centerY, float scale) {
    if (!sprite) {
        return;
    }

    Image *atlas = loadAtlasImage();
    if (!atlas) {
        return;
    }

    const float width = sprite->width * scale;
    const float height = sprite->height * scale;
    const float cx = sprite->centerX * scale;
    const float cy = sprite->centerY * scale;

    RectF dst(centerX - cx, centerY - cy, width, height);

    graphics.DrawImage(atlas, dst, static_cast<float>(sprite->x), static_cast<float>(sprite->y), static_cast<float>(sprite->width), static_cast<float>(sprite->height), UnitPixel);
}

static void drawAtlasSpriteRotated(Graphics &graphics, const er::minimap::SpriteInfo *sprite, float centerX, float centerY, float scale, float radians) {
    if (!sprite) {
        return;
    }

    Image *atlas = loadAtlasImage();
    if (!atlas) {
        return;
    }

    GraphicsState state = graphics.Save();

    graphics.TranslateTransform(centerX, centerY);
    graphics.RotateTransform(radians * 180.0f / 3.14159265f);

    const float width = sprite->width * scale;
    const float height = sprite->height * scale;
    const float cx = sprite->centerX * scale;
    const float cy = sprite->centerY * scale;

    RectF dst(-cx, -cy, width, height);

    graphics.DrawImage(atlas, dst, static_cast<float>(sprite->x), static_cast<float>(sprite->y), static_cast<float>(sprite->width), static_cast<float>(sprite->height), UnitPixel);

    graphics.Restore(state);
}

static void drawPlayerIcon(HDC hdc, const RECT &rc) {
    Graphics graphics(hdc);
    graphics.SetInterpolationMode(InterpolationModeHighQualityBilinear);
    graphics.SetPixelOffsetMode(PixelOffsetModeHalf);

    const int w = rc.right - rc.left;
    const int h = rc.bottom - rc.top;

    const float centerX = w * 0.5f;
    const float centerY = h * 0.5f;

    const auto *playerSprite = er::minimap::gAtlas.findSprite("Player");
    const auto *arrowSprite = er::minimap::gAtlas.findSprite("Arrow");

    const float iconScale = gMapZoom * 0.45f * gOverlayPlayerIconScale;

    // Player body/circle
    drawAtlasSprite(graphics, playerSprite, centerX, centerY, iconScale);

    // Original plugin behavior:
    // map rotation = camera orientation
    // player arrow = body orientation adjusted by map rotation
    const auto &location = er::minimap::gData.location();
    const auto &camera = er::minimap::gData.camera();

    const float mapRad = std::atan2(camera.yawSin, camera.yawCos);
    const float bodyRad = location.oriDeg * 3.14159265f / 180.0f;

    float arrowRad = bodyRad + mapRad;

    drawAtlasSpriteRotated(graphics, arrowSprite, centerX, centerY, iconScale, arrowRad);
}

static void drawEnemyDots(HDC hdc, const RECT &rc) {
    const int w = rc.right - rc.left;
    const int h = rc.bottom - rc.top;

    const float centerX = w * 0.5f;
    const float centerY = h * 0.5f;

    const float playerX = er::minimap::gData.playerCombatX();
    const float playerZ = er::minimap::gData.playerCombatZ();

    if (playerX == 0.f && playerZ == 0.f) {
        return;
    }

    const auto &camera = er::minimap::gData.camera();

    const float cosMapRot = camera.yawCos;
    const float sinMapRot = camera.yawSin;

    const auto &npcs = er::minimap::gData.nearbyNpcs();
    const auto &target = er::minimap::gData.target();

    for (const auto &npc: npcs) {
        if (npc.distance > gNpcRadarRadius) {
            continue;
        }

        const bool isCurrentTarget = target.valid && npc.address == target.address;
        const bool targetIsBoss = target.alliance == 7;

        // Same behavior as old plugin:
        // don't draw small dot if full target icon would be used later.
        if (isCurrentTarget && (targetIsBoss || target.activeLock)) {
            continue;
        }

        const float relX = npc.x - playerX;
        const float relZ = npc.z - playerZ;

        float markerX = relX;
        float markerY = -relZ;

        // Rotate with map.
        const float x = markerX;
        const float y = markerY;

        markerX = x * cosMapRot - y * sinMapRot;
        markerY = x * sinMapRot + y * cosMapRot;

        const float screenRelX = markerX * gMapZoom * gTargetRadarScale;
        const float screenRelY = markerY * gMapZoom * gTargetRadarScale;

        const float sx = centerX + screenRelX;
        const float sy = centerY + screenRelY;

        if (sx < -50.f || sy < -50.f || sx > w + 50.f || sy > h + 50.f) {
            continue;
        }

        const bool isBoss = npc.alliance == 7;

        float radius = isBoss ? 7.0f * gOverlayBossDotScale : 4.0f * gOverlayEnemyDotScale;

        // Height tint-ish behavior.
        const float heightDiff = npc.y - er::minimap::gData.playerCombatY();

        COLORREF color;

        if (heightDiff <= -6.0f) {
            color = isBoss ? RGB(150, 20, 20) : RGB(120, 10, 10);
        } else if (heightDiff >= 7.5f) {
            color = isBoss ? RGB(255, 110, 70) : RGB(255, 80, 50);
        } else {
            color = isBoss ? RGB(255, 50, 50) : RGB(255, 30, 30);
        }

        HBRUSH brush = CreateSolidBrush(color);
        HGDIOBJ oldBrush = SelectObject(hdc, brush);

        HPEN pen = CreatePen(PS_SOLID, 1, RGB(255, 160, 160));
        HGDIOBJ oldPen = SelectObject(hdc, pen);

        Ellipse(hdc, static_cast<int>(sx - radius), static_cast<int>(sy - radius), static_cast<int>(sx + radius), static_cast<int>(sy + radius));

        SelectObject(hdc, oldPen);
        DeleteObject(pen);

        SelectObject(hdc, oldBrush);
        DeleteObject(brush);
    }
}

static void drawTargetMarker(HDC hdc, const RECT &rc) {
    Graphics graphics(hdc);
    graphics.SetInterpolationMode(InterpolationModeHighQualityBilinear);
    graphics.SetPixelOffsetMode(PixelOffsetModeHalf);

    const int w = rc.right - rc.left;
    const int h = rc.bottom - rc.top;

    const float centerX = w * 0.5f;
    const float centerY = h * 0.5f;

    const float playerX = er::minimap::gData.playerCombatX();
    const float playerZ = er::minimap::gData.playerCombatZ();

    if (playerX == 0.f && playerZ == 0.f) {
        return;
    }

    const auto &target = er::minimap::gData.target();

    if (!target.valid) {
        return;
    }

    // Same old logic:
    // Normal enemies only get the full arrow while actively locked-on.
    // Bosses keep the full icon after lock-off.
    if (target.alliance != 7 && !target.activeLock) {
        return;
    }

    const float relX = target.x - playerX;
    const float relZ = target.z - playerZ;

    float markerX = relX;
    float markerY = -relZ;

    const auto &camera = er::minimap::gData.camera();

    const float cosMapRot = camera.yawCos;
    const float sinMapRot = camera.yawSin;

    // Same rotation logic as enemy dots.
    const float x = markerX;
    const float y = markerY;

    markerX = x * cosMapRot - y * sinMapRot;
    markerY = x * sinMapRot + y * cosMapRot;

    const float screenRelX = markerX * gMapZoom * gTargetRadarScale;
    const float screenRelY = markerY * gMapZoom * gTargetRadarScale;

    const float targetX = centerX + screenRelX;
    const float targetY = centerY + screenRelY;

    if (targetX < -100.f || targetY < -100.f || targetX > w + 100.f || targetY > h + 100.f) {
        return;
    }

    const auto *playerSprite = er::minimap::gAtlas.findSprite("Player");
    const auto *arrowSprite = er::minimap::gAtlas.findSprite("Arrow");

    const bool isBoss = target.alliance == 7;

    // Similar to old renderTargetMarker():
    // Bosses bigger, normal locked enemies slightly smaller.
    const float enemyScale = gMapZoom * 0.45f * gOverlayPlayerIconScale * (isBoss ? 1.10f : 0.85f);

    // If atlas sprites are available, draw red-tinted target body + arrow.
    // GDI+ does not tint the atlas sprite as easily as ImGui did,
    // so for now we draw the normal sprite plus a red ring.
    if (playerSprite && arrowSprite) {
        drawAtlasSprite(graphics, playerSprite, targetX, targetY, enemyScale);

        // Enemy arrow points toward the player icon, same band-aid behavior as the old version.
        const float dirX = centerX - targetX;
        const float dirY = centerY - targetY;

        float enemyRad = std::atan2(dirY, dirX) + 3.14159265f / 2.0f;

        drawAtlasSpriteRotated(graphics, arrowSprite, targetX, targetY, enemyScale, enemyRad);
    }

    // Red outline so target is obvious even if atlas tint is not implemented yet.
    {
        const float radius = isBoss ? 22.0f : 16.0f;

        HPEN pen = CreatePen(PS_SOLID, isBoss ? 4 : 3, RGB(255, 30, 30));
        HGDIOBJ oldPen = SelectObject(hdc, pen);

        HBRUSH hollow = static_cast<HBRUSH>(GetStockObject(NULL_BRUSH));
        HGDIOBJ oldBrush = SelectObject(hdc, hollow);

        Ellipse(hdc, static_cast<int>(targetX - radius), static_cast<int>(targetY - radius), static_cast<int>(targetX + radius), static_cast<int>(targetY + radius));

        SelectObject(hdc, oldBrush);
        SelectObject(hdc, oldPen);

        DeleteObject(pen);
    }
}

static void drawMapTiles7x7(HDC hdc, const RECT &rc) {
    int layer = 0;
    float mapX = 0.f;
    float mapY = 0.f;

    if (!getMapDrawState(layer, mapX, mapY)) {
        return;
    }

    const int w = rc.right - rc.left;
    const int h = rc.bottom - rc.top;

    const float centerX = w * 0.5f;
    const float centerY = h * 0.5f;

    const int baseTileX = static_cast<int>(std::floor(mapX / TILE_SIZE));
    const int baseTileY = static_cast<int>(std::floor(mapY / TILE_SIZE));

    Graphics graphics(hdc);

    // NearestNeighbor is ugly but useful for testing performance.
    // If it is smooth, switch to HighQualityBilinear later.
    graphics.SetInterpolationMode(InterpolationModeNearestNeighbor);
    graphics.SetPixelOffsetMode(PixelOffsetModeHalf);

    const auto &camera = er::minimap::gData.camera();

    float cosRot = camera.yawCos;
    float sinRot = camera.yawSin;

    // If rotation is backwards, flip these two signs.
    // cosRot = camera.yawCos;
    // sinRot = camera.yawSin;

    const float drawSize = TILE_SIZE * gMapZoom;

    // 7x7 tile grid around player.
    for (int ty = baseTileY - 2; ty <= baseTileY + 2; ty++) {
        for (int tx = baseTileX - 2; tx <= baseTileX + 2; tx++) {
            Image *tile = loadTileImage(layer, tx, ty);
            if (!tile) {
                continue;
            }

            const float tileWorldX = static_cast<float>(tx * TILE_SIZE);
            const float tileWorldY = static_cast<float>(ty * TILE_SIZE);

            const float tileOffsetX = (tileWorldX - mapX) * gMapZoom;
            const float tileOffsetY = (tileWorldY - mapY) * gMapZoom;

            drawRotatedImage(graphics, tile, centerX, centerY, tileOffsetX, tileOffsetY, drawSize, drawSize, cosRot, sinRot);
        }
    }
}

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_CREATE:
            makeFullscreenBorderless(hwnd);

            // System-wide hotkeys still work even though the overlay is click-through/no-activate.
            RegisterHotKey(hwnd, HOTKEY_TOGGLE, 0, VK_F11);
            RegisterHotKey(hwnd, HOTKEY_CLOSE, 0, VK_F10);

            SetTimer(hwnd, 1, 16, nullptr); // ~60 FPS repaint
            return 0;

        case WM_HOTKEY:
            if (wParam == HOTKEY_TOGGLE) {
                gOverlayVisible = !gOverlayVisible;
                InvalidateRect(hwnd, nullptr, FALSE);
                return 0;
            }

            if (wParam == HOTKEY_CLOSE) {
                gRunning = false;
                DestroyWindow(hwnd);
                return 0;
            }

            return 0;

        case WM_MOUSEACTIVATE:
            return MA_NOACTIVATE;

        case WM_NCHITTEST:
            return HTTRANSPARENT;

        case WM_TIMER:
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;

        case WM_ERASEBKGND:
            // Prevent flicker.
            return 1;

        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC windowHdc = BeginPaint(hwnd, &ps);

            RECT rc;
            GetClientRect(hwnd, &rc);

            const int w = rc.right - rc.left;
            const int h = rc.bottom - rc.top;

            // Double buffer to avoid flicker.
            HDC hdc = CreateCompatibleDC(windowHdc);
            HBITMAP backBitmap = CreateCompatibleBitmap(windowHdc, w, h);
            HGDIOBJ oldBitmap = SelectObject(hdc, backBitmap);

            const bool drawOverlay = shouldDrawOverlay();

            if (!drawOverlay) {
                // Fully invisible overlay when ESC/menu/inventory/Roundtable/etc.
                HBRUSH clearBrush = CreateSolidBrush(TRANSPARENT_KEY);
                FillRect(hdc, &rc, clearBrush);
                DeleteObject(clearBrush);
            } else {
                // Normal active state:
                // Fullscreen black background for now.
                // Later this full area becomes the fullscreen map.
                HBRUSH bgBrush = CreateSolidBrush(RGB(0, 0, 0));
                FillRect(hdc, &rc, bgBrush);
                DeleteObject(bgBrush);

                drawMapTiles7x7(hdc, rc);
                drawEnemyDots(hdc, rc);
                drawTargetMarker(hdc, rc);
                drawPlayerIcon(hdc, rc);

                RECT panel = rc;
                panel.left = 20;
                panel.top = 20;
                panel.right = 680;
                panel.bottom = 230;

                SetBkMode(hdc, TRANSPARENT);
                SetTextColor(hdc, RGB(255, 255, 255));

                const auto &loc = er::minimap::gData.location();
                const auto &cam = er::minimap::gData.camera();

                const float yawRad = std::atan2(cam.yawSin, cam.yawCos) + 3.14159265f;
                const float yawDeg = yawRad * 180.0f / 3.14159265f;

                wchar_t buf[512];

                swprintf_s(buf,
                           L"ER Map Overlay Window Test\n\n"
                           L"F11: Toggle overlay | F10: Close overlay\n\n"
                           L"MapId: %d\n"
                           L"Map XY: %.2f, %.2f\n"
                           L"Player Combat XYZ: %.2f, %.2f, %.2f\n"
                           L"Camera Cos/Sin: %.4f, %.4f\n"
                           L"Camera Deg: %.2f\n"
                           L"onGUI/menu: %s\n",
                           loc.mapId, loc.x, loc.y, er::minimap::gData.playerCombatX(), er::minimap::gData.playerCombatY(), er::minimap::gData.playerCombatZ(), cam.yawCos,
                           cam.yawSin, yawDeg, er::minimap::gData.onGUI() ? L"YES" : L"NO");

                RECT textRect = panel;
                textRect.left += 10;
                textRect.top += 8;
                DrawTextW(hdc, buf, -1, &textRect, DT_LEFT | DT_TOP | DT_NOPREFIX);

                //// Center test marker.
                //// This should now actually be centered on the full overlay window.
                //const int cx = w / 2;
                //const int cy = h / 2;
                //const int len = 140;

                //const int x2 = cx + static_cast<int>(std::cos(yawRad) * len);
                //const int y2 = cy + static_cast<int>(std::sin(yawRad) * len);

                //HPEN pen = CreatePen(PS_SOLID, 6, RGB(255, 80, 80));
                //HGDIOBJ oldPen = SelectObject(hdc, pen);

                //MoveToEx(hdc, cx, cy, nullptr);
                //LineTo(hdc, x2, y2);

                //SelectObject(hdc, oldPen);
                //DeleteObject(pen);

                //HBRUSH dot = CreateSolidBrush(RGB(255, 255, 0));
                //HGDIOBJ oldBrush = SelectObject(hdc, dot);
                //Ellipse(hdc, cx - 9, cy - 9, cx + 9, cy + 9);
                /*SelectObject(hdc, oldBrush);
                DeleteObject(dot);*/
            }

            BitBlt(windowHdc, 0, 0, w, h, hdc, 0, 0, SRCCOPY);

            SelectObject(hdc, oldBitmap);
            DeleteObject(backBitmap);
            DeleteDC(hdc);

            EndPaint(hwnd, &ps);
            return 0;
        }

        case WM_CLOSE:
            gRunning = false;
            DestroyWindow(hwnd);
            return 0;

        case WM_DESTROY:
            UnregisterHotKey(hwnd, HOTKEY_TOGGLE);
            UnregisterHotKey(hwnd, HOTKEY_CLOSE);

            gHwnd = nullptr;
            PostQuitMessage(0);
            return 0;
    }

    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

static void threadMain() {
    GdiplusStartupInput gdiplusStartupInput;
    GdiplusStartup(&gGdiPlusToken, &gdiplusStartupInput, nullptr);

    HINSTANCE hInstance = GetModuleHandleW(nullptr);

    gMapZoom = ::api->configGetFloat("minimap.overlay_map_zoom", 1.0f);
    gTargetRadarScale = ::api->configGetFloat("minimap.target_radar_scale", 1.0f);
    gNpcRadarRadius = ::api->configGetFloat("minimap.npc_radar_radius", 80.0f);
    gOverlayPlayerIconScale = ::api->configGetFloat("minimap.overlay_player_icon_scale", 1.0f);
    gOverlayEnemyDotScale = ::api->configGetFloat("minimap.overlay_enemy_dot_scale", 1.0f);
    gOverlayBossDotScale = ::api->configGetFloat("minimap.overlay_boss_dot_scale", 1.5f);

    const wchar_t CLASS_NAME[] = L"ERMapOnlyPluginOverlayWindow";

    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = CLASS_NAME;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);

    RegisterClassExW(&wc);

    // Initial size does not matter much because WM_CREATE immediately fullscreen-borderlesses it.
    gHwnd = CreateWindowExW(WS_EX_TOPMOST | WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_NOACTIVATE | WS_EX_APPWINDOW, CLASS_NAME, L"ER Map Only Overlay Test", WS_POPUP, 0, 0, 800,
                            600, nullptr, nullptr, hInstance, nullptr);

    if (!gHwnd) {
        gRunning = false;
        return;
    }

    ShowWindow(gHwnd, SW_SHOW);
    UpdateWindow(gHwnd);

    MSG msg = {};
    while (gRunning && GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    if (gHwnd) {
        DestroyWindow(gHwnd);
        gHwnd = nullptr;
    }

    UnregisterClassW(CLASS_NAME, hInstance);

    gTileCache.clear();
    gAtlasImage.reset();
    gAtlasTriedLoad = false;

    if (gGdiPlusToken != 0) {
        GdiplusShutdown(gGdiPlusToken);
        gGdiPlusToken = 0;
    }

    gRunning = false;
}

static void start() {
    if (gRunning) {
        return;
    }

    gRunning = true;
    gThread = std::thread(threadMain);
}

static void stop() {
    if (!gRunning) {
        return;
    }

    gRunning = false;

    if (gHwnd) {
        PostMessageW(gHwnd, WM_CLOSE, 0, 0);
    }

    if (gThread.joinable()) {
        gThread.join();
    }
}

} // namespace er::minimap::overlay_window
