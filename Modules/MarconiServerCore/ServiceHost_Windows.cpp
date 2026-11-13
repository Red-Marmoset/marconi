// Copyright (c) 2026 Red Marmoset Studios Ltd.
// The Windows service: connects to the Service Control Manager, runs the supervisor loop, starts the session
// helper in the console session as SYSTEM, and installs and removes the service.
//
// NOTE: NOT YET BUILT OR RUN ON WINDOWS (written in a Linux cloud environment with no Windows SDK). Expect compile
// fixes, and see the Phase 3 checklist in SERVER_PLAN.md for what has to be tried by hand: the service account
// and privileges, CreateProcessAsUser into the console session, session changes, and the Ctrl+Alt+Del request.

#include "ServiceHost.h"

#include "ServiceNames.h"
#include "ServiceSupervisor.h"

#include <Log/Log.h>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <sddl.h>
#include <userenv.h>
#include <wtsapi32.h>

#include <string>

namespace Marconi
{
    namespace Server
    {
        namespace
        {
            std::string ErrorText(const char* what)
            {
                return std::string(what) + " (Windows error " + std::to_string(GetLastError()) + ")";
            }

            std::wstring GetExePath()
            {
                wchar_t path[MAX_PATH * 2] = {};
                DWORD length = GetModuleFileNameW(nullptr, path, static_cast<DWORD>(sizeof(path) / sizeof(path[0])));
                return std::wstring(path, length);
            }

            // -- The real session list and launcher -------------------------------------------------------------

            class WindowsSessions : public ConsoleSessionSource
            {
            public:
                uint32_t GetActiveConsoleSessionId() override
                {
                    // 0xFFFFFFFF while no session is attached to the console: kNoConsoleSession.
                    return static_cast<uint32_t>(WTSGetActiveConsoleSessionId());
                }
            };

            // Starts `<exe> --session <id>` in the given session with the service's own SYSTEM token. The processes
            // go into a job that kills them if the service dies, so a crashed service never leaves a helper holding
            // the port.
            class WindowsLauncher : public HelperLauncher
            {
            public:
                explicit WindowsLauncher(const std::wstring& exePath) : m_exePath(exePath)
                {
                    m_job = CreateJobObjectW(nullptr, nullptr);
                    if (m_job != nullptr)
                    {
                        JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits = {};
                        limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
                        SetInformationJobObject(m_job, JobObjectExtendedLimitInformation, &limits, sizeof(limits));
                    }
                }

                ~WindowsLauncher() override
                {
                    if (m_job != nullptr)
                    {
                        CloseHandle(m_job); // kills any helper still running
                    }
                }

                bool Launch(uint32_t sessionId, HelperHandle& handle, std::string& error) override
                {
                    HANDLE own = nullptr;
                    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_DUPLICATE | TOKEN_QUERY, &own))
                    {
                        error = ErrorText("could not open the service's token");
                        return false;
                    }
                    HANDLE primary = nullptr;
                    BOOL duplicated = DuplicateTokenEx(own, MAXIMUM_ALLOWED, nullptr, SecurityImpersonation,
                                                       TokenPrimary, &primary);
                    CloseHandle(own);
                    if (!duplicated)
                    {
                        error = ErrorText("could not copy the service's token");
                        return false;
                    }
                    // Putting the token in another session needs SeTcbPrivilege, which LocalSystem has.
                    DWORD session = sessionId;
                    if (!SetTokenInformation(primary, TokenSessionId, &session, sizeof(session)))
                    {
                        error = ErrorText("could not move the token to the console session");
                        CloseHandle(primary);
                        return false;
                    }
                    LPVOID environment = nullptr;
                    if (!CreateEnvironmentBlock(&environment, primary, FALSE))
                    {
                        environment = nullptr; // start without a fresh environment rather than not at all
                    }

