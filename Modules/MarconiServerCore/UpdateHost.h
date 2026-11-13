// Copyright (c) 2026 Red Marmoset Studios Ltd.
// What a program that runs the server gives it so a newer Marconi can update it: a way to look inside an installer,
// a way to start one, a way to ask the person at this computer, and where to keep the file. Each is an interface, so
// the tests use stand-ins and only the Windows versions touch the system (InstallerPlatform_Windows.cpp).

#pragma once

#include "UpdatePolicy.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace Marconi
{
    namespace Server
    {
        // Reads an installer's signature and the product and version written in it.
        class InstallerInspector
        {
        public:
            virtual ~InstallerInspector() = default;
            virtual InstallerFacts Inspect(const std::string& path) = 0;
            // The same facts about the program that is running now (its signature is what an update must match).
            virtual InstallerFacts InspectRunningServer() = 0;
        };

        // Starts an installer that has passed every check, detached, so it outlives this program (which it closes).
        class InstallerLauncher
        {
        public:
            virtual ~InstallerLauncher() = default;
            // False (with `error`, for the log) if it couldn't be started, including when the person at this computer
            // said no to the system's permission prompt.
            virtual bool Launch(const std::string& installerPath, const std::vector<std::string>& arguments,
                                std::string& error) = 0;
        };

        // What the person at this computer is asked.
        struct UpdateQuestion
        {
            std::string viewerName;     // the viewer's name from its Hello
            std::string viewerVersion;  // its Marconi version, if it said
            std::string address;        // where it connected from
            std::string currentVersion; // this server's version
            std::string newVersion;     // the version offered
        };

        // Asks the person at this computer whether to allow an update. `answer` must be called once, from any thread;
        // an answer that comes after kApprovalSeconds, or after the viewer left, is ignored (the server has already
        // counted it as no). `withdraw` is called if the question is no longer needed (the time ran out or the viewer
        // left) so a window can close itself; it may come from any thread.
        class UpdateApprover
        {
        public:
            virtual ~UpdateApprover() = default;
            virtual void Ask(const UpdateQuestion& question, std::function<void(bool allowed)> answer) = 0;
            virtual void Withdraw() {}
        };

        // Everything above, bundled for the listener. A null launcher means this server can't update itself (every
        // offer is answered "not supported here"); a null approver means nobody can be asked, so only the Allow
        // setting lets an update through.
        struct UpdateEnvironment
        {
            std::string stagingFolder; // the server's own folder for the installer; made (owner-only) when needed
            std::string platform = Wire::kUpdatePlatformWindows;
            InstallerInspector* inspector = nullptr;
            InstallerLauncher* launcher = nullptr;
            UpdateApprover* approver = nullptr;
            // Called once the installer has started and the viewer has been told: the program stops so the installer
            // can replace it. From the session's thread.
            std::function<void()> onInstallerStarted;
            // Free bytes on the disk holding the staging folder (tests replace it). Unset: asked of the system.
            std::function<uint64_t(const std::string& folder)> freeSpace;
            // How long the person has to answer, in milliseconds (tests make it short).
            int approvalTimeoutMs = kApprovalSeconds * 1000;
        };

        // The arguments the installer is started with: silent, and start Marconi Server again when done.
        std::vector<std::string> SilentInstallArguments();

        // This system's inspector and launcher, or null where the server can't update itself (everywhere but
        // Windows for now). InstallerPlatform_Windows.cpp and InstallerPlatform_Posix.cpp.
        std::unique_ptr<InstallerInspector> CreatePlatformInstallerInspector();
        std::unique_ptr<InstallerLauncher> CreatePlatformInstallerLauncher();
    } // namespace Server
} // namespace Marconi
