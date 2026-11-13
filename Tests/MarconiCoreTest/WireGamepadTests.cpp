// Copyright (c) 2026 Red Marmoset Studios Ltd.
// Tests for the game controller messages (round trips, size, sequence numbers, a fuzz pass) and for the forwarder
// that sends the viewer's controllers, driven with a fake controller source and a fake connection

#include "TestSupport.h"

#include <MarconiCore/Gamepad/GamepadForwarder.h>
#include <MarconiCore/Wire/WireGamepadMessages.h>
#include <MarconiCore/Wire/WireMessages.h>

#include <chrono>
#include <mutex>
#include <vector>

namespace MarconiTest
{
    using namespace Marconi;

    namespace
    {
        template <class Message>
        Message RoundTrip(const Message& message, Wire::Type expected)
        {
            Wire::Bytes framed = message.Encode();
            Wire::Reader outer(framed);
            uint64_t length = outer.Varint();
            CHECK(length == outer.Remaining());
            Wire::Reader body(framed.data() + (framed.size() - outer.Remaining()), static_cast<size_t>(length));
            CHECK(Wire::ReadType(body) == expected);
            Message back = Message::Decode(body);
            CHECK(body.AtEnd());
            return back;
        }

        // A controller source the test moves by hand.
        class FakeSource : public Gamepad::GamepadSource
        {
        public:
            int GetSlotCount() const override { return Wire::kMaxGamepads; }
            bool Read(int slot, Gamepad::PadInfo& info, Wire::GamepadState& state) override
            {
                std::lock_guard<std::mutex> lock(mutex);
                if (!present[slot])
                {
                    return false;
                }
                info.type = Wire::GamepadType::Xbox;
                info.name = "Fake pad " + std::to_string(slot);
                state = states[slot];
                return true;
            }
            void SetRumble(int slot, uint16_t strong, uint16_t weak) override
            {
                std::lock_guard<std::mutex> lock(mutex);
                rumble[slot] = {strong, weak};
                ++rumbleCalls;
            }

            std::mutex mutex;
            bool present[Wire::kMaxGamepads] = {};
            Wire::GamepadState states[Wire::kMaxGamepads];
            std::pair<uint16_t, uint16_t> rumble[Wire::kMaxGamepads] = {};
            int rumbleCalls = 0;
        };

        // Records what the forwarder sends.
        class FakeLink : public Gamepad::GamepadLink
        {
        public:
            bool SupportsGamepads() const override { return supported; }
            bool SendGamepadConnect(const Wire::GamepadConnect& message) override
            {
                log.push_back('C');
                connects.push_back(message);
                return true;
            }
            bool SendGamepadDisconnect(const Wire::GamepadDisconnect& message) override
            {
                log.push_back('D');
                disconnects.push_back(message);
                return true;
            }
            bool SendGamepadState(const Wire::GamepadState& message) override
            {
                log.push_back('S');
                states.push_back(message);
                return true;
            }
            void SetGamepadListener(Gamepad::GamepadListener* l) override { listener = l; }

            bool supported = true;
            std::string log; // C connect, S state, D disconnect, in the order sent
            std::vector<Wire::GamepadConnect> connects;
            std::vector<Wire::GamepadDisconnect> disconnects;
            std::vector<Wire::GamepadState> states;
            Gamepad::GamepadListener* listener = nullptr;
        };

        using Clock = std::chrono::steady_clock;

        class Random
        {
        public:
            explicit Random(uint32_t seed) : m_state(seed) {}
            uint32_t Next()
            {
                m_state ^= m_state << 13;
                m_state ^= m_state >> 17;
                m_state ^= m_state << 5;
                return m_state;
            }

        private:
            uint32_t m_state;
        };
    } // namespace