                    std::wstring commandLine = L"\"" + m_exePath + L"\" --session " + std::to_wstring(sessionId);
                    STARTUPINFOW startup = {};
                    startup.cb = sizeof(startup);
                    // The interactive window station's default desktop; the helper moves itself to the sign-in or
                    // secure desktop when that one receives input (FollowInputDesktop).
                    wchar_t desktop[] = L"winsta0\\default";
                    startup.lpDesktop = desktop;
                    PROCESS_INFORMATION process = {};
                    DWORD flags = CREATE_UNICODE_ENVIRONMENT | CREATE_NO_WINDOW | CREATE_SUSPENDED;
                    BOOL created = CreateProcessAsUserW(primary, nullptr, &commandLine[0], nullptr, nullptr, FALSE,
                                                        flags, environment, nullptr, &startup, &process);
                    if (!created)
                    {
                        error = ErrorText("could not start the session helper");
                    }
                    if (environment != nullptr)
                    {
                        DestroyEnvironmentBlock(environment);
                    }
                    CloseHandle(primary);
                    if (!created)
                    {
                        return false;
                    }
                    if (m_job != nullptr)
                    {
                        AssignProcessToJobObject(m_job, process.hProcess);
                    }
                    ResumeThread(process.hThread);
                    CloseHandle(process.hThread);
                    handle = static_cast<HelperHandle>(reinterpret_cast<uintptr_t>(process.hProcess));
                    LOG_INFO("Service", "Started the session helper (process %lu) in session %u",
                             static_cast<unsigned long>(process.dwProcessId), static_cast<unsigned>(sessionId));
                    return true;
                }

                bool IsRunning(HelperHandle handle) override
                {
                    return WaitForSingleObject(ToHandle(handle), 0) == WAIT_TIMEOUT;
                }

                void Terminate(HelperHandle handle) override
                {
                    HANDLE process = ToHandle(handle);
                    if (WaitForSingleObject(process, 0) == WAIT_TIMEOUT)
                    {
                        // Forced: the viewer's connection drops and its reconnect bar brings it back. A gentler
                        // stop (the helper closing its listener first) is a possible later refinement.
                        TerminateProcess(process, 1);
                        WaitForSingleObject(process, 3000);
                    }
                    CloseHandle(process);
                }

            private:
                static HANDLE ToHandle(HelperHandle handle)
                {
                    return reinterpret_cast<HANDLE>(static_cast<uintptr_t>(handle));
                }

                std::wstring m_exePath;
                HANDLE m_job = nullptr;
            };

            // -- The service entry point -------------------------------------------------------------------------

            SERVICE_STATUS_HANDLE g_statusHandle = nullptr;
            HANDLE g_stopEvent = nullptr;
            HANDLE g_wakeEvent = nullptr; // a session changed: look at the console session now
            HANDLE g_sasEvent = nullptr;  // the helper wants Ctrl+Alt+Del sent
            bool g_serviceRan = false;

            void ReportStatus(DWORD state, DWORD controlsAccepted, DWORD waitHintMs = 0)
            {
                SERVICE_STATUS status = {};
                status.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
                status.dwCurrentState = state;
                status.dwControlsAccepted = controlsAccepted;
                status.dwWaitHint = waitHintMs;
                SetServiceStatus(g_statusHandle, &status);
            }

            DWORD WINAPI ControlHandler(DWORD control, DWORD /*eventType*/, LPVOID /*eventData*/, LPVOID /*context*/)
            {
                switch (control)
                {
                case SERVICE_CONTROL_STOP:
                case SERVICE_CONTROL_SHUTDOWN:
                    ReportStatus(SERVICE_STOP_PENDING, 0, 10000);
                    SetEvent(g_stopEvent);
                    return NO_ERROR;
                case SERVICE_CONTROL_SESSIONCHANGE:
                    SetEvent(g_wakeEvent); // logon, logoff, console connect and disconnect, lock, unlock
                    return NO_ERROR;
                case SERVICE_CONTROL_INTERROGATE:
                    return NO_ERROR;
                default:
                    return ERROR_CALL_NOT_IMPLEMENTED;
                }
            }

