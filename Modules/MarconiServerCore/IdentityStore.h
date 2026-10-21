// Copyright (c) 2026 Red Marmoset Studios Ltd.
// Keeping the server's TLS identity (certificate and key) on disk, so its fingerprint stays the same across restarts

#pragma once

#include "Settings.h"

#include <string>

namespace Marconi
{
    namespace Server
    {
        // Puts a certificate and private key into `settings`. If both files already exist, loads them; if neither
        // does, makes a fresh self-signed identity for `name` and saves it, so the next run reuses it (the key is
        // written so only its owner can read it). False (with `error`) on a real problem: one file present without
        // the other, a key that can't be written, or a failure making the certificate.
        bool LoadOrCreateIdentity(const std::string& certificatePath, const std::string& keyPath,
                                  const std::string& name, Settings& settings, std::string& error);
    } // namespace Server
} // namespace Marconi
