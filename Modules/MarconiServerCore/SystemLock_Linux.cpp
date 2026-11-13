// Copyright (c) 2026 Red Marmoset Studios Ltd.
// Locks a Linux desktop by asking the session manager (loginctl), then the screensaver (xdg-screensaver)

#include "SystemLock.h"

#include <spawn.h>
#include <sys/wait.h>

extern char** environ;

namespace Marconi
{
    namespace Server
    {
        namespace
        {
            // Runs a program with arguments and returns true if it ran and exited with 0.
            bool RunQuietly(char* const argv[])
            {
                pid_t pid = 0;
                if (posix_spawnp(&pid, argv[0], nullptr, nullptr, argv, environ) != 0)
                {
                    return false;
                }
                int status = 0;
                if (waitpid(pid, &status, 0) < 0)
                {
                    return false;
                }
                return WIFEXITED(status) && WEXITSTATUS(status) == 0;
            }
        } // namespace

        bool LockThisComputer()
        {
            char loginctl[] = "loginctl";
            char lockSession[] = "lock-session";
            char* const first[] = {loginctl, lockSession, nullptr};
            if (RunQuietly(first))
            {
                return true;
            }
            char xdg[] = "xdg-screensaver";
            char lock[] = "lock";
            char* const second[] = {xdg, lock, nullptr};
            return RunQuietly(second);
        }
    } // namespace Server
} // namespace Marconi
