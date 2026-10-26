// Copyright (c) 2026 Red Marmoset Studios Ltd.
// Winsock specifics for Net::Socket

#include "SocketPlatform.h"

#include <mstcpip.h>

#include <mutex>

namespace Marconi
{
    namespace Net
    {
        namespace Platform
        {
            void EnsureInitialised()
            {
                static std::once_flag once;
                std::call_once(once, []() {
                    WSADATA data;
                    WSAStartup(MAKEWORD(2, 2), &data);
                });
            }

            void CloseSocket(intptr_t handle)
            {
                closesocket(static_cast<SOCKET>(handle));
            }

            void ShutdownSocket(intptr_t handle)
            {
                shutdown(static_cast<SOCKET>(handle), SD_BOTH);
            }

            bool SetNonBlocking(intptr_t handle, bool nonBlocking)
            {
                u_long mode = nonBlocking ? 1 : 0;
                return ioctlsocket(static_cast<SOCKET>(handle), FIONBIO, &mode) == 0;
            }

            void DisableSigPipe(intptr_t)
            {
                // Windows has no SIGPIPE.
            }

            void EnableKeepAlive(intptr_t handle, int idleSeconds, int intervalSeconds, int count)
            {
                // Windows 10 and later send 10 probes; `count` can't be set here.
                (void)count;
                tcp_keepalive settings = {};
                settings.onoff = 1;
                settings.keepalivetime = static_cast<ULONG>(idleSeconds) * 1000;
                settings.keepaliveinterval = static_cast<ULONG>(intervalSeconds) * 1000;
                DWORD returned = 0;
                WSAIoctl(static_cast<SOCKET>(handle), SIO_KEEPALIVE_VALS, &settings, sizeof(settings), nullptr, 0,
                         &returned, nullptr, nullptr);
            }

            int SendFlags()
            {
                return 0;
            }

            int LastError()
            {
                return WSAGetLastError();
            }

            bool IsConnectInProgress(int error)
            {
                return error == WSAEWOULDBLOCK || error == WSAEINPROGRESS;
            }

            bool IsWouldBlock(int error)
            {
                return error == WSAEWOULDBLOCK;
            }

            bool IsInterrupted(int error)
            {
                return error == WSAEINTR;
            }

            std::string ErrorString(int error)
            {
                char* message = nullptr;
                FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                               nullptr, static_cast<DWORD>(error), MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
                               reinterpret_cast<LPSTR>(&message), 0, nullptr);
                std::string text = message != nullptr ? message : "unknown error";
                if (message != nullptr)
                {
                    LocalFree(message);
                }
                while (!text.empty() && (text.back() == '\n' || text.back() == '\r' || text.back() == '.'))
                {
                    text.pop_back();
                }
                return text + " (" + std::to_string(error) + ")";
            }

            int SelectWidth(intptr_t)
            {
                return 0;
            }
        } // namespace Platform
    } // namespace Net
} // namespace Marconi
