// Copyright (c) 2026 Red Marmoset Studios Ltd.
// The password hashing and the lockout

#include "TestSupport.h"

#include <MarconiServerCore/Auth.h>
#include <MarconiServerCore/PasswordHash.h>

namespace MarconiServerTest
{
    using namespace Marconi;

    void RunPasswordTests()
    {
        RunTest("A password hash verifies the right password and rejects the wrong one", []() {
            std::string stored = Server::HashPassword("correct horse", 10000);
            CHECK(!stored.empty());
            CHECK(stored.compare(0, 7, "pbkdf2$") == 0);
            CHECK(Server::HashMatches(stored, "correct horse"));
            CHECK(!Server::HashMatches(stored, "wrong"));
            CHECK(!Server::HashMatches(stored, "correct horse ")); // a trailing space is a different password
            // A junk or empty stored value never matches, rather than crashing.
            CHECK(!Server::HashMatches("", "correct horse"));
            CHECK(!Server::HashMatches("pbkdf2$bad", "correct horse"));
            CHECK(!Server::HashMatches("not a hash at all", "x"));
        });

        RunTest("Hashing the same password twice gives different strings (a random salt)", []() {
            std::string a = Server::HashPassword("same", 10000);
            std::string b = Server::HashPassword("same", 10000);
            CHECK(a != b);               // different salts
            CHECK(Server::HashMatches(a, "same") && Server::HashMatches(b, "same")); // both still verify
        });

        RunTest("No password set lets anyone in", []() {
            Server::Auth auth("");
            CHECK(!auth.HasPassword());
            int retry = 0;
            CHECK(auth.Check("anything", retry) == Server::Auth::Result::Allowed);
            CHECK(auth.Check("", retry) == Server::Auth::Result::Allowed);
        });

        RunTest("Wrong passwords eventually lock login out", []() {
            Server::Auth auth(Server::HashPassword("secret", 10000));
            CHECK(auth.HasPassword());
            int retry = 0;
            // The right password is allowed.
            CHECK(auth.Check("secret", retry) == Server::Auth::Result::Allowed);
            // Wrong passwords are rejected; after enough of them, login is locked out with a wait.
            bool lockedOut = false;
            for (int i = 0; i < 10; ++i)
            {
                Server::Auth::Result result = auth.Check("nope", retry);
                if (result == Server::Auth::Result::LockedOut)
                {
                    lockedOut = true;
                    CHECK(retry > 0);
                    break;
                }
                CHECK(result == Server::Auth::Result::WrongPassword);
            }
            CHECK(lockedOut);
            // While locked out, even the right password waits.
            CHECK(auth.Check("secret", retry) == Server::Auth::Result::LockedOut);
        });
    }
} // namespace MarconiServerTest
