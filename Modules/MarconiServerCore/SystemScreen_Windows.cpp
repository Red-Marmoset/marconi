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

#include "SystemScreen.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <cstring>

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

                std::vector<Wire::Display> GetDisplays() override
                {
                    MeasureVirtualScreen();
                    Wire::Display display;
                    display.id = 1;
                    display.x = 0; // the capture is offset so the virtual desktop's top-left is (0,0)
                    display.y = 0;
                    display.width = m_width;
                    display.height = m_height;
                    return {display};
                }

                bool Grab(Capture& out) override
                {
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
