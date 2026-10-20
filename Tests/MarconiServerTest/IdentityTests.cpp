// Copyright (c) 2026 Red Marmoset Studios Ltd.
// LoadOrCreateIdentity: makes a certificate and key on first run, reuses them after, and refuses a half-made pair

#include "TestSupport.h"

#include <MarconiServerCore/IdentityStore.h>

#include <chrono>
#include <cstdio>
#include <string>

namespace MarconiServerTest
{
    using namespace Marconi;

    namespace
    {
        // A unique path in the system temp area, so parallel runs and leftovers don't clash.
        std::string TempPath(const char* suffix)
        {
            const char* dir = std::getenv("TMPDIR");
            std::string base = dir && *dir ? dir : "/tmp";
            auto now = std::chrono::steady_clock::now().time_since_epoch().count();
            return base + "/marconi-identity-" + std::to_string(now) + suffix;
        }

        bool FileExists(const std::string& path)
        {
            FILE* file = std::fopen(path.c_str(), "rb");
            if (file)
            {
                std::fclose(file);
                return true;
            }
            return false;
        }
    } // namespace

    void RunIdentityTests()
    {
        RunTest("The server makes an identity on first run and reuses it after", []() {
            std::string certPath = TempPath("-cert.pem");
            std::string keyPath = TempPath("-key.pem");
            std::remove(certPath.c_str());
            std::remove(keyPath.c_str());

            // First run: neither file exists, so a fresh identity is made and saved.
            Server::Settings first;
            std::string error;
            CHECK(Server::LoadOrCreateIdentity(certPath, keyPath, "Test PC", first, error));
            CHECK(error.empty());
            CHECK(!first.certificatePem.empty() && !first.privateKeyPem.empty());
            CHECK(FileExists(certPath) && FileExists(keyPath));

            // Second run: both files exist, so the same identity comes back unchanged.
            Server::Settings second;
            CHECK(Server::LoadOrCreateIdentity(certPath, keyPath, "Test PC", second, error));
            CHECK(second.certificatePem == first.certificatePem);
            CHECK(second.privateKeyPem == first.privateKeyPem);

            // Only one of the pair present is a mistake worth catching, not a reason to overwrite the other.
            std::remove(keyPath.c_str());
            Server::Settings third;
            CHECK(!Server::LoadOrCreateIdentity(certPath, keyPath, "Test PC", third, error));
            CHECK(!error.empty());

            std::remove(certPath.c_str());
            std::remove(keyPath.c_str());
        });
    }
} // namespace MarconiServerTest
