// Copyright (c) 2026 Red Marmoset Studios Ltd.
// Tests for MarconiMcp and Marconi's saved connections: finding one by name or address, the address its saved
// protocol and port give, and connecting with its saved password without the AI ever seeing it

#include "TestSupport.h"

#include <MarconiMcp/SavedComputers.h>
#include <MarconiMcp/Tools.h>

#include <MarconiCore/Net/Tls.h>
#include <MarconiCore/Rfb/Auth.h>
#include <MarconiCore/Rfb/Handshake.h>
#include <MarconiCore/Rfb/Messages.h>

#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace MarconiTest
{
    using namespace Marconi;
    using nlohmann::json;

    namespace
    {
        // A list of saved connections with passwords, and a record of which passwords were asked for.
        class FakeSaved : public Mcp::SavedComputers
        {
        public:
            std::vector<Mcp::SavedComputer> computers;
            std::vector<std::pair<std::string, std::string>> passwords; // id, password
            std::vector<std::string> asked;

            std::vector<Mcp::SavedComputer> List() override { return computers; }

            bool Password(const std::string& id, std::string& out) override
            {
                asked.push_back(id);
                for (const auto& entry : passwords)
                {
                    if (entry.first == id)
                    {
                        out = entry.second;
                        return true;
                    }
                }
                return false;
            }
        };

        Mcp::SavedComputer Computer(const std::string& id, const std::string& name, const std::string& address,
                                    const std::string& detected = "")
        {
            Mcp::SavedComputer computer;
            computer.id = id;
            computer.name = name;
            computer.address = address;
            computer.detected = detected;
            computer.quality = "balanced";
            return computer;
        }

        const uint8_t kChallenge[16] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16};

        // Runs the server side of a VNC password login and says whether the client's answer fits `password`.
        bool ServeVncPasswordLogin(FakeServer& server, const std::string& password)
        {
            server.SendString("RFB 003.008\n");
            server.In().ReadString(12);
            Rfb::OutBuffer types;
            types.U8(1).U8(Rfb::SecurityType::VncAuth);
            server.Send(types);
            if (server.In().U8() != Rfb::SecurityType::VncAuth)
            {
                return false;
            }
            server.Send(Rfb::Bytes(kChallenge, kChallenge + 16));
            const Rfb::Bytes response = server.In().ReadBytes(16);
            const std::vector<uint8_t> challenge(kChallenge, kChallenge + 16);
            const bool right = response == Rfb::ComputeVncAuthResponse(password, challenge);
            Rfb::OutBuffer result;
            result.U32(right ? 0 : 1);
            if (!right)
            {
                const std::string reason = "Authentication failed";
                result.U32(static_cast<uint32_t>(reason.size()));
                result.Append(reason.data(), reason.size());
            }
            server.Send(result);
            if (!right)
            {
                return false;
            }
            server.In().U8(); // ClientInit
            Rfb::OutBuffer init;
            init.U16(8).U16(6);
            init.U8(32).U8(24).U8(0).U8(1).U16(255).U16(255).U16(255).U8(16).U8(8).U8(0).Padding(3);
            const std::string name = "saved desktop";
            init.U32(static_cast<uint32_t>(name.size()));
            init.Append(name.data(), name.size());
            server.Send(init);
            server.ReadClientSetup();
            return true;
        }

        std::string AllText(const Mcp::ToolResult& result)
        {
            std::string text;
            for (const json& item : result.content)
            {
                if (item.value("type", "") == "text")
                {
                    text += item["text"].get<std::string>() + "\n";
                }
            }
            return text;
        }
    } // namespace

    void RunMcpSavedTests()
    {
        RunTest("MCP saved connections: found by name, then by address, ignoring case", []() {
            std::vector<Mcp::SavedComputer> computers = {Computer("1", "Drips", "drips.local"),
                                                         Computer("2", "", "10.0.0.5"),
                                                         Computer("3", "Twin", "a"), Computer("4", "twin", "b")};
            std::string error;
            const Mcp::SavedComputer* found = Mcp::FindSavedComputer(computers, "drips", error);
            CHECK(found && found->id == "1");
            found = Mcp::FindSavedComputer(computers, "DRIPS.LOCAL", error);
            CHECK(found && found->id == "1");
            found = Mcp::FindSavedComputer(computers, "10.0.0.5", error);
            CHECK(found && found->id == "2" && found->DisplayName() == "10.0.0.5");
            CHECK(!Mcp::FindSavedComputer(computers, "twin", error) && error.find("More than one") == 0);
            CHECK(!Mcp::FindSavedComputer(computers, "nowhere", error) && error.find("list_connections") !=
                                                                                  std::string::npos);
        });

        RunTest("MCP saved connections: the protocol and port Marconi found are used", []() {
            std::string address;
            std::string error;
            CHECK(Mcp::AddressFor(Computer("1", "a", "studio"), address, error) && address == "studio");
            CHECK(Mcp::AddressFor(Computer("1", "a", "studio", "marconi 1900"), address, error));
            CHECK(address == "marconi://studio:1900");
            Address parsed;
            CHECK(Mcp::AddressFor(Computer("1", "a", "studio", "vnc 5901"), address, error));
            CHECK(ParseAddress(address, parsed, error) && parsed.port == 5901 && parsed.protocol == Protocol::Vnc);
            // An address that names its protocol keeps it.
            CHECK(Mcp::AddressFor(Computer("1", "a", "vnc://studio::5902", "marconi 1895"), address, error));
            CHECK(address == "vnc://studio::5902");
            CHECK(!Mcp::AddressFor(Computer("1", "Office", "office", "rdp 3389"), address, error));
            CHECK(error.find("Remote Desktop") != std::string::npos);
        });

        RunTest("MCP saved connections: listed without passwords", []() {
            FakeSaved saved;
            saved.computers = {Computer("1", "Drips", "drips.local")};
            saved.computers[0].hasPassword = true;
            saved.computers[0].group = "Studio";
            saved.passwords = {{"1", "hunter2"}};
            Mcp::ToolOptions options;
            options.saved = &saved;
            Mcp::MarconiTools tools(options);
            Mcp::ToolResult result;
            CHECK(tools.CallTool("list_connections", json::object(), result));
            const std::string text = AllText(result);
            CHECK(text.find("Drips") != std::string::npos && text.find("Studio") != std::string::npos);
            CHECK(text.find("\"password_saved\": true") != std::string::npos);
            CHECK(text.find("hunter2") == std::string::npos);
            CHECK(saved.asked.empty()); // listing never fetches a password
        });

        RunTest("MCP saved connections: connect by name uses the saved password, never shown", []() {
            FakeServer server;
            FakeSaved saved;
            saved.computers = {Computer("1", "Drips", "127.0.0.1::" + std::to_string(server.GetPort()))};
            saved.computers[0].hasPassword = true;
            saved.passwords = {{"1", "hunter2"}};
            Mcp::ToolOptions options;
            options.saved = &saved;
            Mcp::MarconiTools tools(options);

            Mcp::ToolResult result;
            std::thread connecting([&]() { tools.CallTool("connect", {{"connection", "drips"}}, result); });
            const bool accepted = server.AcceptClient();
            bool loggedIn = false;
            if (accepted)
            {
                loggedIn = ServeVncPasswordLogin(server, "hunter2");
            }
            connecting.join();
            CHECK(accepted && loggedIn);
            CHECK(!result.isError);
            const std::string text = AllText(result);
            CHECK(text.find("Connected to Drips") == 0);
            CHECK(text.find("hunter2") == std::string::npos);
            CHECK(saved.asked == std::vector<std::string>({"1"}));
        });

        RunTest("MCP saved connections: an address that's saved gets its password too", []() {
            FakeServer server;
            FakeSaved saved;
            const std::string address = "127.0.0.1::" + std::to_string(server.GetPort());
            saved.computers = {Computer("7", "Drips", address)};
            saved.computers[0].hasPassword = true;
            saved.passwords = {{"7", "hunter2"}};
            Mcp::ToolOptions options;
            options.saved = &saved;
            Mcp::MarconiTools tools(options);
            Mcp::ToolResult result;
            std::thread connecting([&]() { tools.CallTool("connect", {{"address", address}}, result); });
            const bool accepted = server.AcceptClient();
            const bool loggedIn = accepted && ServeVncPasswordLogin(server, "hunter2");
            connecting.join();
            CHECK(loggedIn && !result.isError);
        });

        RunTest("MCP: a server offering only encryption this computer can't make is reached without it", []() {
            if (!Net::IsTlsAvailable() || Net::IsAnonymousTlsAvailable())
            {
                return; // the first try only fails this way where encryption exists but not without a certificate
            }
            FakeServer server;
            FakeSaved saved;
            saved.computers = {Computer("3", "Snorkels", "127.0.0.1::" + std::to_string(server.GetPort()))};
            saved.computers[0].hasPassword = true;
            saved.passwords = {{"3", "hunter2"}};
            Mcp::ToolOptions options;
            options.saved = &saved;
            Mcp::MarconiTools tools(options);

            Mcp::ToolResult result;
            std::thread connecting([&]() { tools.CallTool("connect", {{"connection", "snorkels"}}, result); });
            bool askedForEncryption = false;
            if (server.AcceptClient())
            {
                server.SendString("RFB 003.008\n");
                server.In().ReadString(12);
                Rfb::OutBuffer types;
                types.U8(2).U8(Rfb::SecurityType::VeNCrypt).U8(Rfb::SecurityType::VncAuth);
                server.Send(types);
                askedForEncryption = server.In().U8() == Rfb::SecurityType::VeNCrypt;
                if (askedForEncryption)
                {
                    Rfb::OutBuffer version;
                    version.U8(0).U8(2);
                    server.Send(version);
                    server.In().ReadBytes(2); // the client's version
                    Rfb::OutBuffer subtypes;
                    subtypes.U8(0).U8(1).U32(Rfb::VeNCrypt::TlsVnc); // only without a certificate
                    server.Send(subtypes);
                }
                server.Close();
            }
            const bool loggedIn = server.AcceptClient() && ServeVncPasswordLogin(server, "hunter2");
            connecting.join();
            CHECK(askedForEncryption);
            CHECK(loggedIn);
            CHECK(!result.isError);
            CHECK(AllText(result).find("Not encrypted") != std::string::npos);
        });

        RunTest("MCP saved connections: unknown names and no saved list say so", []() {
            FakeSaved saved;
            Mcp::ToolOptions options;
            options.saved = &saved;
            Mcp::MarconiTools tools(options);
            Mcp::ToolResult result;
            CHECK(tools.CallTool("connect", {{"connection", "drips"}}, result));
            CHECK(result.isError && AllText(result).find("No saved connection") == 0);
            CHECK(tools.CallTool("connect", json::object(), result) && result.isError);

            Mcp::MarconiTools without(Mcp::ToolOptions{});
            CHECK(without.CallTool("connect", {{"connection", "drips"}}, result) && result.isError);
            CHECK(without.CallTool("list_connections", json::object(), result) && result.isError);
        });

        RunTest("MCP on this network: listed unless saved, searched at most every 30 seconds", []() {
            FakeSaved saved;
            saved.computers = {Computer("1", "Studio", "studio.local")};
            int searches = 0;
            Mcp::ToolOptions options;
            options.saved = &saved;
            options.discover = [&searches]() {
                ++searches;
                Net::DiscoveredService phone;
                phone.instance = "Sam's Phone";
                phone.hostName = "Sams-Phone.local.";
                phone.ipv4 = "192.168.1.30";
                phone.port = 5900;
                phone.protocol = Protocol::Vnc;
                Net::DiscoveredService studio = phone;
                studio.instance = "Studio";
                studio.hostName = "studio.local.";
                studio.ipv4 = "192.168.1.31";
                Net::DiscoveredService office = phone;
                office.instance = "Office";
                office.hostName = "office.local.";
                office.ipv4 = "";
                office.port = 3389;
                office.protocol = Protocol::Rdp;
                return std::vector<Net::DiscoveredService>{phone, studio, office};
            };
            Mcp::MarconiTools tools(options);
            Mcp::ToolResult result;
            CHECK(tools.CallTool("list_connections", json::object(), result) && !result.isError);
            const json listed = json::parse(AllText(result));
            CHECK(listed["saved"].size() == 1 && listed["saved"][0]["name"] == "Studio");
            const json& network = listed["on_this_network"];
            CHECK(network.size() == 2); // Studio is saved already
            CHECK(network[0]["name"] == "Sam's Phone" && network[0]["address"] == "192.168.1.30");
            CHECK(network[0]["kind"] == "VNC" && !network[0].contains("usable"));
            CHECK(network[1]["address"] == "office.local" && network[1]["usable"] == false);
            CHECK(tools.CallTool("list_connections", json::object(), result));
            CHECK(searches == 1);

            // Remote Desktop isn't something the AI tools can use yet.
            CHECK(tools.CallTool("connect", {{"connection", "office"}}, result) && result.isError);
            CHECK(AllText(result).find("Remote Desktop") != std::string::npos);
        });

        RunTest("MCP on this network: connect by the name a computer announces", []() {
            FakeServer server;
            Mcp::ToolOptions options; // no saved connections, as when wx can't start
            options.discover = [&server]() {
                Net::DiscoveredService phone;
                phone.instance = "Sam's Phone";
                phone.hostName = "Sams-Phone.local.";
                phone.ipv4 = "127.0.0.1";
                phone.port = server.GetPort();
                phone.protocol = Protocol::Vnc;
                return std::vector<Net::DiscoveredService>{phone};
            };
            Mcp::MarconiTools tools(options);
            Mcp::ToolResult result;
            std::thread connecting([&]() {
                tools.CallTool("connect", {{"connection", "sam's phone"}, {"password", "hunter2"}}, result);
            });
            const bool accepted = server.AcceptClient();
            const bool loggedIn = accepted && ServeVncPasswordLogin(server, "hunter2");
            connecting.join();
            CHECK(loggedIn && !result.isError);
            CHECK(AllText(result).find("Connected to Sam's Phone") == 0);
        });
    }
} // namespace MarconiTest
