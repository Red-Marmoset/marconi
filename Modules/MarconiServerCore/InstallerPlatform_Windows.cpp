// Copyright (c) 2026 Red Marmoset Studios Ltd.
// Self-update on Windows: checking an installer's Authenticode signature and version information, and starting it
//
// NOT YET BUILT ON WINDOWS. Written on Linux from the Windows SDK documentation (WinVerifyTrust, WTHelper*,
// CertGetNameStringW, GetFileVersionInfoW, ShellExecuteExW); it has never been compiled or run. SERVER_PLAN.md,
// "Self-update", lists what to try on a real PC.
//
// Signature: WinVerifyTrust with the generic Authenticode policy, no user interface and no revocation check (a
// revocation check needs the network and would fail on a computer that can't reach the certificate authority; the
// owner may choose otherwise). The signer is the leaf certificate's simple display name (usually its CN).
//
// Starting the installer: the tray program runs as the signed-in user and the installer needs administrator rights,
// so it is started with the "runas" verb, which shows Windows' permission prompt on this computer's screen. That
// prompt appears on the secure desktop, which a program running as the user can neither capture nor click: the person
// at the computer has to answer it (a viewer sees the picture stop until they do). A program already running as an
// administrator or as SYSTEM starts it directly, detached and outside any job, so closing this program doesn't end it.

#include "UpdateHost.h"

#include <Log/Log.h>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shellapi.h>
#include <softpub.h>
#include <wincrypt.h>
#include <wintrust.h>

#include <string>
#include <vector>

#pragma comment(lib, "wintrust.lib")
#pragma comment(lib, "crypt32.lib")
#pragma comment(lib, "version.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "advapi32.lib")

