// Copyright (c) 2026 Red Marmoset Studios Ltd.
// BSD socket specifics for Net::Socket (macOS, Linux)

#include "SocketPlatform.h"

#include <cerrno>
#include <cstring>
#include <fcntl.h>

namespace Marconi
{
    namespace Net
    {
        namespace Platform
        {
            void EnsureInitialised()
            {
            }

            void CloseSocket(intptr_t handle)
            {
                close(static_cast<int>(handle));
            }

            void ShutdownSocket(intptr_t handle)
            {
                shutdown(static_cast<int>(handle), SHUT_RDWR);
            }

            bool SetNonBlocking(intptr_t handle, bool nonBlocking)
            {
                int fd = static_cast<int>(handle);
                int flags = fcntl(fd, F_GETFL, 0);
                if (flags < 0)
                {
                    return false;
                }
                flags = nonBlocking ? (flags | O_NONBLOCK) : (flags & ~O_NONBLOCK);
                return fcntl(fd, F_SETFL, flags) == 0;
            }

            void DisableSigPipe(intptr_t handle)
            {
#if defined(SO_NOSIGPIPE)
                int on = 1;
                setsockopt(static_cast<int>(handle), SOL_SOCKET, SO_NOSIGPIPE, &on, sizeof(on));
#else
                (void)handle;
#endif
            }

            int SendFlags()
            {
#if defined(MSG_NOSIGNAL)
                return MSG_NOSIGNAL;
#else
                return 0;
#endif
            }

            int LastError()
            {
                return errno;
            }

            bool IsConnectInProgress(int error)
            {
                return error == EINPROGRESS || error == EWOULDBLOCK;
            }

            bool IsInterrupted(int error)
            {
                return error == EINTR;
            }

            std::string ErrorString(int error)
            {
                return std::string(std::strerror(error)) + " (" + std::to_string(error) + ")";
            }

            int SelectWidth(intptr_t handle)
            {
                return static_cast<int>(handle) + 1;
            }
        } // namespace Platform
    } // namespace Net
} // namespace Marconi
