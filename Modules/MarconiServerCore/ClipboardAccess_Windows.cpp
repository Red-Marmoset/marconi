// Copyright (c) 2026 Red Marmoset Studios Ltd.
// Windows clipboard: text (CF_UNICODETEXT) and images (CF_DIB, converted to and from PNG), with change notification
// from a clipboard format listener.
//
// NOTE: not yet built or run on Windows from this environment (the cloud build is Linux). It needs a build and a
// real test on a Windows machine: copying text and images in other programs and pasting what a viewer sent. Text uses
// CRLF on Windows and bare LF on the wire, converted both ways here.

#include "ClipboardAccess.h"

#include <MarconiCore/Wire/WireImage.h>

#include <Log/Log.h>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <condition_variable>
#include <cstring>
#include <mutex>
#include <thread>

namespace Marconi
{
    namespace Server
    {
        namespace
        {
            constexpr wchar_t kWindowClass[] = L"MarconiServerClipboardWindow";

            // The clipboard may be held by another program for a moment; try for about a quarter of a second.
            bool OpenClipboardWithRetry(HWND owner)
            {
                for (int attempt = 0; attempt < 12; ++attempt)
                {
                    if (OpenClipboard(owner))
                    {
                        return true;
                    }
                    Sleep(20);
                }
                return false;
            }

            std::string WideToUtf8(const wchar_t* wide, int length)
            {
                if (length <= 0)
                {
                    return std::string();
                }
                int bytes = WideCharToMultiByte(CP_UTF8, 0, wide, length, nullptr, 0, nullptr, nullptr);
                std::string out(static_cast<size_t>(bytes), '\0');
                WideCharToMultiByte(CP_UTF8, 0, wide, length, &out[0], bytes, nullptr, nullptr);
                return out;
            }

            // Moves bytes into a new global memory block for SetClipboardData (which takes ownership on success).
            HGLOBAL MakeGlobal(const void* data, size_t length)
            {
                HGLOBAL global = GlobalAlloc(GMEM_MOVEABLE, length);
                if (global == nullptr)
                {
                    return nullptr;
                }
                void* target = GlobalLock(global);
                if (target == nullptr)
                {
                    GlobalFree(global);
                    return nullptr;
                }
                std::memcpy(target, data, length);
                GlobalUnlock(global);
                return global;
            }

            class WindowsClipboard : public ClipboardAccess
            {
            public:
                ~WindowsClipboard() override
                {
                    if (m_thread.joinable())
                    {
                        if (m_window != nullptr)
                        {
                            PostMessageW(m_window, WM_CLOSE, 0, 0);
                        }
                        m_thread.join();
                    }
                }

                bool Open()
                {
                    // The listener needs a window with a message loop, so a thread makes a message-only window and
                    // runs the loop. Open waits until it is ready (or has failed).
                    std::unique_lock<std::mutex> lock(m_startMutex);
                    m_thread = std::thread([this]() { RunWindow(); });
                    m_started.wait(lock, [this]() { return m_startDone; });
                    return m_window != nullptr;
                }

                bool Read(ClipboardContents& out) override
                {
                    out = ClipboardContents();
                    if (!OpenClipboardWithRetry(m_window))
                    {
                        return false;
                    }
                    if (IsClipboardFormatAvailable(CF_UNICODETEXT))
                    {
                        HANDLE handle = GetClipboardData(CF_UNICODETEXT);
                        if (handle != nullptr)
                        {
                            const wchar_t* wide = static_cast<const wchar_t*>(GlobalLock(handle));
                            if (wide != nullptr)
                            {
                                size_t capacity = GlobalSize(handle) / sizeof(wchar_t);
                                size_t length = 0;
                                while (length < capacity && wide[length] != 0)
                                {
                                    ++length;
                                }
                                std::string text = WideToUtf8(wide, static_cast<int>(length));
                                GlobalUnlock(handle);
                                for (size_t i = 0; i < text.size(); ++i)
                                {
                                    if (text[i] == '\r' && i + 1 < text.size() && text[i + 1] == '\n')
                                    {
                                        continue; // CRLF -> LF
                                    }
                                    out.text.push_back(text[i]);
                                }
                            }
                        }
                    }
                    if (IsClipboardFormatAvailable(CF_DIB))
                    {
                        HANDLE handle = GetClipboardData(CF_DIB);
                        if (handle != nullptr)
                        {
                            const uint8_t* data = static_cast<const uint8_t*>(GlobalLock(handle));
                            if (data != nullptr)
                            {
                                Wire::Bytes dib(data, data + GlobalSize(handle));
                                GlobalUnlock(handle);
                                Wire::DibToPng(dib, out.png); // leaves it empty for a form it can't read
                            }
                        }
                    }
                    CloseClipboard();
                    return !out.IsEmpty();
                }

