// Copyright (c) 2026 Red Marmoset Studios Ltd.
// Tests for the update check: version numbers, GitHub's releases list, and the HTTP response reader. An opt-in test
// (MARCONI_UPDATE_TEST=1) asks GitHub for real.

#include "TestSupport.h"

#include <MarconiCore/Net/Https.h>
#include <MarconiCore/Update.h>
#include <MarconiCore/Version.h>

#include <cstdio>
#include <cstdlib>
#include <string>
#include <thread>

namespace MarconiTest
{
    using namespace Marconi;

    namespace
    {
        // The shape of GitHub's answer, cut down: a draft first, then two published pre-releases out of order.
        const char* const kReleasesJson = R"([
  {"tag_name": "v9.9.9.999", "name": "Draft", "draft": true, "prerelease": false, "html_url": "x", "assets": []},
  {"tag_name": "v0.5.3.160", "name": "Marconi 0.5.3.160", "draft": false, "prerelease": true,
   "html_url": "https://github.com/Red-Marmoset/marconi/releases/tag/v0.5.3.160", "body": "older", "assets": []},
  {"tag_name": "v0.6.0.169", "name": "Marconi 0.6.0.169", "draft": false, "prerelease": true,
   "html_url": "https://github.com/Red-Marmoset/marconi/releases/tag/v0.6.0.169",
   "body": "This pre-release is for 64-bit Windows (x64) and Apple Silicon Macs only.",
   "assets": [
     {"name": "Marconi-0.6.0.169-macos-arm64.dmg", "size": 1,
      "browser_download_url": "https://example.test/download/v0.6.0.169/Marconi-0.6.0.169-macos-arm64.dmg"},
     {"name": "Marconi-0.6.0.169-win64-setup.exe", "size": 2,
      "browser_download_url": "https://example.test/download/v0.6.0.169/Marconi-0.6.0.169-win64-setup.exe"}
   ]},
  {"tag_name": "nightly", "name": "Not a version", "draft": false, "prerelease": true, "html_url": "y", "assets": []}
])";
    } // namespace

    void RunUpdateTests()
    {
        RunTest("Version numbers parse and compare", []() {
            Update::Version a;
            Update::Version b;
            CHECK(Update::Version::Parse("v0.6.0.169", a) && a.parts[0] == 0 && a.parts[1] == 6 && a.parts[3] == 169);
            CHECK(Update::Version::Parse("0.7.0", b) && b.parts[2] == 0 && b.parts[3] == 0);
            CHECK(b.Compare(a) > 0 && a.Compare(b) < 0 && a.Compare(a) == 0);
            CHECK(Update::Version::Parse("0.7.0.176", b) && b.ToString() == "0.7.0.176");
            CHECK(Update::Version::Parse("0.7.1.1", a) && a.Compare(b) > 0);
            CHECK(!Update::Version::Parse("nightly", a));
            CHECK(!Update::Version::Parse("1.2", a));
            CHECK(!Update::Version::Parse("1.2.3.4.5", a));
            CHECK(!Update::Version::Parse("1.2.x", a));
            CHECK(!Update::Version::Parse("", a));
            Update::Version thisBuild;
            CHECK(Update::Version::Parse(GetVersionString(), thisBuild)); // the build's own version is one
        });

        RunTest("The newest published release is picked, with the download for this computer", []() {
            Update::Release release;
            std::string error;
            CHECK(Update::ParseReleases(kReleasesJson, "-win64-setup.exe", release, error));
            CHECK(release.tag == "v0.6.0.169" && release.name == "Marconi 0.6.0.169" && release.prerelease);
            CHECK(release.version.ToString() == "0.6.0.169");
            CHECK(release.pageUrl == "https://github.com/Red-Marmoset/marconi/releases/tag/v0.6.0.169");
            CHECK(release.downloadUrl == "https://example.test/download/v0.6.0.169/Marconi-0.6.0.169-win64-setup.exe");
            CHECK(release.notes.compare(0, 16, "This pre-release") == 0);
            CHECK(Update::ParseReleases(kReleasesJson, "-macos-arm64.dmg", release, error));
            CHECK(release.downloadUrl.find("macos-arm64.dmg") != std::string::npos);
            CHECK(Update::ParseReleases(kReleasesJson, "", release, error) && release.downloadUrl.empty());
            CHECK(Update::ParseReleases(kReleasesJson, "-linux.tar", release, error) && release.downloadUrl.empty());
            CHECK(!Update::ParseReleases("{\"message\": \"Not Found\"}", "", release, error) && !error.empty());
            CHECK(!Update::ParseReleases("not json", "", release, error));
            CHECK(!Update::ParseReleases("[{\"tag_name\": \"nightly\", \"draft\": false}]", "", release, error));
        });

        RunTest("HTTP responses are read with a length, in chunks, or to the end", []() {
            Net::HttpResponse response;
            std::string error;
            CHECK(Net::ParseHttpResponse(
                "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\nContent-Length: 5\r\n\r\nhelloextra", response, error));
            CHECK(response.status == 200 && response.headers["content-type"] == "text/plain");
            CHECK(response.body == "hello");
            CHECK(Net::ParseHttpResponse("HTTP/1.1 404 Not Found\r\nTransfer-Encoding: chunked\r\n\r\n"
                                         "3\r\nabc\r\n2;ext=1\r\nde\r\n0\r\n\r\n",
                                         response, error));
            CHECK(response.status == 404 && response.body == "abcde");
            CHECK(Net::ParseHttpResponse("HTTP/1.0 200 OK\r\n\r\nto the end", response, error));
            CHECK(response.body == "to the end");
            CHECK(!Net::ParseHttpResponse("HTTP/1.1 200 OK\r\nContent-Length: 9\r\n\r\nshort", response, error));
            CHECK(!Net::ParseHttpResponse("HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n5\r\nab", response,
                                          error));
            CHECK(!Net::ParseHttpResponse("<html>", response, error));
            CHECK(!Net::ParseHttpResponse("HTTP/1.1 abc\r\n\r\n", response, error));
        });

        RunTest("This build knows which release file is for it", []() {
            std::string suffix = Update::AssetSuffixForThisBuild();
#if defined(_WIN32)
            CHECK(suffix == "-win64-setup.exe");
#elif defined(__APPLE__)
            CHECK(suffix.compare(0, 7, "-macos-") == 0);
#else
            CHECK(suffix.empty());
#endif
        });

        RunTest("The whole check runs against a server on loopback standing in for GitHub", []() {
            // A stand-in for TLS that just passes bytes through, so the request and the answer can be checked.
            class Passthrough : public Net::TlsStream
            {
            public:
                explicit Passthrough(Net::Socket& socket) : m_socket(socket) {}
                bool Handshake(std::string&) override { return true; }
                ptrdiff_t Read(void* buffer, size_t length) override { return m_socket.Receive(buffer, length); }
                bool Write(const void* data, size_t length) override { return m_socket.SendAll(data, length); }
                void Shutdown() override {}
                std::string GetProtocolName() const override { return "none"; }
                std::vector<uint8_t> GetPeerPublicKey() const override { return {}; }

            private:
                Net::Socket& m_socket;
            };
            FakeServer server;
            Net::HttpsRequest request;
            request.port = server.GetPort();
            request.createTlsStream = [](Net::Socket& socket, const Net::TlsOptions& options) {
                CHECK(options.hostName == "127.0.0.1" && !options.trustDecision); // only a trusted certificate will do
                return std::unique_ptr<Net::TlsStream>(new Passthrough(socket));
            };
            std::string requestText;
            std::thread serverThread([&]() {
                if (!server.AcceptClient())
                {
                    return;
                }
                Net::Socket& client = server.GetClientSocket();
                client.SetReceiveTimeout(5000);
                char buffer[2048];
                while (requestText.find("\r\n\r\n") == std::string::npos)
                {
                    ptrdiff_t n = client.Receive(buffer, sizeof(buffer));
                    if (n <= 0)
                    {
                        break;
                    }
                    requestText.append(buffer, static_cast<size_t>(n));
                }
                std::string body = kReleasesJson;
                char chunkSize[16];
                snprintf(chunkSize, sizeof(chunkSize), "%zx", body.size());
                std::string reply = "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nTransfer-Encoding: chunked\r\n"
                                    "\r\n" + std::string(chunkSize) + "\r\n" + body + "\r\n0\r\n\r\n";
                client.SendAll(reply.data(), reply.size());
                server.Close();
            });
            // Pretend to be an older build, so the sample's 0.6.0.169 counts as newer... and a newer one.
            Update::CheckResult older = Update::CheckForUpdate("0.5.0.100", request, 5000, "127.0.0.1");
            serverThread.join();
            CHECK(older.checked && older.newer && older.release.tag == "v0.6.0.169");
            CHECK(requestText.compare(0, 5, "GET /") == 0);
            CHECK(requestText.find("Host: 127.0.0.1\r\n") != std::string::npos);
            CHECK(requestText.find("User-Agent: Marconi/0.5.0.100\r\n") != std::string::npos);
            CHECK(requestText.find("Accept: application/vnd.github+json\r\n") != std::string::npos);
            CHECK(requestText.find(Update::kReleasesPath) != std::string::npos);
            // A server that answers with an error, or can't be reached, is reported, not treated as an update.
            Net::HttpsRequest unreachable;
            unreachable.port = 1;
            Update::CheckResult failed = Update::CheckForUpdate("0.7.0.1", unreachable, 2000, "127.0.0.1");
            CHECK(!failed.checked && !failed.newer && failed.error.find("Couldn't reach GitHub") == 0);
            Update::CheckResult badVersion = Update::CheckForUpdate("dev", unreachable, 2000, "127.0.0.1");
            CHECK(!badVersion.checked && badVersion.error.find("dev") != std::string::npos);
        });

        if (std::getenv("MARCONI_UPDATE_TEST") == nullptr)
        {
            printf("  (set MARCONI_UPDATE_TEST=1 to ask GitHub for the newest release for real)\n");
            return;
        }
        RunTest("GitHub answers the update check", []() {
            Net::HttpsRequest request;
            Update::CheckResult result = Update::CheckForUpdate(GetVersionString(), request, 15000);
            if (!result.checked)
            {
                printf("  not checked: %s\n", result.error.c_str());
            }
            CHECK(result.checked);
            printf("  newest: %s (%s), this build %s: %s; download %s\n", result.release.tag.c_str(),
                   result.release.name.c_str(), GetVersionString(), result.newer ? "newer" : "not newer",
                   result.release.downloadUrl.empty() ? "(none for this system)" : result.release.downloadUrl.c_str());
        });
    }
} // namespace MarconiTest
