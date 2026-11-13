// Copyright (c) 2026 Red Marmoset Studios Ltd.
// Windows screen capture with GDI: copy the whole virtual desktop into a 32-bit buffer each grab.
//
// This is the straightforward, reliable way to read the screen, good enough for the lossless-still picture of
// Phase 1. The faster path (Desktop Duplication, which hands over only the changed region on the GPU) belongs
// with the video work in a later phase; this keeps the first Windows server simple and correct.
//
// NOTE: not yet built or run on Windows from this environment (the cloud build is Linux). It needs a build on a
// Windows machine, and its dirty-rectangle handling is deliberately coarse (whole screen, or nothing when the
// picture hasn't changed) until Desktop Duplication replaces it.
//
// The monitor layout (EnumDisplayMonitors), the mode change (ChangeDisplaySettingsEx) and the pointer's image and
// position (GetCursorInfo, GetIconInfo, DrawIconEx) were added in the same state: written to the Win32 documentation
// and mirroring the Linux version, never compiled or run on Windows. Check them first on a real machine.

#include "SystemScreen.h"

#include "DesktopSwitch.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace Marconi
{
    namespace Server
    {
        namespace
        {
            class WindowsScreen : public ScreenSource
            {
            public:
                ~WindowsScreen() override
                {
                    Release();
                }

                bool Open(std::string& error)
                {
                    m_screenDc = GetDC(nullptr);
                    if (m_screenDc == nullptr)
                    {
                        error = "could not get the screen device context";
                        return false;
                    }
                    m_memoryDc = CreateCompatibleDC(m_screenDc);
                    if (m_memoryDc == nullptr)
                    {
                        error = "could not create a memory device context";
                        Release();
                        return false;
                    }
                    return true;
                }

                // Every monitor, primary first, placed relative to the virtual desktop's top-left corner (which is
                // where the capture starts). A monitor's id is the number in its device name (\\.\DISPLAY2 is 2),
                // which stays the same when its mode changes.
                std::vector<Wire::Display> GetDisplays() override
                {
                    MeasureVirtualScreen();
                    std::vector<MonitorEntry> monitors = EnumerateMonitors();
                    std::vector<Wire::Display> displays;
                    for (const MonitorEntry& monitor : monitors)
                    {
                        Wire::Display display;
                        display.id = monitor.id;
                        display.x = monitor.rect.left - m_originX;
                        display.y = monitor.rect.top - m_originY;
                        display.width = monitor.rect.right - monitor.rect.left;
                        display.height = monitor.rect.bottom - monitor.rect.top;
                        if (monitor.primary)
                        {
                            displays.insert(displays.begin(), display);
                        }
                        else
                        {
                            displays.push_back(display);
                        }
                    }
                    if (displays.empty())
                    {
                        Wire::Display display;
                        display.id = 1;
                        display.width = m_width;
                        display.height = m_height;
                        displays.push_back(display);
                    }
                    return displays;
                }

                bool CanSetDisplayMode() override { return true; }

                // Changes the monitor to the listed mode nearest width x height (same colour depth and
                // orientation, never a mode the monitor doesn't list), for this session only: the registry's saved
                // mode is untouched, and RestoreDisplayModes puts it back.
                Wire::DisplayModeResult::Status SetDisplayMode(uint32_t displayId, int32_t width,
                                                               int32_t height) override
                {
                    using Status = Wire::DisplayModeResult::Status;
                    if (width <= 0 || height <= 0)
                    {
                        return Status::NotListed;
                    }
                    const MonitorEntry* found = nullptr;
                    std::vector<MonitorEntry> monitors = EnumerateMonitors();
                    for (const MonitorEntry& monitor : monitors)
                    {
                        if (monitor.id == displayId)
                        {
                            found = &monitor;
                        }
                    }
                    if (found == nullptr)
                    {
                        return Status::NoSuchDisplay;
                    }
                    const wchar_t* device = found->device;
                    DEVMODEW current = {};
                    current.dmSize = sizeof(current);
                    if (!EnumDisplaySettingsW(device, ENUM_CURRENT_SETTINGS, &current))
                    {
                        return Status::Failed;
                    }
                    DEVMODEW best = {};
                    long bestDistance = -1;
                    DEVMODEW mode = {};
                    mode.dmSize = sizeof(mode);
                    for (DWORD i = 0; EnumDisplaySettingsExW(device, i, &mode, 0); ++i)
                    {
                        if (mode.dmBitsPerPel != current.dmBitsPerPel ||
                            mode.dmDisplayOrientation != current.dmDisplayOrientation)
                        {
                            continue;
                        }
                        long distance = std::labs(static_cast<long>(mode.dmPelsWidth) - width) +
                                        std::labs(static_cast<long>(mode.dmPelsHeight) - height);
                        // Among modes of the same size, keep the one at the current refresh rate.
                        if (bestDistance < 0 || distance < bestDistance ||
                            (distance == bestDistance && mode.dmDisplayFrequency == current.dmDisplayFrequency))
                        {
                            best = mode;
                            bestDistance = distance;
                        }
                    }
                    if (bestDistance < 0)
                    {
                        return Status::NotListed;
                    }
                    if (best.dmPelsWidth == current.dmPelsWidth && best.dmPelsHeight == current.dmPelsHeight)
                    {
                        return Status::Ok; // already that size
                    }
                    best.dmFields = DM_PELSWIDTH | DM_PELSHEIGHT | DM_BITSPERPEL | DM_DISPLAYFREQUENCY;
                    if (ChangeDisplaySettingsExW(device, &best, nullptr, CDS_TEST, nullptr) != DISP_CHANGE_SUCCESSFUL)
                    {
                        return Status::NotListed;
                    }
                    LONG result = ChangeDisplaySettingsExW(device, &best, nullptr, 0, nullptr);
                    if (result != DISP_CHANGE_SUCCESSFUL)
                    {
                        return result == DISP_CHANGE_BADMODE ? Status::NotListed : Status::Failed;
                    }
                    if (std::find(m_changedDevices.begin(), m_changedDevices.end(), std::wstring(device)) ==
                        m_changedDevices.end())
                    {
                        m_changedDevices.push_back(device);
                    }
                    return Status::Ok;
                }

                // A null mode restores the mode saved in the registry, which we never touched.
                void RestoreDisplayModes() override
                {
                    for (const std::wstring& device : m_changedDevices)
                    {
                        ChangeDisplaySettingsExW(device.c_str(), nullptr, nullptr, 0, nullptr);
                    }
                    m_changedDevices.clear();
                }

                // True only when the pointer's image (or whether it's showing) is new since the last true.
                bool GetCursor(Cursor& out) override
                {
                    CURSORINFO info = {};
                    info.cbSize = sizeof(info);
                    if (!GetCursorInfo(&info))
                    {
                        return false;
                    }
                    const bool showing = (info.flags & CURSOR_SHOWING) != 0;
                    HCURSOR handle = showing ? info.hCursor : nullptr;
                    if (m_cursorSent && handle == m_lastCursor)
                    {
                        return false;
                    }
                    if (handle == nullptr)
                    {
                        m_cursorSent = true;
                        m_lastCursor = nullptr;
                        out = Cursor(); // hidden
                        return true;
                    }
                    if (!RenderCursor(handle, out))
                    {
                        return false; // try again next time
                    }
                    m_cursorSent = true;
                    m_lastCursor = handle;
                    return true;
                }

                bool GetCursorPosition(int32_t& x, int32_t& y) override
                {
                    CURSORINFO info = {};
                    info.cbSize = sizeof(info);
                    if (!GetCursorInfo(&info))
                    {
                        return false;
                    }
                    x = info.ptScreenPos.x - m_originX;
                    y = info.ptScreenPos.y - m_originY;
                    return true;
                }

                bool Grab(Capture& out) override
                {
                    // Follow the desktop that receives input (UAC prompt, lock screen): the device contexts belong
                    // to the desktop they were taken on, so they are taken again after a switch.
                    if (FollowInputDesktop())
                    {
                        std::string ignored;
                        Release();
                        Open(ignored);
                    }
                    MeasureVirtualScreen();
                    if (m_width <= 0 || m_height <= 0)
                    {
                        return false;
                    }
                    if (!EnsureBitmap())
                    {
                        return false;
                    }

                    // Copy the virtual desktop (which may start at a negative coordinate on multi-monitor setups)
                    // into our top-left-origin bitmap.
                    if (!BitBlt(m_memoryDc, 0, 0, m_width, m_height, m_screenDc, m_originX, m_originY, SRCCOPY))
                    {
                        return false;
                    }

                    out.width = m_width;
                    out.height = m_height;
                    out.pixels.resize(static_cast<size_t>(m_width) * m_height);
                    // The DIB section is 32-bit BGRX, bottom-up unless the height is negative; we set a top-down
                    // DIB in EnsureBitmap, so row 0 is the top. Each pixel is already 0x00RRGGBB in memory.
                    std::memcpy(out.pixels.data(), m_bits, out.pixels.size() * 4);

                    out.dirty.clear();
                    if (m_previous.size() == out.pixels.size() && m_previous == out.pixels)
                    {
                        return true; // nothing changed: no rectangles, so the session sends nothing
                    }
                    // Coarse for now: the whole screen is the changed area. Row- or tile-level differencing, and
                    // Desktop Duplication's own dirty rectangles, come with the video phase.
                    out.dirty.push_back({0, 0, m_width, m_height});
                    m_previous = out.pixels;
                    return true;
                }

            private:
                struct MonitorEntry
                {
                    uint32_t id = 0;
                    RECT rect = {};
                    bool primary = false;
                    wchar_t device[CCHDEVICENAME] = {};
                };

                static BOOL CALLBACK CollectMonitor(HMONITOR monitor, HDC, LPRECT, LPARAM data)
                {
                    auto* list = reinterpret_cast<std::vector<MonitorEntry>*>(data);
                    MONITORINFOEXW info = {};
                    info.cbSize = sizeof(info);
                    if (!GetMonitorInfoW(monitor, &info))
                    {
                        return TRUE;
                    }
                    MonitorEntry entry;
                    entry.rect = info.rcMonitor;
                    entry.primary = (info.dwFlags & MONITORINFOF_PRIMARY) != 0;
                    wcsncpy_s(entry.device, info.szDevice, _TRUNCATE);
                    // \\.\DISPLAY2 -> 2; a name without a number gets the next free one.
                    uint32_t number = 0;
                    for (const wchar_t* c = info.szDevice; *c != 0; ++c)
                    {
                        if (*c >= L'0' && *c <= L'9')
                        {
                            number = number * 10 + static_cast<uint32_t>(*c - L'0');
                        }
                    }
                    entry.id = number != 0 ? number : static_cast<uint32_t>(list->size() + 1);
                    list->push_back(entry);
                    return TRUE;
                }

                static std::vector<MonitorEntry> EnumerateMonitors()
                {
                    std::vector<MonitorEntry> monitors;
                    EnumDisplayMonitors(nullptr, nullptr, CollectMonitor, reinterpret_cast<LPARAM>(&monitors));
                    return monitors;
                }

                // Draws the cursor on black and on white in a small DIB; the difference gives each pixel's alpha and
                // the picture over black gives its (premultiplied) colour. This handles colour, alpha and
                // black-and-white cursors alike. Alpha is then cut to 0 or 255, as the wire carries.
                bool RenderCursor(HCURSOR cursor, Cursor& out)
                {
                    ICONINFO iconInfo = {};
                    if (!GetIconInfo(cursor, &iconInfo))
                    {
                        return false;
                    }
                    BITMAP bitmap = {};
                    bool ok = GetObject(iconInfo.hbmColor != nullptr ? iconInfo.hbmColor : iconInfo.hbmMask,
                                        sizeof(bitmap), &bitmap) != 0;
                    // A black-and-white cursor keeps both masks in one bitmap, twice as tall as the image.
                    int width = bitmap.bmWidth;
                    int height = iconInfo.hbmColor != nullptr ? bitmap.bmHeight : bitmap.bmHeight / 2;
                    int hotspotX = static_cast<int>(iconInfo.xHotspot);
                    int hotspotY = static_cast<int>(iconInfo.yHotspot);
                    if (iconInfo.hbmColor != nullptr)
                    {
                        DeleteObject(iconInfo.hbmColor);
                    }
                    if (iconInfo.hbmMask != nullptr)
                    {
                        DeleteObject(iconInfo.hbmMask);
                    }
                    if (!ok || width <= 0 || height <= 0 || width > 512 || height > 512)
                    {
                        return false;
                    }

                    BITMAPINFO header = {};
                    header.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
                    header.bmiHeader.biWidth = width;
                    header.bmiHeader.biHeight = -height;
                    header.bmiHeader.biPlanes = 1;
                    header.bmiHeader.biBitCount = 32;
                    header.bmiHeader.biCompression = BI_RGB;
                    void* bits = nullptr;
                    HBITMAP dib = CreateDIBSection(m_memoryDc, &header, DIB_RGB_COLORS, &bits, nullptr, 0);
                    if (dib == nullptr || bits == nullptr)
                    {
                        return false;
                    }
                    HDC dc = CreateCompatibleDC(m_screenDc);
                    HGDIOBJ previous = SelectObject(dc, dib);
                    const size_t count = static_cast<size_t>(width) * height;
                    std::vector<uint32_t> onBlack(count);
                    std::vector<uint32_t> onWhite(count);
                    std::memset(bits, 0x00, count * 4);
                    DrawIconEx(dc, 0, 0, cursor, width, height, 0, nullptr, DI_NORMAL);
                    std::memcpy(onBlack.data(), bits, count * 4);
                    std::memset(bits, 0xFF, count * 4);
                    DrawIconEx(dc, 0, 0, cursor, width, height, 0, nullptr, DI_NORMAL);
                    std::memcpy(onWhite.data(), bits, count * 4);
                    SelectObject(dc, previous);
                    DeleteDC(dc);
                    DeleteObject(dib);

                    out.width = width;
                    out.height = height;
                    out.hotspotX = hotspotX;
                    out.hotspotY = hotspotY;
                    out.argb.assign(count, 0);
                    bool anyVisible = false;
                    for (size_t i = 0; i < count; ++i)
                    {
                        uint32_t black = onBlack[i];
                        uint32_t white = onWhite[i];
                        // The most a channel rose from black to white is how much of the pixel was see-through.
                        uint32_t seeThrough = 0;
                        for (int shift = 0; shift <= 16; shift += 8)
                        {
                            uint32_t rise = ((white >> shift) & 0xFF) - ((black >> shift) & 0xFF);
                            seeThrough = std::max(seeThrough, rise);
                        }
                        uint32_t alpha = 255 - std::min<uint32_t>(255, seeThrough);
                        if (alpha < 128)
                        {
                            continue;
                        }
                        anyVisible = true;
                        uint32_t red = std::min<uint32_t>(255, ((black >> 16) & 0xFF) * 255 / alpha);
                        uint32_t green = std::min<uint32_t>(255, ((black >> 8) & 0xFF) * 255 / alpha);
                        uint32_t blue = std::min<uint32_t>(255, (black & 0xFF) * 255 / alpha);
                        out.argb[i] = 0xFF000000u | (red << 16) | (green << 8) | blue;
                    }
                    if (!anyVisible)
                    {
                        out.argb.clear();
                        out.width = 0;
                        out.height = 0;
                    }
                    return true;
                }

                void MeasureVirtualScreen()
                {
                    m_originX = GetSystemMetrics(SM_XVIRTUALSCREEN);
                    m_originY = GetSystemMetrics(SM_YVIRTUALSCREEN);
                    m_width = GetSystemMetrics(SM_CXVIRTUALSCREEN);
                    m_height = GetSystemMetrics(SM_CYVIRTUALSCREEN);
                }

                bool EnsureBitmap()
                {
                    if (m_bitmap != nullptr && m_bitmapWidth == m_width && m_bitmapHeight == m_height)
                    {
                        return true;
                    }
                    if (m_bitmap != nullptr)
                    {
                        SelectObject(m_memoryDc, m_oldBitmap);
                        DeleteObject(m_bitmap);
                        m_bitmap = nullptr;
                    }
                    BITMAPINFO info = {};
                    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
                    info.bmiHeader.biWidth = m_width;
                    info.bmiHeader.biHeight = -m_height; // negative: top-down, so row 0 is the top row
                    info.bmiHeader.biPlanes = 1;
                    info.bmiHeader.biBitCount = 32;
                    info.bmiHeader.biCompression = BI_RGB;
                    m_bits = nullptr;
                    m_bitmap = CreateDIBSection(m_memoryDc, &info, DIB_RGB_COLORS, &m_bits, nullptr, 0);
                    if (m_bitmap == nullptr || m_bits == nullptr)
                    {
                        m_bitmap = nullptr;
                        return false;
                    }
                    m_oldBitmap = static_cast<HBITMAP>(SelectObject(m_memoryDc, m_bitmap));
                    m_bitmapWidth = m_width;
                    m_bitmapHeight = m_height;
                    m_previous.clear();
                    return true;
                }

                void Release()
                {
                    if (m_bitmap != nullptr)
                    {
                        SelectObject(m_memoryDc, m_oldBitmap);
                        DeleteObject(m_bitmap);
                        m_bitmap = nullptr;
                    }
                    if (m_memoryDc != nullptr)
                    {
                        DeleteDC(m_memoryDc);
                        m_memoryDc = nullptr;
                    }
                    if (m_screenDc != nullptr)
                    {
                        ReleaseDC(nullptr, m_screenDc);
                        m_screenDc = nullptr;
                    }
                }

                HDC m_screenDc = nullptr;
                HDC m_memoryDc = nullptr;
                HBITMAP m_bitmap = nullptr;
                HBITMAP m_oldBitmap = nullptr;
                void* m_bits = nullptr;
                int m_originX = 0;
                int m_originY = 0;
                int m_width = 0;
                int m_height = 0;
                int m_bitmapWidth = 0;
                int m_bitmapHeight = 0;
                std::vector<uint32_t> m_previous; // the last frame, to tell when nothing changed
                std::vector<std::wstring> m_changedDevices; // monitors whose mode we changed (to put back)
                HCURSOR m_lastCursor = nullptr;             // the pointer image last sent
                bool m_cursorSent = false;
            };
        } // namespace

        std::unique_ptr<ScreenSource> CreateSystemScreen(std::string& error)
        {
            auto screen = std::make_unique<WindowsScreen>();
            if (!screen->Open(error))
            {
                return nullptr;
            }
            return screen;
        }
    } // namespace Server
} // namespace Marconi
