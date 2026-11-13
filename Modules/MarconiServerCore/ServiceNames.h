// Copyright (c) 2026 Red Marmoset Studios Ltd.
// Names shared by the Windows service, its helper and the installer. Only the *_Windows.cpp files include this.

#pragma once

namespace Marconi
{
    namespace Server
    {
        // The service's name in the Services list and for `sc` (the installer starts and stops it by this).
        constexpr const wchar_t* kServiceName = L"MarconiServer";
        constexpr const wchar_t* kServiceDisplayName = L"Marconi Server";
        constexpr const wchar_t* kServiceDescription =
            L"Lets Marconi viewers connect to this computer, including its sign-in screen.";

        // An event the service creates and the session helper sets to ask the service to send Ctrl+Alt+Del.
        constexpr const wchar_t* kSasEventName = L"Global\\MarconiServerSAS";
    } // namespace Server
} // namespace Marconi
