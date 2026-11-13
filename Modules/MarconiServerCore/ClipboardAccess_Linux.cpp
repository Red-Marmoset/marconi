// Copyright (c) 2026 Red Marmoset Studios Ltd.
// Linux clipboard on X11: the CLIPBOARD selection, read and set, with change notification from XFixes.
//
// Reading opens a short-lived connection of its own for each call (so any thread may read, and nothing waits on the
// event thread). Setting the clipboard means owning the selection and answering other programs' paste requests for as
// long as we own it, so one event thread on a second connection does that; Write hands it the content and waits until
// it owns the selection. Limits: no INCR when serving (a paste of more than the X server's maximum request size is
// refused; reading handles INCR), and nothing on Wayland-only sessions (no DISPLAY: CreateSystemClipboard is null).

#include "ClipboardAccess.h"

#include <Log/Log.h>

#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/extensions/Xfixes.h>

#include <sys/select.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
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
            constexpr size_t kMaxReadBytes = 48 * 1024 * 1024; // more than any clipboard we'd carry; stops a runaway owner
            constexpr int kReadTimeoutMs = 2000;

            XErrorHandler g_previousErrorHandler = nullptr;

            // A paste requester that vanishes mid-reply makes the X server report BadWindow to us; that must not end
            // the whole server, which is what Xlib's default handler does. Other errors go to the previous handler.
            int ClipboardErrorHandler(Display* display, XErrorEvent* event)
            {
                if (event->error_code == BadWindow)
                {
                    return 0;
                }
                if (g_previousErrorHandler != nullptr)
                {
                    return g_previousErrorHandler(display, event);
                }
                return 0;
            }

            struct Atoms
            {
                Atom clipboard, targets, utf8, text, textUtf8, png, incr, property;

                void Load(Display* display)
                {
                    clipboard = XInternAtom(display, "CLIPBOARD", False);
                    targets = XInternAtom(display, "TARGETS", False);
                    utf8 = XInternAtom(display, "UTF8_STRING", False);
                    text = XA_STRING;
                    textUtf8 = XInternAtom(display, "text/plain;charset=utf-8", False);
                    png = XInternAtom(display, "image/png", False);
                    incr = XInternAtom(display, "INCR", False);
                    property = XInternAtom(display, "MARCONI_CLIPBOARD", False);
                }
            };

            std::string Latin1ToUtf8(const std::vector<uint8_t>& latin1)
            {
                std::string out;
                for (uint8_t c : latin1)
                {
                    if (c < 0x80)
                    {
                        out.push_back(static_cast<char>(c));
                    }
                    else
                    {
                        out.push_back(static_cast<char>(0xC0 | (c >> 6)));
                        out.push_back(static_cast<char>(0x80 | (c & 0x3F)));
                    }
                }
                return out;
            }

            // Waits up to the deadline for the next event on this connection. False on timeout.
            bool NextEvent(Display* display, XEvent& event, std::chrono::steady_clock::time_point deadline)
            {
                for (;;)
                {
                    if (XPending(display) > 0)
                    {
                        XNextEvent(display, &event);
                        return true;
                    }
                    auto now = std::chrono::steady_clock::now();
                    if (now >= deadline)
                    {
                        return false;
                    }
                    auto remaining = std::chrono::duration_cast<std::chrono::microseconds>(deadline - now);
                    fd_set fds;
                    FD_ZERO(&fds);
                    int fd = ConnectionNumber(display);
                    FD_SET(fd, &fds);
                    timeval timeout;
                    timeout.tv_sec = static_cast<long>(remaining.count() / 1000000);
                    timeout.tv_usec = static_cast<long>(remaining.count() % 1000000);
                    select(fd + 1, &fds, nullptr, nullptr, &timeout);
                }
            }

            // Asks the CLIPBOARD owner for `target` and collects the answer (following INCR for big ones).
            bool Fetch(Display* display, Window window, const Atoms& atoms, Atom target, Atom& type,
                       std::vector<uint8_t>& bytes)
            {
                bytes.clear();
                XDeleteProperty(display, window, atoms.property);
                XConvertSelection(display, atoms.clipboard, target, atoms.property, window, CurrentTime);
                XFlush(display);
                auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(kReadTimeoutMs);
                bool incremental = false;
                bool started = false;
                XEvent event;
                while (NextEvent(display, event, deadline))
                {
                    if (!started)
                    {
                        if (event.type != SelectionNotify || event.xselection.requestor != window)
                        {
                            continue;
                        }
                        if (event.xselection.property == None)
                        {
                            return false; // the owner can't give it in that form (or there is no owner)
                        }
                        started = true;
                    }
                    else if (!(event.type == PropertyNotify && event.xproperty.window == window &&
                               event.xproperty.atom == atoms.property && event.xproperty.state == PropertyNewValue))
                    {
                        continue;
                    }

                    Atom actualType = None;
                    int actualFormat = 0;
                    unsigned long count = 0;
                    unsigned long after = 0;
                    unsigned char* data = nullptr;
                    if (XGetWindowProperty(display, window, atoms.property, 0, 1L << 28, True, AnyPropertyType,
                                           &actualType, &actualFormat, &count, &after, &data) != Success)
                    {
                        return false;
                    }
                    if (actualType == atoms.incr && !incremental)
                    {
                        if (data != nullptr)
                        {
                            XFree(data);
                        }
                        incremental = true; // the owner will now send the data in pieces, announced by PropertyNotify
                        deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(kReadTimeoutMs);
                        continue;
                    }
                    size_t unit = actualFormat == 32 ? sizeof(long) : actualFormat == 16 ? 2 : 1;
                    size_t length = static_cast<size_t>(count) * unit;
                    if (data != nullptr)
                    {
                        if (bytes.size() + length > kMaxReadBytes)
                        {
                            XFree(data);
                            return false;
                        }
                        bytes.insert(bytes.end(), data, data + length);
                        XFree(data);
                    }
                    type = actualType;
                    if (!incremental || length == 0)
                    {
                        return true;
                    }
                    deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(kReadTimeoutMs);
                }
                return false; // timed out
            }

            class LinuxClipboard : public ClipboardAccess
            {
            public:
                ~LinuxClipboard() override
                {
                    if (m_thread.joinable())
                    {
                        m_stop = true;
                        Wake();
                        m_thread.join();
                    }
                    if (m_display != nullptr)
                    {
                        XDestroyWindow(m_display, m_window);
                        XCloseDisplay(m_display);
                    }
                    if (m_wakePipe[0] >= 0)
                    {
                        close(m_wakePipe[0]);
                        close(m_wakePipe[1]);
                    }
                }

                bool Open()
                {
                    m_display = XOpenDisplay(nullptr);
                    if (m_display == nullptr)
                    {
                        return false;
                    }
                    if (g_previousErrorHandler == nullptr)
                    {
                        g_previousErrorHandler = XSetErrorHandler(ClipboardErrorHandler);
                    }
                    int errorBase = 0;
                    if (XFixesQueryExtension(m_display, &m_fixesEventBase, &errorBase) == 0)
                    {
                        XCloseDisplay(m_display);
                        m_display = nullptr;
                        return false;
                    }
                    if (pipe(m_wakePipe) != 0)
                    {
                        XCloseDisplay(m_display);
                        m_display = nullptr;
                        return false;
                    }
                    m_atoms.Load(m_display);
                    Window root = DefaultRootWindow(m_display);
                    m_window = XCreateSimpleWindow(m_display, root, 0, 0, 1, 1, 0, 0, 0);
                    XFixesSelectSelectionInput(m_display, m_window, m_atoms.clipboard,
                                               XFixesSetSelectionOwnerNotifyMask);
                    XFlush(m_display);
                    m_thread = std::thread([this]() { RunEvents(); });
                    return true;
                }

                bool Read(ClipboardContents& out) override
                {
                    out = ClipboardContents();
                    {
                        std::lock_guard<std::mutex> lock(m_mutex);
                        if (m_owned)
                        {
                            out = m_contents; // our own: asking ourselves over X would wait on the thread we're on
                            return !out.IsEmpty();
                        }
                    }
                    Display* display = XOpenDisplay(nullptr);
                    if (display == nullptr)
                    {
                        return false;
                    }
                    Atoms atoms;
                    atoms.Load(display);
                    Window window = XCreateSimpleWindow(display, DefaultRootWindow(display), 0, 0, 1, 1, 0, 0, 0);
                    XSelectInput(display, window, PropertyChangeMask);

                    Atom type = None;
                    std::vector<uint8_t> bytes;
                    bool haveUtf8 = false;
                    bool haveLatin1 = false;
                    bool havePng = false;
                    if (Fetch(display, window, atoms, atoms.targets, type, bytes))
                    {
                        const unsigned long* list = reinterpret_cast<const unsigned long*>(bytes.data());
                        for (size_t i = 0; i < bytes.size() / sizeof(unsigned long); ++i)
                        {
                            haveUtf8 |= list[i] == atoms.utf8 || list[i] == atoms.textUtf8;
                            haveLatin1 |= list[i] == atoms.text;
                            havePng |= list[i] == atoms.png;
                        }
                    }
                    else
                    {
                        // An owner that won't say what it has: try text anyway.
                        haveUtf8 = true;
                    }
                    if (haveUtf8 && Fetch(display, window, atoms, atoms.utf8, type, bytes))
                    {
                        out.text.assign(bytes.begin(), bytes.end());
                    }
                    else if (haveLatin1 && Fetch(display, window, atoms, atoms.text, type, bytes))
                    {
                        out.text = Latin1ToUtf8(bytes);
                    }
                    if (havePng && Fetch(display, window, atoms, atoms.png, type, bytes))
                    {
                        out.png = bytes;
                    }
                    XDestroyWindow(display, window);
                    XCloseDisplay(display);
                    return !out.IsEmpty();
                }

                bool Write(const ClipboardContents& contents) override
                {
                    std::unique_lock<std::mutex> lock(m_mutex);
                    m_pendingWrite = contents;
                    m_writeRequested = true;
                    m_writeDone = false;
                    Wake();
                    bool done = m_writeChanged.wait_for(lock, std::chrono::seconds(2), [this]() { return m_writeDone; });
                    return done && m_writeOk;
                }

                void SetChangeHandler(std::function<void()> handler) override
                {
                    std::lock_guard<std::mutex> lock(m_handlerMutex);
                    m_handler = std::move(handler);
                }

            private:
                void Wake()
                {
                    char byte = 1;
                    if (write(m_wakePipe[1], &byte, 1) < 0)
                    {
                        // a full pipe already holds a wake-up
                    }
                }

                void RunEvents()
                {
                    const int xfd = ConnectionNumber(m_display);
                    while (!m_stop)
                    {
                        while (XPending(m_display) > 0)
                        {
                            XEvent event;
                            XNextEvent(m_display, &event);
                            HandleEvent(event);
                        }
                        ApplyWrite();
                        fd_set fds;
                        FD_ZERO(&fds);
                        FD_SET(xfd, &fds);
                        FD_SET(m_wakePipe[0], &fds);
                        int highest = xfd > m_wakePipe[0] ? xfd : m_wakePipe[0];
                        if (select(highest + 1, &fds, nullptr, nullptr, nullptr) > 0 && FD_ISSET(m_wakePipe[0], &fds))
                        {
                            char buffer[64];
                            if (read(m_wakePipe[0], buffer, sizeof(buffer)) < 0)
                            {
                                // nothing to do
                            }
                        }
                    }
                }

                // Event thread: takes a content Write handed over and becomes the CLIPBOARD owner.
                void ApplyWrite()
                {
                    std::lock_guard<std::mutex> lock(m_mutex);
                    if (!m_writeRequested)
                    {
                        return;
                    }
                    m_writeRequested = false;
                    m_contents = std::move(m_pendingWrite);
                    XSetSelectionOwner(m_display, m_atoms.clipboard, m_window, CurrentTime);
                    XFlush(m_display);
                    m_owned = XGetSelectionOwner(m_display, m_atoms.clipboard) == m_window;
                    m_writeOk = m_owned;
                    m_writeDone = true;
                    m_writeChanged.notify_all();
                }

                void HandleEvent(const XEvent& event)
                {
                    if (event.type == SelectionRequest)
                    {
                        Serve(event.xselectionrequest);
                    }
                    else if (event.type == SelectionClear)
                    {
                        std::lock_guard<std::mutex> lock(m_mutex);
                        if (event.xselectionclear.selection == m_atoms.clipboard)
                        {
                            m_owned = false; // someone else copied: their content is the clipboard now
                            m_contents = ClipboardContents();
                        }
                    }
                    else if (event.type == m_fixesEventBase + XFixesSelectionNotify)
                    {
                        const XFixesSelectionNotifyEvent& notify =
                            reinterpret_cast<const XFixesSelectionNotifyEvent&>(event);
                        if (notify.selection == m_atoms.clipboard && notify.owner != m_window)
                        {
                            std::lock_guard<std::mutex> lock(m_handlerMutex);
                            if (m_handler)
                            {
                                m_handler();
                            }
                        }
                    }
                }

                // Answers one program's request to paste what we own.
                void Serve(const XSelectionRequestEvent& request)
                {
                    XEvent reply;
                    std::memset(&reply, 0, sizeof(reply));
                    reply.xselection.type = SelectionNotify;
                    reply.xselection.display = request.display;
                    reply.xselection.requestor = request.requestor;
                    reply.xselection.selection = request.selection;
                    reply.xselection.target = request.target;
                    reply.xselection.time = request.time;
                    reply.xselection.property = None; // refused unless we fill it in
                    Atom property = request.property != None ? request.property : request.target;

                    {
                        std::lock_guard<std::mutex> lock(m_mutex);
                        const size_t maxBytes = static_cast<size_t>(XExtendedMaxRequestSize(m_display)) * 4 - 1024;
                        if (m_owned && request.selection == m_atoms.clipboard)
                        {
                            if (request.target == m_atoms.targets)
                            {
                                std::vector<Atom> list = {m_atoms.targets};
                                if (!m_contents.text.empty())
                                {
                                    list.push_back(m_atoms.utf8);
                                    list.push_back(m_atoms.textUtf8);
                                    list.push_back(m_atoms.text);
                                }
                                if (!m_contents.png.empty())
                                {
                                    list.push_back(m_atoms.png);
                                }
                                XChangeProperty(m_display, request.requestor, property, XA_ATOM, 32, PropModeReplace,
                                                reinterpret_cast<const unsigned char*>(list.data()),
                                                static_cast<int>(list.size()));
                                reply.xselection.property = property;
                            }
                            else if (!m_contents.text.empty() &&
                                     (request.target == m_atoms.utf8 || request.target == m_atoms.textUtf8) &&
                                     m_contents.text.size() <= maxBytes)
                            {
                                XChangeProperty(m_display, request.requestor, property, m_atoms.utf8, 8,
                                                PropModeReplace,
                                                reinterpret_cast<const unsigned char*>(m_contents.text.data()),
                                                static_cast<int>(m_contents.text.size()));
                                reply.xselection.property = property;
                            }
                            else if (!m_contents.text.empty() && request.target == m_atoms.text &&
                                     m_contents.text.size() <= maxBytes)
                            {
                                std::string latin1 = ToLatin1(m_contents.text);
                                XChangeProperty(m_display, request.requestor, property, XA_STRING, 8, PropModeReplace,
                                                reinterpret_cast<const unsigned char*>(latin1.data()),
                                                static_cast<int>(latin1.size()));
                                reply.xselection.property = property;
                            }
                            else if (!m_contents.png.empty() && request.target == m_atoms.png &&
                                     m_contents.png.size() <= maxBytes)
                            {
                                XChangeProperty(m_display, request.requestor, property, m_atoms.png, 8, PropModeReplace,
                                                m_contents.png.data(), static_cast<int>(m_contents.png.size()));
                                reply.xselection.property = property;
                            }
                        }
                    }
                    XSendEvent(m_display, request.requestor, False, 0, &reply);
                    XFlush(m_display);
                }

                // UTF-8 to Latin-1 for programs that only ask for STRING; characters outside it become '?'.
                static std::string ToLatin1(const std::string& utf8)
                {
                    std::string out;
                    for (size_t i = 0; i < utf8.size();)
                    {
                        unsigned char lead = static_cast<unsigned char>(utf8[i]);
                        if (lead < 0x80)
                        {
                            out.push_back(static_cast<char>(lead));
                            ++i;
                        }
                        else if ((lead & 0xE0) == 0xC0 && i + 1 < utf8.size())
                        {
                            unsigned code = ((lead & 0x1F) << 6) | (static_cast<unsigned char>(utf8[i + 1]) & 0x3F);
                            out.push_back(code < 256 ? static_cast<char>(code) : '?');
                            i += 2;
                        }
                        else
                        {
                            out.push_back('?');
                            ++i;
                            while (i < utf8.size() && (static_cast<unsigned char>(utf8[i]) & 0xC0) == 0x80)
                            {
                                ++i;
                            }
                        }
                    }
                    return out;
                }

                Display* m_display = nullptr;
                Window m_window = 0;
                Atoms m_atoms{};
                int m_fixesEventBase = 0;
                int m_wakePipe[2] = {-1, -1};
                std::thread m_thread;
                std::atomic<bool> m_stop{false};

                std::mutex m_mutex; // guards what follows
                std::condition_variable m_writeChanged;
                bool m_owned = false;
                ClipboardContents m_contents; // what we serve while we own the selection
                ClipboardContents m_pendingWrite;
                bool m_writeRequested = false;
                bool m_writeDone = false;
                bool m_writeOk = false;

                std::mutex m_handlerMutex;
                std::function<void()> m_handler;
            };
        } // namespace

        std::unique_ptr<ClipboardAccess> CreateSystemClipboard()
        {
            auto clipboard = std::make_unique<LinuxClipboard>();
            if (!clipboard->Open())
            {
                return nullptr; // no X display, or no XFixes
            }
            return clipboard;
        }
    } // namespace Server
} // namespace Marconi