namespace Marconi
{
    namespace Server
    {
        namespace
        {
            std::wstring Widen(const std::string& utf8)
            {
                if (utf8.empty())
                {
                    return std::wstring();
                }
                int count = MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), nullptr, 0);
                std::wstring wide(static_cast<size_t>(count), L'\0');
                MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), &wide[0], count);
                return wide;
            }

            std::string Narrow(const wchar_t* wide)
            {
                if (wide == nullptr || *wide == L'\0')
                {
                    return std::string();
                }
                int count = WideCharToMultiByte(CP_UTF8, 0, wide, -1, nullptr, 0, nullptr, nullptr);
                std::string utf8(static_cast<size_t>(count > 0 ? count - 1 : 0), '\0');
                if (count > 1)
                {
                    WideCharToMultiByte(CP_UTF8, 0, wide, -1, &utf8[0], count, nullptr, nullptr);
                }
                return utf8;
            }

            // The Authenticode signature and its signer's name.
            void ReadSignature(const std::wstring& path, InstallerFacts& facts)
            {
                WINTRUST_FILE_INFO file = {};
                file.cbStruct = sizeof(file);
                file.pcwszFilePath = path.c_str();

                WINTRUST_DATA data = {};
                data.cbStruct = sizeof(data);
                data.dwUIChoice = WTD_UI_NONE;
                data.fdwRevocationChecks = WTD_REVOKE_NONE;
                data.dwUnionChoice = WTD_CHOICE_FILE;
                data.pFile = &file;
                data.dwStateAction = WTD_STATEACTION_VERIFY;
                data.dwProvFlags = WTD_SAFER_FLAG;

                GUID policy = WINTRUST_ACTION_GENERIC_VERIFY_V2;
                LONG status = WinVerifyTrust(static_cast<HWND>(INVALID_HANDLE_VALUE), &policy, &data);
                if (status == ERROR_SUCCESS)
                {
                    facts.signature = SignatureState::Valid;
                    CRYPT_PROVIDER_DATA* provider = WTHelperProvDataFromStateData(data.hWVTStateData);
                    CRYPT_PROVIDER_SGNR* signer =
                        provider != nullptr ? WTHelperGetProvSignerFromChain(provider, 0, FALSE, 0) : nullptr;
                    CRYPT_PROVIDER_CERT* cert = signer != nullptr ? WTHelperGetProvCertFromChain(signer, 0) : nullptr;
                    if (cert != nullptr && cert->pCert != nullptr)
                    {
                        wchar_t name[512] = {};
                        CertGetNameStringW(cert->pCert, CERT_NAME_SIMPLE_DISPLAY_TYPE, 0, nullptr, name, 512);
                        facts.signer = Narrow(name);
                    }
                    if (facts.signer.empty())
                    {
                        facts.signature = SignatureState::Invalid; // signed, but whose can't be told: not usable
                    }
                }
                else if (status == TRUST_E_NOSIGNATURE || status == TRUST_E_SUBJECT_FORM_UNKNOWN ||
                         status == TRUST_E_PROVIDER_UNKNOWN)
                {
                    facts.signature = SignatureState::Missing;
                }
                else
                {
                    facts.signature = SignatureState::Invalid;
                    LOG_INFO("Server", "WinVerifyTrust said 0x%08lx for an update installer",
                             static_cast<unsigned long>(status));
                }
                data.dwStateAction = WTD_STATEACTION_CLOSE;
                WinVerifyTrust(static_cast<HWND>(INVALID_HANDLE_VALUE), &policy, &data);
            }

            // ProductName and ProductVersion from the file's version information (the installer's VIAddVersionKey).
            void ReadVersionInfo(const std::wstring& path, InstallerFacts& facts)
            {
                DWORD ignored = 0;
                DWORD size = GetFileVersionInfoSizeW(path.c_str(), &ignored);
                if (size == 0)
                {
                    return;
                }
                std::vector<uint8_t> block(size);
                if (!GetFileVersionInfoW(path.c_str(), 0, size, block.data()))
                {
                    return;
                }
                struct Translation
                {
                    WORD language;
                    WORD codePage;
                };
                std::vector<Translation> tried;
                Translation* translations = nullptr;
                UINT length = 0;
                if (VerQueryValueW(block.data(), L"\\VarFileInfo\\Translation", reinterpret_cast<void**>(&translations),
                                   &length) &&
                    translations != nullptr)
                {
                    for (UINT i = 0; i < length / sizeof(Translation); ++i)
                    {
                        tried.push_back(translations[i]);
                    }
                }
                tried.push_back({0x0000, 0x04B0}); // NSIS's /LANG=0 in a Unicode installer
                tried.push_back({0x0409, 0x04B0});
                for (const Translation& t : tried)
                {
                    wchar_t key[96];
                    wchar_t* value = nullptr;
                    UINT valueLength = 0;
                    swprintf_s(key, L"\\StringFileInfo\\%04x%04x\\ProductName", t.language, t.codePage);
                    if (!VerQueryValueW(block.data(), key, reinterpret_cast<void**>(&value), &valueLength) ||
                        value == nullptr)
                    {
                        continue;
                    }
                    facts.product = Narrow(value);
                    value = nullptr;
                    swprintf_s(key, L"\\StringFileInfo\\%04x%04x\\ProductVersion", t.language, t.codePage);
                    if (VerQueryValueW(block.data(), key, reinterpret_cast<void**>(&value), &valueLength) &&
                        value != nullptr)
                    {
                        facts.version = Narrow(value);
                    }
                    return;
                }
            }

            InstallerFacts InspectPath(const std::wstring& path)
            {
                InstallerFacts facts;
                ReadSignature(path, facts);
                ReadVersionInfo(path, facts);
                return facts;
            }

            class WindowsInspector : public InstallerInspector
            {
            public:
                InstallerFacts Inspect(const std::string& path) override { return InspectPath(Widen(path)); }

                InstallerFacts InspectRunningServer() override
                {
                    if (!m_runningKnown)
                    {
                        wchar_t path[MAX_PATH * 2] = {};
                        DWORD length = GetModuleFileNameW(nullptr, path, MAX_PATH * 2);
                        if (length > 0 && length < MAX_PATH * 2)
                        {
                            m_running = InspectPath(path);
                        }
                        m_runningKnown = true; // the running program doesn't change: checked once
                    }
                    return m_running;
                }

            private:
                bool m_runningKnown = false;
                InstallerFacts m_running;
            };

            bool IsElevated()
            {
                HANDLE token = nullptr;
                if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token))
                {
                    return false;
                }
                TOKEN_ELEVATION elevation = {};
                DWORD size = 0;
                bool elevated = GetTokenInformation(token, TokenElevation, &elevation, sizeof(elevation), &size) &&
                                elevation.TokenIsElevated != 0;
                CloseHandle(token);
                return elevated;
            }

            std::wstring QuoteArguments(const std::vector<std::string>& arguments)
            {
                std::wstring line;
                for (const std::string& argument : arguments)
                {
                    if (!line.empty())
                    {
                        line += L' ';
                    }
                    line += Widen(argument); // only our own fixed switches ("/S", "/restart"): nothing to quote
                }
                return line;
            }

            class WindowsLauncher : public InstallerLauncher
            {
            public:
                ~WindowsLauncher() override
                {
                    if (m_lock != INVALID_HANDLE_VALUE)
                    {
                        CloseHandle(m_lock);
                    }
                }

                bool Launch(const std::string& installerPath, const std::vector<std::string>& arguments,
                            std::string& error) override
                {
                    const std::wstring path = Widen(installerPath);
                    // Held open (others may read, not change or delete) from the checks until this program ends, so
                    // the file that runs is the file that was checked.
                    if (m_lock != INVALID_HANDLE_VALUE)
                    {
                        CloseHandle(m_lock);
                    }
                    m_lock = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                                         FILE_ATTRIBUTE_NORMAL, nullptr);
                    if (m_lock == INVALID_HANDLE_VALUE)
                    {
                        error = "could not open the installer to start it (" + std::to_string(GetLastError()) + ")";
                        return false;
                    }
                    std::wstring parameters = QuoteArguments(arguments);
                    if (IsElevated())
                    {
                        std::wstring commandLine = L"\"" + path + L"\" " + parameters;
                        STARTUPINFOW startup = {};
                        startup.cb = sizeof(startup);
                        PROCESS_INFORMATION process = {};
                        DWORD flags = DETACHED_PROCESS | CREATE_NEW_PROCESS_GROUP | CREATE_BREAKAWAY_FROM_JOB;
                        BOOL ok = CreateProcessW(path.c_str(), &commandLine[0], nullptr, nullptr, FALSE, flags, nullptr,
                                                 nullptr, &startup, &process);
                        if (!ok && GetLastError() == ERROR_ACCESS_DENIED)
                        {
                            // The job this program runs in doesn't allow breaking away: start it inside it instead.
                            flags &= ~static_cast<DWORD>(CREATE_BREAKAWAY_FROM_JOB);
                            ok = CreateProcessW(path.c_str(), &commandLine[0], nullptr, nullptr, FALSE, flags, nullptr,
                                                nullptr, &startup, &process);
                        }
                        if (!ok)
                        {
                            error = "could not start the installer (" + std::to_string(GetLastError()) + ")";
                            return false;
                        }
                        CloseHandle(process.hThread);
                        CloseHandle(process.hProcess);
                        return true;
                    }
                    SHELLEXECUTEINFOW info = {};
                    info.cbSize = sizeof(info);
                    info.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_FLAG_NO_UI | SEE_MASK_NOASYNC;
                    info.lpVerb = L"runas"; // Windows' permission prompt, on this computer's screen
                    info.lpFile = path.c_str();
                    info.lpParameters = parameters.c_str();
                    info.nShow = SW_SHOWNORMAL;
                    if (!ShellExecuteExW(&info))
                    {
                        DWORD code = GetLastError();
                        error = code == ERROR_CANCELLED ? "permission to run the installer was refused on this computer"
                                                        : "could not start the installer (" + std::to_string(code) + ")";
                        return false;
                    }
                    if (info.hProcess != nullptr)
                    {
                        CloseHandle(info.hProcess);
                    }
                    return true;
                }

            private:
                HANDLE m_lock = INVALID_HANDLE_VALUE;
            };
        } // namespace

        std::unique_ptr<InstallerInspector> CreatePlatformInstallerInspector()
        {
            return std::make_unique<WindowsInspector>();
        }

        std::unique_ptr<InstallerLauncher> CreatePlatformInstallerLauncher()
        {
            return std::make_unique<WindowsLauncher>();
        }
    } // namespace Server
} // namespace Marconi