    void RunWireGamepadTests()
    {
        RunTest("controller messages round-trip", []() {
            Wire::GamepadConnect connect;
            connect.pad = 3;
            connect.type = Wire::GamepadType::PlayStation;
            connect.name = "DualSense Wireless Controller";
            Wire::GamepadConnect connectBack = RoundTrip(connect, Wire::Type::GamepadConnect);
            CHECK(connectBack.pad == 3 && connectBack.type == Wire::GamepadType::PlayStation);
            CHECK(connectBack.name == connect.name);

            Wire::GamepadDisconnect disconnect;
            disconnect.pad = 2;
            CHECK(RoundTrip(disconnect, Wire::Type::GamepadDisconnect).pad == 2);

            Wire::GamepadState state;
            state.pad = 1;
            state.sequence = 54321;
            state.buttons = Wire::GamepadButton::A | Wire::GamepadButton::DpadLeft | Wire::GamepadButton::Guide;
            state.leftTrigger = 200;
            state.rightTrigger = 7;
            state.leftX = -32768;
            state.leftY = 32767;
            state.rightX = -1;
            state.rightY = 1234;
            state.battery = 80;
            Wire::GamepadState back = RoundTrip(state, Wire::Type::GamepadState);
            CHECK(back.pad == 1 && back.sequence == 54321);
            CHECK(back.buttons == state.buttons);
            CHECK(back.leftTrigger == 200 && back.rightTrigger == 7);
            CHECK(back.leftX == -32768 && back.leftY == 32767 && back.rightX == -1 && back.rightY == 1234);
            CHECK(back.battery == 80);

            Wire::GamepadRumble rumble;
            rumble.pad = 0;
            rumble.sequence = 9;
            rumble.strong = 65535;
            rumble.weak = 300;
            Wire::GamepadRumble rumbleBack = RoundTrip(rumble, Wire::Type::GamepadRumble);
            CHECK(rumbleBack.sequence == 9 && rumbleBack.strong == 65535 && rumbleBack.weak == 300);

            Wire::GamepadResult result;
            result.pad = 1;
            result.status = Wire::GamepadResult::Status::NoDriver;
            result.message = "Controllers need the ViGEmBus driver, which isn't installed.";
            Wire::GamepadResult resultBack = RoundTrip(result, Wire::Type::GamepadResult);
            CHECK(resultBack.status == Wire::GamepadResult::Status::NoDriver && resultBack.message == result.message);
        });

        RunTest("a controller state is small and uses the free message numbers", []() {
            Wire::GamepadState state;
            CHECK(state.Encode().size() <= 20); // one length byte, one type byte, and 16 bytes of state
            CHECK(static_cast<int>(Wire::Type::GamepadConnect) >= 80 && static_cast<int>(Wire::Type::GamepadResult) <= 89);
            CHECK(Wire::Capability::Gamepads != 0);
        });

        RunTest("controller messages refuse a slot out of range and cut stray bits", []() {
            Wire::GamepadState state;
            state.pad = 4;
            Wire::Bytes framed = state.Encode();
            Wire::Reader outer(framed);
            uint64_t length = outer.Varint();
            Wire::Reader body(framed.data() + (framed.size() - outer.Remaining()), static_cast<size_t>(length));
            Wire::ReadType(body);
            CHECK(ThrowsWireError([&]() { Wire::GamepadState::Decode(body); }));

            Wire::GamepadState stray;
            stray.buttons = 0xFFFF;
            stray.battery = 150; // not a percentage and not "unknown"
            Wire::GamepadState back = RoundTrip(stray, Wire::Type::GamepadState);
            CHECK(back.buttons == Wire::GamepadButton::All);
            CHECK(back.battery == Wire::kBatteryUnknown);

            Wire::GamepadConnect longName;
            longName.name = std::string(Wire::kMaxGamepadNameBytes + 1, 'x');
            Wire::Bytes longFramed = longName.Encode();
            Wire::Reader longOuter(longFramed);
            uint64_t longLength = longOuter.Varint();
            Wire::Reader longBody(longFramed.data() + (longFramed.size() - longOuter.Remaining()),
                                  static_cast<size_t>(longLength));
            Wire::ReadType(longBody);
            CHECK(ThrowsWireError([&]() { Wire::GamepadConnect::Decode(longBody); }));
        });

        RunTest("sequence numbers compare across the wrap", []() {
            CHECK(Wire::IsNewerSequence(2, 1));
            CHECK(!Wire::IsNewerSequence(1, 2));
            CHECK(!Wire::IsNewerSequence(5, 5));
            CHECK(Wire::IsNewerSequence(0, 65535)); // wrapped
            CHECK(!Wire::IsNewerSequence(65535, 0));
            CHECK(Wire::IsNewerSequence(100, 65000)); // 636 ahead, across the wrap
            CHECK(!Wire::IsNewerSequence(40000, 1)); // more than half the range ahead counts as behind
        });

        RunTest("random bytes in controller messages are decoded or refused, never crash", []() {
            Random random(0xBADC0DEu);
            for (int round = 0; round < 4000; ++round)
            {
                Wire::Bytes body(random.Next() % 40);
                for (uint8_t& byte : body)
                {
                    byte = static_cast<uint8_t>(random.Next() >> 24);
                }
                for (int kind = 0; kind < 5; ++kind)
                {
                    Wire::Reader reader(body);
                    try
                    {
                        switch (kind)
                        {
                        case 0: Wire::GamepadConnect::Decode(reader); break;
                        case 1: Wire::GamepadDisconnect::Decode(reader); break;
                        case 2: Wire::GamepadState::Decode(reader); break;
                        case 3: Wire::GamepadRumble::Decode(reader); break;
                        default: Wire::GamepadResult::Decode(reader); break;
                        }
                    }
                    catch (const Wire::WireError&)
                    {
                    }
                }
            }
            CHECK(true);
        });

        RunTest("forwarder announces a controller, sends changes and heartbeats, and no more than that", []() {
            FakeSource source;
            FakeLink link;
            Gamepad::GamepadForwarder forwarder(source, link);
            CHECK(link.listener == &forwarder);
            auto t = Clock::now();

            forwarder.Tick(t, true);
            CHECK(link.log.empty()); // nothing plugged in

            source.present[1] = true;
            forwarder.Tick(t, true);
            CHECK(link.log == "CS");
            CHECK(link.connects[0].pad == 1 && link.connects[0].name == "Fake pad 1");
            CHECK(link.states[0].pad == 1 && link.states[0].sequence == 1);
            CHECK(forwarder.GetConnectedCount() == 1);

            // Nothing changed: no message until the heartbeat is due.
            forwarder.Tick(t + std::chrono::milliseconds(100), true);
            CHECK(link.log == "CS");
            forwarder.Tick(t + std::chrono::milliseconds(260), true);
            CHECK(link.log == "CSS");
            CHECK(link.states[1].sequence == 2);

            // A change goes out, but not within the minimum interval of the last send.
            source.states[1].buttons = Wire::GamepadButton::A;
            forwarder.Tick(t + std::chrono::milliseconds(262), true);
            CHECK(link.log == "CSS");
            forwarder.Tick(t + std::chrono::milliseconds(275), true);
            CHECK(link.log == "CSSS");
            CHECK(link.states[2].buttons == Wire::GamepadButton::A && link.states[2].sequence == 3);

            // The press was released between two ticks that were too close: it is still sent, later, not lost.
            source.states[1].buttons = 0;
            forwarder.Tick(t + std::chrono::milliseconds(277), true);
            CHECK(link.log == "CSSS");
            forwarder.Tick(t + std::chrono::milliseconds(290), true);
            CHECK(link.log == "CSSSS");
            CHECK(link.states[3].buttons == 0);
        });

        RunTest("forwarder lets everything go when switched off, unplugged or the window loses focus", []() {
            FakeSource source;
            FakeLink link;
            Gamepad::GamepadForwarder forwarder(source, link);
            auto t = Clock::now();
            source.present[0] = true;
            source.states[0].buttons = Wire::GamepadButton::B | Wire::GamepadButton::LeftShoulder;
            source.states[0].leftTrigger = 255;
            forwarder.Tick(t, true);
            CHECK(link.log == "CS");

            // Not active any more: a neutral state, then the disconnect.
            forwarder.Tick(t + std::chrono::milliseconds(10), false);
            CHECK(link.log == "CSSD");
            CHECK(link.states.back().IsNeutral());
            CHECK(link.states.back().sequence == 2);
            CHECK(forwarder.GetConnectedCount() == 0);
            forwarder.Tick(t + std::chrono::milliseconds(20), false);
            CHECK(link.log == "CSSD"); // and nothing more while off

            // Back in front: announced afresh.
            forwarder.Tick(t + std::chrono::milliseconds(30), true);
            CHECK(link.log == "CSSDCS");
            CHECK(link.states.back().sequence == 1); // a new connection counts from the start

            // Unplugged.
            source.present[0] = false;
            forwarder.Tick(t + std::chrono::milliseconds(40), true);
            CHECK(link.log == "CSSDCSSD");

            // Not forwarding a controller when the server doesn't take them.
            source.present[0] = true;
            link.supported = false;
            forwarder.Tick(t + std::chrono::milliseconds(50), true);
            CHECK(link.log == "CSSDCSSD");
        });

        RunTest("forwarder releases controllers when it is destroyed", []() {
            FakeSource source;
            FakeLink link;
            {
                Gamepad::GamepadForwarder forwarder(source, link);
                source.present[2] = true;
                source.states[2].buttons = Wire::GamepadButton::Start;
                forwarder.Tick(Clock::now(), true);
                CHECK(link.log == "CS");
            }
            CHECK(link.log == "CSSD");
            CHECK(link.states.back().IsNeutral());
            CHECK(link.listener == nullptr);
        });

        RunTest("forwarder applies rumble to the right controller and ignores a late one", []() {
            FakeSource source;
            FakeLink link;
            Gamepad::GamepadForwarder forwarder(source, link);
            source.present[1] = true;
            forwarder.Tick(Clock::now(), true);

            Wire::GamepadRumble rumble;
            rumble.pad = 1;
            rumble.sequence = 10;
            rumble.strong = 40000;
            rumble.weak = 100;
            link.listener->OnGamepadRumble(rumble);
            CHECK(source.rumble[1].first == 40000 && source.rumble[1].second == 100);
            CHECK(source.rumble[0].first == 0);

            rumble.sequence = 9; // older: ignored
            rumble.strong = 1;
            link.listener->OnGamepadRumble(rumble);
            CHECK(source.rumble[1].first == 40000);

            rumble.sequence = 11;
            rumble.strong = 0;
            rumble.weak = 0;
            link.listener->OnGamepadRumble(rumble);
            CHECK(source.rumble[1].first == 0 && source.rumble[1].second == 0);

            rumble.pad = 3; // a controller that isn't connected
            rumble.sequence = 12;
            rumble.strong = 5;
            int calls = source.rumbleCalls;
            link.listener->OnGamepadRumble(rumble);
            CHECK(source.rumbleCalls == calls);
        });

        RunTest("forwarder passes on a refusal once and doesn't keep asking", []() {
            FakeSource source;
            FakeLink link;
            Gamepad::GamepadForwarder forwarder(source, link);
            auto t = Clock::now();
            source.present[0] = true;
            forwarder.Tick(t, true);
            CHECK(link.log == "CS");

            Wire::GamepadResult refused;
            refused.pad = 0;
            refused.status = Wire::GamepadResult::Status::NoDriver;
            refused.message = "Controllers need the ViGEmBus driver, which isn't installed.";
            link.listener->OnGamepadResult(refused);
            CHECK(forwarder.TakeNotice() == refused.message);
            CHECK(forwarder.TakeNotice().empty());
            CHECK(forwarder.GetConnectedCount() == 0);

            forwarder.Tick(t + std::chrono::seconds(1), true);
            forwarder.Tick(t + std::chrono::seconds(2), true);
            CHECK(link.log == "CS"); // no more connects, no states

            // Unplug and plug in again: asked again.
            source.present[0] = false;
            forwarder.Tick(t + std::chrono::seconds(3), true);
            source.present[0] = true;
            forwarder.Tick(t + std::chrono::seconds(4), true);
            CHECK(link.log == "CSCS");
        });
    }
} // namespace MarconiTest
