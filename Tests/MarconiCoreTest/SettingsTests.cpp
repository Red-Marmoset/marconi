// Copyright (c) 2026 Red Marmoset Studios Ltd.
// Tests for picture quality settings, Wake-on-LAN packets, and changing the quality of a live connection

#include "TestSupport.h"

#include <MarconiCore/Net/WakeOnLan.h>
#include <MarconiCore/Quality.h>
#include <MarconiCore/Rfb/Messages.h>

#include <algorithm>

namespace MarconiTest
{
    using namespace Marconi;

    void RunSettingsTests()
    {
        RunTest("Picture quality names and levels round-trip", []() {
            CHECK(QualityFromName("best") == PictureQuality::Best);
            CHECK(QualityFromName("low") == PictureQuality::LowBandwidth);
            CHECK(QualityFromName("balanced") == PictureQuality::Balanced);
            CHECK(QualityFromName("nonsense") == PictureQuality::Balanced);
            CHECK(std::string(QualityName(PictureQuality::Best)) == "best");
            CHECK(SettingsFor(PictureQuality::Best).qualityLevel == -1);
            CHECK(SettingsFor(PictureQuality::Balanced).qualityLevel == 8 && SettingsFor(PictureQuality::Balanced).compressLevel == 2);
            CHECK(SettingsFor(PictureQuality::LowBandwidth).compressLevel > SettingsFor(PictureQuality::Balanced).compressLevel);
            CHECK(SettingsFor(PictureQuality::LowBandwidth).qualityLevel < SettingsFor(PictureQuality::Balanced).qualityLevel);
        });

        RunTest("Hardware addresses parse in the usual spellings and build a magic packet", []() {
            Net::MacAddress mac;
            CHECK(Net::ParseMacAddress("00:1A:2b:3C:4d:5E", mac) && Net::FormatMacAddress(mac) == "00:1A:2B:3C:4D:5E");
            CHECK(Net::ParseMacAddress("00-1a-2b-3c-4d-5e", mac) && mac[5] == 0x5E);
            CHECK(Net::ParseMacAddress("001a.2b3c.4d5e", mac) && mac[0] == 0x00 && mac[2] == 0x2B);
            CHECK(Net::ParseMacAddress("001A2B3C4D5E", mac));
            CHECK(!Net::ParseMacAddress("", mac));
            CHECK(!Net::ParseMacAddress("00:1A:2B:3C:4D", mac));
            CHECK(!Net::ParseMacAddress("00:1A:2B:3C:4D:5E:6F", mac));
            CHECK(!Net::ParseMacAddress("studio-mac.local", mac));
            std::vector<uint8_t> packet = Net::BuildMagicPacket(mac);
            CHECK(packet.size() == 102);
            CHECK(std::count(packet.begin(), packet.begin() + 6, 0xFF) == 6);
            CHECK(packet[6] == 0x00 && packet[11] == 0x5E && packet[12] == 0x00 && packet[101] == 0x5E);
            std::string error;
            CHECK(!Net::SendWakeOnLan("not an address", error));
            CHECK(error.find("hardware") != std::string::npos);
        });

        RunTest("Changing the picture quality re-sends the encodings with the new levels", []() {
            FakeServer server;
            RecordingListener listener;
            Rfb::Client client(listener);
            Rfb::ConnectionOptions options;
            options.host = "127.0.0.1";
            options.port = server.GetPort();
            client.Connect(options);
            CHECK(server.AcceptClient());
            server.HandshakeNone("RFB 003.008\n", 8, 8, "q");
            std::vector<int32_t> initial = server.ReadClientSetup();
            CHECK(std::find(initial.begin(), initial.end(), Rfb::Encoding::CompressLevel0 + 2) != initial.end());
            CHECK(std::find(initial.begin(), initial.end(), Rfb::Encoding::QualityLevel0 + 8) != initial.end());
            CHECK(listener.WaitFor([&]() { return listener.connected; }));

            client.SetQuality(6, 4);
            Rfb::InStream& in = server.In();
            CHECK(in.U8() == Rfb::ClientMessage::SetEncodings);
            in.Skip(1);
            uint16_t count = in.U16();
            std::vector<int32_t> encodings;
            for (uint16_t i = 0; i < count; ++i)
            {
                encodings.push_back(in.S32());
            }
            CHECK(std::find(encodings.begin(), encodings.end(), Rfb::Encoding::CompressLevel0 + 6) != encodings.end());
            CHECK(std::find(encodings.begin(), encodings.end(), Rfb::Encoding::QualityLevel0 + 4) != encodings.end());
            CHECK(std::find(encodings.begin(), encodings.end(), Rfb::Encoding::CompressLevel0 + 2) == encodings.end());
            CHECK(std::find(encodings.begin(), encodings.end(), Rfb::Encoding::Tight) != encodings.end());
            CHECK(std::find(encodings.begin(), encodings.end(), Rfb::Encoding::ExtendedDesktopSize) != encodings.end());

            client.SetQuality(2, -1); // lossless: no quality level at all
            CHECK(in.U8() == Rfb::ClientMessage::SetEncodings);
            in.Skip(1);
            count = in.U16();
            encodings.clear();
            for (uint16_t i = 0; i < count; ++i)
            {
                encodings.push_back(in.S32());
            }
            bool anyQuality = false;
            for (int32_t encoding : encodings)
            {
                anyQuality = anyQuality || (encoding >= Rfb::Encoding::QualityLevel0 && encoding <= Rfb::Encoding::QualityLevel0 + 9);
            }
            CHECK(!anyQuality);
        });
    }
} // namespace MarconiTest