            // Only the SYSTEM account and administrators may touch the Ctrl+Alt+Del request event.
            HANDLE MakeSasEvent()
            {
                SECURITY_ATTRIBUTES attributes = {};
                attributes.nLength = sizeof(attributes);
                PSECURITY_DESCRIPTOR descriptor = nullptr;
                if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(L"D:(A;;GA;;;SY)(A;;GA;;;BA)",
                                                                          SDDL_REVISION_1, &descriptor, nullptr))
                {
                    return nullptr;
                }
                attributes.lpSecurityDescriptor = descriptor;
                HANDLE event = CreateEventW(&attributes, FALSE, FALSE, kSasEventName); // auto-reset
                LocalFree(descriptor);
                return event;
            }

            void SendSasFromService()
            {
                HMODULE sas = LoadLibraryW(L"sas.dll");
                if (sas == nullptr)
                {
                    LOG_INFO("Service", "sas.dll could not be loaded, so Ctrl+Alt+Del was not sent");
                    return;
                }
                using SendSasFunction = VOID(WINAPI*)(BOOL);
                SendSasFunction sendSas = reinterpret_cast<SendSasFunction>(GetProcAddress(sas, "SendSAS"));
                if (sendSas != nullptr)
                {
                    sendSas(FALSE); // FALSE: called from a service
                }
                FreeLibrary(sas);
            }

            void WINAPI ServiceMain(DWORD /*argc*/, LPWSTR* /*argv*/)
            {
                g_statusHandle = RegisterServiceCtrlHandlerExW(kServiceName, ControlHandler, nullptr);
                if (g_statusHandle == nullptr)
                {
                    return;
                }
                g_serviceRan = true;
                ReportStatus(SERVICE_START_PENDING, 0, 5000);
                g_stopEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
                g_wakeEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
                g_sasEvent = MakeSasEvent();
                if (g_stopEvent == nullptr || g_wakeEvent == nullptr)
                {
                    ReportStatus(SERVICE_STOPPED, 0);
                    return;
                }
                ReportStatus(SERVICE_RUNNING, SERVICE_ACCEPT_STOP | SERVICE_ACCEPT_SHUTDOWN |
                                                  SERVICE_ACCEPT_SESSIONCHANGE);

                {
                    WindowsSessions sessions;
                    WindowsLauncher launcher(GetExePath());
                    ServiceSupervisor supervisor(sessions, launcher);
                    HANDLE waits[3] = {g_stopEvent, g_wakeEvent, g_sasEvent};
                    DWORD count = g_sasEvent != nullptr ? 3 : 2;
                    for (;;)
                    {
                        supervisor.Tick(static_cast<int64_t>(GetTickCount64()));
                        // A second is the longest the helper can be gone before it is noticed; session changes and
                        // the stop request wake the loop at once.
                        DWORD woken = WaitForMultipleObjects(count, waits, FALSE, 1000);
                        if (woken == WAIT_OBJECT_0)
                        {
                            break;
                        }
                        if (woken == WAIT_OBJECT_0 + 2)
                        {
                            SendSasFromService();
                        }
                    }
                    supervisor.Stop();
                }

                CloseHandle(g_stopEvent);
                CloseHandle(g_wakeEvent);
                if (g_sasEvent != nullptr)
                {
                    CloseHandle(g_sasEvent);
                }
                ReportStatus(SERVICE_STOPPED, 0);
            }

            constexpr const wchar_t* kPolicyKey = L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Policies\\System";
            constexpr const wchar_t* kSasPolicyValue = L"SoftwareSASGeneration";
        } // namespace

        bool RunAsService(std::string& error)
        {
            wchar_t name[] = L"MarconiServer";
            SERVICE_TABLE_ENTRYW table[] = {{name, ServiceMain}, {nullptr, nullptr}};
            if (!StartServiceCtrlDispatcherW(table))
            {
                error = ErrorText("could not connect to the Service Control Manager (run by Windows, not by hand)");
                return false;
            }
            return g_serviceRan;
        }

        bool InstallService(bool startNow, std::string& error)
        {
            SC_HANDLE manager = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_ALL_ACCESS);
            if (manager == nullptr)
            {
                error = ErrorText("could not open the Service Control Manager (run as administrator)");
                return false;
            }
            std::wstring binary = L"\"" + GetExePath() + L"\" --service";
            SC_HANDLE service = CreateServiceW(manager, kServiceName, kServiceDisplayName, SERVICE_ALL_ACCESS,
                                               SERVICE_WIN32_OWN_PROCESS, SERVICE_AUTO_START, SERVICE_ERROR_NORMAL,
                                               binary.c_str(), nullptr, nullptr, nullptr, nullptr, nullptr);
            if (service == nullptr && GetLastError() == ERROR_SERVICE_EXISTS)
            {
                // An upgrade: point the existing service at this exe.
                service = OpenServiceW(manager, kServiceName, SERVICE_ALL_ACCESS);
                if (service != nullptr &&
                    !ChangeServiceConfigW(service, SERVICE_WIN32_OWN_PROCESS, SERVICE_AUTO_START,
                                          SERVICE_ERROR_NORMAL, binary.c_str(), nullptr, nullptr, nullptr, nullptr,
                                          nullptr, kServiceDisplayName))
                {
                    error = ErrorText("could not update the existing service");
                    CloseServiceHandle(service);
                    CloseServiceHandle(manager);
                    return false;
                }
            }
            if (service == nullptr)
            {
                error = ErrorText("could not create the service");
                CloseServiceHandle(manager);
                return false;
            }

            SERVICE_DESCRIPTIONW description = {const_cast<LPWSTR>(kServiceDescription)};
            ChangeServiceConfig2W(service, SERVICE_CONFIG_DESCRIPTION, &description);

            // If the supervisor itself crashes, Windows starts it again after 5 s, 10 s, then every 30 s.
            SC_ACTION actions[3] = {{SC_ACTION_RESTART, 5000}, {SC_ACTION_RESTART, 10000}, {SC_ACTION_RESTART, 30000}};
            SERVICE_FAILURE_ACTIONSW failure = {};
            failure.dwResetPeriod = 24 * 60 * 60;
            failure.cActions = 3;
            failure.lpsaActions = actions;
            ChangeServiceConfig2W(service, SERVICE_CONFIG_FAILURE_ACTIONS, &failure);

            // Let services send Ctrl+Alt+Del (SendSAS): 1 = services only. The helper asks the service to do it.
            HKEY key = nullptr;
            if (RegCreateKeyExW(HKEY_LOCAL_MACHINE, kPolicyKey, 0, nullptr, 0, KEY_SET_VALUE, nullptr, &key,
                                nullptr) == ERROR_SUCCESS)
            {
                DWORD value = 1;
                RegSetValueExW(key, kSasPolicyValue, 0, REG_DWORD, reinterpret_cast<const BYTE*>(&value),
                               sizeof(value));
                RegCloseKey(key);
            }

            bool ok = true;
            if (startNow && !StartServiceW(service, 0, nullptr) && GetLastError() != ERROR_SERVICE_ALREADY_RUNNING)
            {
                error = ErrorText("the service was installed but did not start");
                ok = false;
            }
            CloseServiceHandle(service);
            CloseServiceHandle(manager);
            return ok;
        }

        bool RemoveService(std::string& error)
        {
            SC_HANDLE manager = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_ALL_ACCESS);
            if (manager == nullptr)
            {
                error = ErrorText("could not open the Service Control Manager (run as administrator)");
                return false;
            }
            SC_HANDLE service = OpenServiceW(manager, kServiceName, SERVICE_ALL_ACCESS);
            if (service == nullptr)
            {
                bool missing = GetLastError() == ERROR_SERVICE_DOES_NOT_EXIST;
                if (!missing)
                {
                    error = ErrorText("could not open the service");
                }
                CloseServiceHandle(manager);
                return missing;
            }
            SERVICE_STATUS status = {};
            if (ControlService(service, SERVICE_CONTROL_STOP, &status))
            {
                for (int i = 0; i < 150 && status.dwCurrentState != SERVICE_STOPPED; ++i)
                {
                    Sleep(100);
                    if (!QueryServiceStatus(service, &status))
                    {
                        break;
                    }
                }
            }
            bool ok = DeleteService(service) || GetLastError() == ERROR_SERVICE_MARKED_FOR_DELETE;
            if (!ok)
            {
                error = ErrorText("could not delete the service");
            }
            CloseServiceHandle(service);
            CloseServiceHandle(manager);

            HKEY key = nullptr;
            if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, kPolicyKey, 0, KEY_SET_VALUE, &key) == ERROR_SUCCESS)
            {
                RegDeleteValueW(key, kSasPolicyValue);
                RegCloseKey(key);
            }
            return ok;
        }
    } // namespace Server
} // namespace Marconi
