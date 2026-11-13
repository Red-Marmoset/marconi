// Copyright (c) 2026 Red Marmoset Studios Ltd.
// The password hashing and the lockout

#include "TestSupport.h"

#include <cstdio>

#include <MarconiServerCore/Auth.h>
#include <MarconiServerCore/PasswordFile.h>
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

        RunTest("A password file is read as UTF-16 or UTF-8, minus one line break, and can be erased", []() {
            const std::string path = "marconi_password_file_test.txt";
            auto write = [&](const std::string& bytes) {
                FILE* file = fopen(path.c_str(), "wb");
                fwrite(bytes.data(), 1, bytes.size(), file);
                fclose(file);
            };
            std::string password;
            std::string error;

            // UTF-16 little-endian with a mark, as the installer writes it: "p", a-umlaut, "ss" and an emoji.
            write(std::string("\xFF\xFE"
                              "p\0\xE4\0s\0s\0\x3D\xD8\x00\xDE",
                              14));
            CHECK(Server::ReadPasswordFile(path, password, error));
            CHECK(password == "p\xC3\xA4ss\xF0\x9F\x98\x80");

            write("\xEF\xBB\xBFhunter2\r\n");
            CHECK(Server::ReadPasswordFile(path, password, error));
            CHECK(password == "hunter2");

            write("two lines\n\n"); // only one line break goes
            CHECK(Server::ReadPasswordFile(path, password, error));
            CHECK(password == "two lines\n");

            write("");
            CHECK(!Server::ReadPasswordFile(path, password, error));
            write(std::string("\xFF\xFE"
                              "a",
                              3));
            CHECK(!Server::ReadPasswordFile(path, password, error)); // half a character
            write(std::string("\xFF\xFE\x3D\xD8"
                              "a\0",
                              6));
            CHECK(!Server::ReadPasswordFile(path, password, error)); // a lone surrogate

            CHECK(Server::ErasePasswordFile(path));
            FILE* gone = fopen(path.c_str(), "rb");
            CHECK(gone == nullptr);
            if (gone != nullptr)
            {
                fclose(gone);
            }
            CHECK(!Server::ReadPasswordFile("marconi_no_such_password_file.txt", password, error));
        });
    }
} // namespace MarconiServerTest