                bool Write(const ClipboardContents& contents) override
                {
                    // Prepare everything before taking the clipboard, so it is held for as short a time as possible.
                    HGLOBAL textGlobal = nullptr;
                    HGLOBAL dibGlobal = nullptr;
                    if (!contents.text.empty())
                    {
                        std::string crlf;
                        for (size_t i = 0; i < contents.text.size(); ++i)
                        {
                            if (contents.text[i] == '\n' && (i == 0 || contents.text[i - 1] != '\r'))
                            {
                                crlf.push_back('\r');
                            }
                            crlf.push_back(contents.text[i]);
                        }
                        int length = MultiByteToWideChar(CP_UTF8, 0, crlf.data(), static_cast<int>(crlf.size()),
                                                         nullptr, 0);
                        std::wstring wide(static_cast<size_t>(length) + 1, L'\0');
                        MultiByteToWideChar(CP_UTF8, 0, crlf.data(), static_cast<int>(crlf.size()), &wide[0], length);
                        textGlobal = MakeGlobal(wide.c_str(), (static_cast<size_t>(length) + 1) * sizeof(wchar_t));
                    }
                    if (!contents.png.empty())
                    {
                        Wire::Bytes dib;
                        if (Wire::PngToDib(contents.png, dib))
                        {
                            dibGlobal = MakeGlobal(dib.data(), dib.size());
                        }
                    }
                    if (textGlobal == nullptr && dibGlobal == nullptr)
                    {
                        return false;
                    }
                    if (!OpenClipboardWithRetry(m_window))
                    {
                        if (textGlobal != nullptr)
                        {
                            GlobalFree(textGlobal);
                        }
                        if (dibGlobal != nullptr)
                        {
                            GlobalFree(dibGlobal);
                        }
                        return false;
                    }
                    EmptyClipboard();
                    bool ok = false;
                    if (textGlobal != nullptr)
                    {
                        if (SetClipboardData(CF_UNICODETEXT, textGlobal) != nullptr)
                        {
                            ok = true;
                        }
                        else
                        {
                            GlobalFree(textGlobal);
                        }
                    }
                    if (dibGlobal != nullptr)
                    {
                        if (SetClipboardData(CF_DIB, dibGlobal) != nullptr)
                        {
                            ok = true;
                        }
                        else
                        {
                            GlobalFree(dibGlobal);
                        }
                    }
                    CloseClipboard();
                    return ok;
                }

                void SetChangeHandler(std::function<void()> handler) override
                {
                    std::lock_guard<std::mutex> lock(m_handlerMutex);
                    m_handler = std::move(handler);
                }

            private:
                static LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
                {
                    if (message == WM_NCCREATE)
                    {
                        auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
                        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(create->lpCreateParams));
                    }
                    auto* self = reinterpret_cast<WindowsClipboard*>(GetWindowLongPtrW(window, GWLP_USERDATA));
                    if (message == WM_CLIPBOARDUPDATE && self != nullptr)
                    {
                        std::lock_guard<std::mutex> lock(self->m_handlerMutex);
                        if (self->m_handler)
                        {
                            self->m_handler();
                        }
                        return 0;
                    }
                    if (message == WM_CLOSE)
                    {
                        DestroyWindow(window);
                        return 0;
                    }
                    if (message == WM_DESTROY)
                    {
                        RemoveClipboardFormatListener(window);
                        PostQuitMessage(0);
                        return 0;
                    }
                    return DefWindowProcW(window, message, wParam, lParam);
                }

                void RunWindow()
                {
                    WNDCLASSW windowClass = {};
                    windowClass.lpfnWndProc = &WindowsClipboard::WindowProc;
                    windowClass.hInstance = GetModuleHandleW(nullptr);
                    windowClass.lpszClassName = kWindowClass;
                    RegisterClassW(&windowClass); // already registered is fine
                    HWND window = CreateWindowExW(0, kWindowClass, L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr,
                                                  windowClass.hInstance, this);
                    if (window != nullptr && !AddClipboardFormatListener(window))
                    {
                        DestroyWindow(window);
                        window = nullptr;
                    }
                    {
                        std::lock_guard<std::mutex> lock(m_startMutex);
                        m_window = window;
                        m_startDone = true;
                    }
                    m_started.notify_all();
                    if (window == nullptr)
                    {
                        return;
                    }
                    MSG message;
                    while (GetMessageW(&message, nullptr, 0, 0) > 0)
                    {
                        TranslateMessage(&message);
                        DispatchMessageW(&message);
                    }
                }

                std::thread m_thread;
                HWND m_window = nullptr;
                std::mutex m_startMutex;
                std::condition_variable m_started;
                bool m_startDone = false;

                std::mutex m_handlerMutex;
                std::function<void()> m_handler;
            };
        } // namespace

        std::unique_ptr<ClipboardAccess> CreateSystemClipboard()
        {
            auto clipboard = std::make_unique<WindowsClipboard>();
            if (!clipboard->Open())
            {
                return nullptr;
            }
            return clipboard;
        }
    } // namespace Server
} // namespace Marconi
