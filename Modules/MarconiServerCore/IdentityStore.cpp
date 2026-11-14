// Copyright (c) 2026 Red Marmoset Studios Ltd.
// Reading and writing the server's TLS identity

#include "IdentityStore.h"

#include <Crypto/TlsServer.h>

#include <fstream>
#include <sstream>

#ifndef _WIN32
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace Marconi
{
    namespace Server
    {
        namespace
        {
            // Reads a whole file into `out`. False if it can't be opened (for a cert/key pair, that means "make a
            // new one").
            bool ReadWholeFile(const std::string& path, std::string& out)
            {
                std::ifstream file(path, std::ios::binary);
                if (!file)
                {
                    return false;
                }
                std::ostringstream contents;
                contents << file.rdbuf();
                out = contents.str();
                return true;
            }

            // Writes `contents` to `path`. On POSIX a private key is written owner-only, so it isn't left
            // world-readable.
            bool WriteWholeFile(const std::string& path, const std::string& contents, bool privateKey)
            {
#ifndef _WIN32
                if (privateKey)
                {
                    // Made owner-only before anything is written to it, so there is never a moment when another
                    // account could open it. (Any older file is removed first: its permissions would be kept.)
                    unlink(path.c_str());
                    int made = open(path.c_str(), O_WRONLY | O_CREAT | O_EXCL, S_IRUSR | S_IWUSR);
                    if (made < 0)
                    {
                        return false;
                    }
                    close(made);
                }
#endif
                std::ofstream file(path, std::ios::binary | std::ios::trunc);
                if (!file)
                {
                    return false;
                }
                file << contents;
                file.close();
                if (!file)
                {
                    return false;
                }
#ifndef _WIN32
                if (privateKey)
                {
                    chmod(path.c_str(), S_IRUSR | S_IWUSR); // 0600
                }
#else
                (void)privateKey;
#endif
                return true;
            }
        } // namespace

        bool LoadOrCreateIdentity(const std::string& certificatePath, const std::string& keyPath,
                                  const std::string& name, Settings& settings, std::string& error)
        {
            std::string cert;
            std::string key;
            bool haveCert = ReadWholeFile(certificatePath, cert);
            bool haveKey = ReadWholeFile(keyPath, key);
            if (haveCert && haveKey)
            {
                settings.certificatePem = cert;
                settings.privateKeyPem = key;
                return true;
            }
            if (haveCert != haveKey)
            {
                error = "Only one of the certificate and key files exists; move or delete it so a matching pair can "
                        "be made.";
                return false;
            }

            Crypto::TlsIdentity identity;
            if (!Crypto::MakeSelfSignedIdentity(name.empty() ? "Marconi Server" : name, 3650, identity, error))
            {
                return false;
            }
            if (!WriteWholeFile(certificatePath, identity.certificatePem, false) ||
                !WriteWholeFile(keyPath, identity.privateKeyPem, true))
            {
                error = "Could not save the certificate or key.";
                return false;
            }
            settings.certificatePem = identity.certificatePem;
            settings.privateKeyPem = identity.privateKeyPem;
            return true;
        }
    } // namespace Server
} // namespace Marconi
