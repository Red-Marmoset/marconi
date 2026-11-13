// Copyright (c) 2026 Red Marmoset Studios Ltd.
// Game controllers: the host that makes a virtual controller for each of the viewer's, and the whole path end to end
// (the real Wire client and forwarder against the real server over a loopback socket) with a fake controller on
// each side, since no real controller or driver is needed to check the logic. On Linux the button and axis
// mapping and the rumble bookkeeping of the uinput backend are checked too (the device itself can't be, see the
// header of VirtualGamepad_Linux.cpp).

#include "TestSupport.h"

#include <MarconiCore/Gamepad/GamepadForwarder.h>
#include <MarconiCore/Wire/WireClient.h>
#include <MarconiServerCore/GamepadHost.h>
#include <MarconiServerCore/Listener.h>
#include <MarconiServerCore/TestPatternScreen.h>
#include <MarconiServerCore/VirtualGamepad.h>

#if defined(__linux__)
#include <MarconiServerCore/UinputMapping.h>
#endif

#include <chrono>
#include <memory>
#include <thread>
#include <vector>

namespace MarconiServerTest
{
    using namespace Marconi;

    namespace
    {
        bool WaitUntil(const std::function<bool()>& predicate, int timeoutMs)
        {
            auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
            while (std::chrono::steady_clock::now() < deadline)
            {
                if (predicate())
                {
                    return true;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }
            return predicate();
        }

        // What the fake backend remembers about one virtual controller, kept after the controller is destroyed.
        struct PadRecord
        {
            std::mutex mutex;
            std::vector<Wire::GamepadState> states;
            bool destroyed = false;
            Server::RumbleHandler rumble;

            Wire::GamepadState Last()
            {
                std::lock_guard<std::mutex> lock(mutex);
                return states.empty() ? Wire::GamepadState() : states.back();
            }
            size_t Count()
            {
                std::lock_guard<std::mutex> lock(mutex);
                return states.size();
            }
            bool Destroyed()
            {
                std::lock_guard<std::mutex> lock(mutex);
                return destroyed;
            }
        };

        class FakePad : public Server::VirtualGamepad
        {
        public:
            explicit FakePad(std::shared_ptr<PadRecord> record) : m_record(std::move(record)) {}
            ~FakePad() override
            {
                std::lock_guard<std::mutex> lock(m_record->mutex);
                m_record->destroyed = true;
            }
            bool Update(const Wire::GamepadState& state) override
            {
                std::lock_guard<std::mutex> lock(m_record->mutex);
                m_record->states.push_back(state);
                return true;
            }

        private:
            std::shared_ptr<PadRecord> m_record;
        };

        class FakeBackend : public Server::VirtualGamepadBackend
        {
        public:
            std::string GetName() const override { return "fake"; }
            std::unique_ptr<Server::VirtualGamepad> Create(Wire::GamepadType type, Server::RumbleHandler onRumble,
                                                           Wire::GamepadResult::Status& status,
                                                           std::string& message) override
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                if (failWith != Wire::GamepadResult::Status::Ok)
                {
                    status = failWith;
                    message = failMessage;
                    return nullptr;
                }
                auto record = std::make_shared<PadRecord>();
                record->rumble = std::move(onRumble);
                records.push_back(record);
                types.push_back(type);
                return std::make_unique<FakePad>(record);
            }

            std::shared_ptr<PadRecord> Record(size_t index)
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                return index < records.size() ? records[index] : nullptr;
            }
            size_t Created()
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                return records.size();
            }
            int Alive()
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                int alive = 0;
                for (auto& record : records)
                {
                    alive += record->Destroyed() ? 0 : 1;
                }
                return alive;
            }

            Wire::GamepadResult::Status failWith = Wire::GamepadResult::Status::Ok;
            std::string failMessage;
            std::vector<std::shared_ptr<PadRecord>> records;
            std::vector<Wire::GamepadType> types;

        private:
            std::mutex m_mutex;
        };

        Wire::GamepadState StateWith(uint8_t pad, uint16_t sequence, uint16_t buttons)
        {
            Wire::GamepadState state;
            state.pad = pad;
            state.sequence = sequence;
            state.buttons = buttons;
            return state;
        }

        Wire::GamepadConnect ConnectMessage(uint8_t pad)
        {
            Wire::GamepadConnect connect;
            connect.pad = pad;
            connect.type = Wire::GamepadType::Xbox;
            connect.name = "Test pad";
            return connect;
        }

        // The viewer's controllers for the end-to-end tests.
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
                info.name = "Viewer pad " + std::to_string(slot);
                state = states[slot];
                return true;
            }
            void SetRumble(int slot, uint16_t strong, uint16_t weak) override
            {
                std::lock_guard<std::mutex> lock(mutex);
                rumble[slot] = {strong, weak};
            }
            std::pair<uint16_t, uint16_t> Rumble(int slot)
            {
                std::lock_guard<std::mutex> lock(mutex);
                return rumble[slot];
            }
            void Set(int slot, bool isPresent, uint16_t buttons = 0)
            {
                std::lock_guard<std::mutex> lock(mutex);
                present[slot] = isPresent;
                states[slot] = Wire::GamepadState();
                states[slot].buttons = buttons;
            }

            std::mutex mutex;
            bool present[Wire::kMaxGamepads] = {};
            Wire::GamepadState states[Wire::kMaxGamepads];
            std::pair<uint16_t, uint16_t> rumble[Wire::kMaxGamepads] = {};
        };

        // The server (with `backend`, or none) and a connected viewer.
        struct Rig
        {
            Server::TestPatternScreen screen{8, 8};
            RecordingInput input;
            Server::Settings settings = []() {
                Server::Settings made;
                made.allowNoPassword = true;
                return made;
            }();
            std::unique_ptr<Server::Listener> listener;
            std::thread serverThread;
            RecordingClientListener clientListener;
            std::unique_ptr<Wire::Client> client;

            explicit Rig(Server::VirtualGamepadBackend* backend)
            {
                settings.port = 0;
                listener = std::make_unique<Server::Listener>(screen, input, settings);
                listener->SetFrameIntervalMs(1);
                listener->SetGamepads(backend);
                std::string error;
                CHECK(listener->Start(error, "127.0.0.1"));
                serverThread = std::thread([this]() { listener->ServeOne(5000); });
                client = std::make_unique<Wire::Client>(clientListener);
                Wire::ClientOptions options;
                options.host = "127.0.0.1";
                options.port = listener->GetPort();
                options.clientName = "gamepad viewer";
                client->Connect(options);
                CHECK(clientListener.WaitFor([this]() { return clientListener.connected; }));
            }

            ~Rig()
            {
                client->Disconnect();
                clientListener.WaitFor([this]() { return clientListener.disconnected; });
                listener->Stop();
                serverThread.join();
            }

            // Ticks the forwarder until `predicate` holds (or time runs out).
            bool Pump(Gamepad::GamepadForwarder& forwarder, const std::function<bool()>& predicate,
                      int timeoutMs = 5000, bool active = true)
            {
                auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
                while (std::chrono::steady_clock::now() < deadline)
                {
                    forwarder.Tick(std::chrono::steady_clock::now(), active);
                    if (predicate())
                    {
                        return true;
                    }
                    std::this_thread::sleep_for(std::chrono::milliseconds(4));
                }
                return predicate();
            }
        };
    } // namespace

    void RunGamepadTests()
    {
        RunTest("the host makes a controller, applies its state, and removes it releasing everything", []() {
            FakeBackend backend;
            Server::GamepadHost host(backend, [](const Wire::GamepadRumble&) {});
            Wire::GamepadResult result = host.Connect(ConnectMessage(0));
            CHECK(result.status == Wire::GamepadResult::Status::Ok);
            CHECK(host.GetCount() == 1 && backend.Created() == 1);

            CHECK(host.Update(StateWith(0, 1, Wire::GamepadButton::A | Wire::GamepadButton::RightShoulder)));
            CHECK(backend.Record(0)->Last().buttons == (Wire::GamepadButton::A | Wire::GamepadButton::RightShoulder));
            // The same inputs again (a heartbeat) are applied but are not a change.
            CHECK(!host.Update(StateWith(0, 2, Wire::GamepadButton::A | Wire::GamepadButton::RightShoulder)));

            host.Disconnect(0);
            CHECK(host.GetCount() == 0);
            CHECK(backend.Record(0)->Destroyed());
            CHECK(backend.Record(0)->Last().IsNeutral()); // let go before it vanished
            CHECK(backend.Alive() == 0);
        });

        RunTest("the host ignores states for a controller that isn't there and ones out of order", []() {
            FakeBackend backend;
            Server::GamepadHost host(backend, [](const Wire::GamepadRumble&) {});
            CHECK(!host.Update(StateWith(1, 1, Wire::GamepadButton::B))); // nothing connected in slot 1
            CHECK(host.Connect(ConnectMessage(1)).status == Wire::GamepadResult::Status::Ok);
            CHECK(host.Update(StateWith(1, 10, Wire::GamepadButton::B)));
            CHECK(!host.Update(StateWith(1, 9, Wire::GamepadButton::X)));  // older
            CHECK(!host.Update(StateWith(1, 10, Wire::GamepadButton::X))); // the same one twice
            CHECK(backend.Record(0)->Last().buttons == Wire::GamepadButton::B);
            CHECK(host.Update(StateWith(1, 11, Wire::GamepadButton::Y)));
            CHECK(backend.Record(0)->Last().buttons == Wire::GamepadButton::Y);

            // The sequence wraps around and carries on.
            CHECK(host.Update(StateWith(1, 30000, 0)));
            CHECK(host.Update(StateWith(1, 60000, Wire::GamepadButton::A)));
            CHECK(host.Update(StateWith(1, 65535, Wire::GamepadButton::B)));
            CHECK(host.Update(StateWith(1, 0, Wire::GamepadButton::Start)));
            CHECK(backend.Record(0)->Last().buttons == Wire::GamepadButton::Start);
            CHECK(!host.Update(StateWith(1, 65535, Wire::GamepadButton::Back)));
            CHECK(backend.Record(0)->Last().buttons == Wire::GamepadButton::Start);
        });

        RunTest("the host limits how many controllers a viewer can have", []() {
            FakeBackend backend;
            Server::GamepadHost host(backend, [](const Wire::GamepadRumble&) {}, 2);
            CHECK(host.Connect(ConnectMessage(0)).status == Wire::GamepadResult::Status::Ok);
            CHECK(host.Connect(ConnectMessage(1)).status == Wire::GamepadResult::Status::Ok);
            Wire::GamepadResult third = host.Connect(ConnectMessage(2));
            CHECK(third.status == Wire::GamepadResult::Status::TooMany);
            CHECK(!third.message.empty());
            CHECK(host.GetCount() == 2 && backend.Created() == 2);

            Server::GamepadHost four(backend, [](const Wire::GamepadRumble&) {});
            for (uint8_t i = 0; i < 4; ++i)
            {
                CHECK(four.Connect(ConnectMessage(i)).status == Wire::GamepadResult::Status::Ok);
            }
            CHECK(four.GetCount() == 4);
        });

        RunTest("plugging a controller in twice gives a clean new one, and a backend that can't says why", []() {
            FakeBackend backend;
            Server::GamepadHost host(backend, [](const Wire::GamepadRumble&) {});
            host.Connect(ConnectMessage(0));
            host.Update(StateWith(0, 5, Wire::GamepadButton::A));
            host.Connect(ConnectMessage(0));
            CHECK(backend.Created() == 2 && backend.Record(0)->Destroyed() && !backend.Record(1)->Destroyed());
            CHECK(backend.Record(0)->Last().IsNeutral());
            CHECK(host.Update(StateWith(0, 1, Wire::GamepadButton::B))); // the count starts again with the new one

            backend.failWith = Wire::GamepadResult::Status::NoDriver;
            backend.failMessage = "Controllers need the ViGEmBus driver, which isn't installed.";
            Wire::GamepadResult refused = host.Connect(ConnectMessage(2));
            CHECK(refused.status == Wire::GamepadResult::Status::NoDriver);
            CHECK(refused.message == backend.failMessage);
            CHECK(refused.pad == 2);
            CHECK(host.GetCount() == 1);
        });

        RunTest("the host destroying itself releases every controller", []() {
            FakeBackend backend;
            {
                Server::GamepadHost host(backend, [](const Wire::GamepadRumble&) {});
                host.Connect(ConnectMessage(0));
                host.Connect(ConnectMessage(3));
                host.Update(StateWith(0, 1, Wire::GamepadButton::A));
                host.Update(StateWith(3, 1, Wire::GamepadButton::LeftStick));
                CHECK(backend.Alive() == 2);
            }
            CHECK(backend.Alive() == 0);
            CHECK(backend.Record(0)->Last().IsNeutral() && backend.Record(1)->Last().IsNeutral());
        });

        RunTest("rumble from the backend goes out numbered, for the right controller", []() {
            FakeBackend backend;
            std::vector<Wire::GamepadRumble> sent;
            Server::GamepadHost host(backend, [&sent](const Wire::GamepadRumble& r) { sent.push_back(r); });
            host.Connect(ConnectMessage(2));
            backend.Record(0)->rumble(30000, 500);
            backend.Record(0)->rumble(0, 0);
            CHECK(sent.size() == 2);
            CHECK(sent[0].pad == 2 && sent[0].strong == 30000 && sent[0].weak == 500);
            CHECK(Wire::IsNewerSequence(sent[1].sequence, sent[0].sequence));
            CHECK(sent[1].strong == 0 && sent[1].weak == 0);
        });

#if defined(__linux__)
        RunTest("the Linux controller maps buttons and sticks the way the Xbox driver does", []() {
            using namespace Server;
            auto find = [](const std::vector<input_event>& events, uint16_t type, uint16_t code, int32_t& value) {
                for (const input_event& e : events)
                {
                    if (e.type == type && e.code == code)
                    {
                        value = e.value;
                        return true;
                    }
                }
                return false;
            };
            Wire::GamepadState rest;
            Wire::GamepadState state;
            state.buttons = Wire::GamepadButton::A | Wire::GamepadButton::Guide | Wire::GamepadButton::DpadUp |
                            Wire::GamepadButton::DpadRight;
            state.leftY = 32767;  // pushed up
            state.rightY = -32768; // pushed down
            state.leftX = -1000;
            state.leftTrigger = 200;
            std::vector<input_event> events;
            BuildUinputEvents(rest, state, false, events);
            int32_t value = 99;
            CHECK(find(events, EV_KEY, BTN_SOUTH, value) && value == 1);
            CHECK(find(events, EV_KEY, BTN_MODE, value) && value == 1);
            CHECK(!find(events, EV_KEY, BTN_EAST, value)); // unchanged buttons aren't repeated
            CHECK(find(events, EV_ABS, ABS_HAT0X, value) && value == 1);
            CHECK(find(events, EV_ABS, ABS_HAT0Y, value) && value == -1); // up is negative on the hat
            CHECK(find(events, EV_ABS, ABS_Y, value) && value == -32768);  // up is negative, no overflow
            CHECK(find(events, EV_ABS, ABS_RY, value) && value == 32767);
            CHECK(find(events, EV_ABS, ABS_X, value) && value == -1000);
            CHECK(find(events, EV_ABS, ABS_Z, value) && value == 200);
            CHECK(events.back().type == EV_SYN && events.back().code == SYN_REPORT);

            // Letting go reports the release; nothing changed reports nothing at all.
            std::vector<input_event> release;
            BuildUinputEvents(state, rest, false, release);
            CHECK(find(release, EV_KEY, BTN_SOUTH, value) && value == 0);
            CHECK(find(release, EV_ABS, ABS_HAT0X, value) && value == 0);
            std::vector<input_event> same;
            BuildUinputEvents(rest, rest, false, same);
            CHECK(same.empty());
            std::vector<input_event> all;
            BuildUinputEvents(rest, rest, true, all); // the first state writes everything
            CHECK(find(all, EV_KEY, BTN_START, value) && value == 0);
            CHECK(find(all, EV_ABS, ABS_Y, value) && value == -1); // centred, as xpad reports it
        });

        RunTest("the Linux controller works out rumble from what a game uploads and plays", []() {
            using namespace Server;
            RumbleTracker tracker;
            tracker.Upload(1, 40000, 10000, 0);
            tracker.Upload(2, 20000, 60000, 500);
            CHECK(tracker.Current() == RumbleTracker::Result{});
            tracker.Play(1, 1, 1000);
            CHECK(tracker.Current().strong == 40000 && tracker.Current().weak == 10000);
            CHECK(tracker.NextExpiry(1000) == -1); // plays until stopped
            tracker.Play(2, 1, 1000);
            CHECK(tracker.Current().strong == 40000 && tracker.Current().weak == 60000); // the strongest of each
            CHECK(tracker.NextExpiry(1100) == 400);
            tracker.Expire(1500);
            CHECK(tracker.Current().weak == 10000);
            tracker.SetGain(0x7FFF);
            CHECK(tracker.Current().strong == 19999 || tracker.Current().strong == 20000);
            tracker.SetGain(0xFFFF);
            tracker.Play(1, 0, 2000);
            CHECK(tracker.Current() == RumbleTracker::Result{});
            tracker.Play(2, 1, 3000);
            tracker.Erase(2);
            CHECK(tracker.Current() == RumbleTracker::Result{});
        });
#endif

        RunTest("a server with no controller backend doesn't offer controllers", []() {
            Rig rig(nullptr);
            CHECK(!rig.client->SupportsGamepads());
            FakeSource source;
            Gamepad::GamepadForwarder forwarder(source, *rig.client);
            source.Set(0, true, Wire::GamepadButton::A);
            for (int i = 0; i < 20; ++i)
            {
                forwarder.Tick(std::chrono::steady_clock::now(), true);
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }
            CHECK(forwarder.GetConnectedCount() == 0); // nothing was sent
        });

        RunTest("a viewer's controller appears on the server, follows it, shakes back, and goes away", []() {
            FakeBackend backend;
            Rig rig(&backend);
            CHECK(rig.client->SupportsGamepads());
            FakeSource source;
            Gamepad::GamepadForwarder forwarder(source, *rig.client);

            // Plugged in while holding A.
            source.Set(0, true, Wire::GamepadButton::A);
            CHECK(rig.Pump(forwarder, [&]() { return backend.Created() == 1 && backend.Record(0)->Count() >= 1; }));
            CHECK(backend.Record(0)->Last().buttons == Wire::GamepadButton::A);
            CHECK(backend.types[0] == Wire::GamepadType::Xbox);

            // Changes follow: a stick and the trigger move, A is released, B is pressed.
            {
                std::lock_guard<std::mutex> lock(source.mutex);
                source.states[0].buttons = Wire::GamepadButton::B;
                source.states[0].leftX = 12345;
                source.states[0].rightTrigger = 99;
            }
            CHECK(rig.Pump(forwarder, [&]() { return backend.Record(0)->Last().buttons == Wire::GamepadButton::B; }));
            CHECK(backend.Record(0)->Last().leftX == 12345 && backend.Record(0)->Last().rightTrigger == 99);

            // A game on the server shakes the controller, and the viewer's own controller shakes.
            backend.Record(0)->rumble(50000, 1000);
            CHECK(rig.Pump(forwarder, [&]() { return source.Rumble(0).first == 50000; }));
            CHECK(source.Rumble(0).second == 1000);
            backend.Record(0)->rumble(0, 0);
            CHECK(rig.Pump(forwarder, [&]() { return source.Rumble(0).first == 0 && source.Rumble(0).second == 0; }));

            // Unplugged: the virtual one is removed with everything released.
            source.Set(0, false);
            CHECK(rig.Pump(forwarder, [&]() { return backend.Record(0)->Destroyed(); }));
            CHECK(backend.Record(0)->Last().IsNeutral());
        });

        RunTest("controllers stop and are released when the window loses focus", []() {
            FakeBackend backend;
            Rig rig(&backend);
            FakeSource source;
            Gamepad::GamepadForwarder forwarder(source, *rig.client);
            source.Set(1, true, Wire::GamepadButton::LeftShoulder);
            CHECK(rig.Pump(forwarder, [&]() { return backend.Created() == 1 && backend.Record(0)->Count() >= 1; }));
            CHECK(backend.Record(0)->Last().buttons == Wire::GamepadButton::LeftShoulder);
            // The window is no longer in front: not active.
            CHECK(rig.Pump(forwarder, [&]() { return backend.Record(0)->Destroyed(); }, 5000, false));
            CHECK(backend.Record(0)->Last().IsNeutral());
        });

        RunTest("a viewer that drops with a button held leaves nothing pressed on the server", []() {
            FakeBackend backend;
            Rig rig(&backend);
            FakeSource source;
            Gamepad::GamepadForwarder forwarder(source, *rig.client);
            source.Set(0, true, Wire::GamepadButton::A | Wire::GamepadButton::RightStick);
            source.Set(2, true, Wire::GamepadButton::Y);
            CHECK(rig.Pump(forwarder, [&]() {
                return backend.Created() == 2 && backend.Record(0)->Last().buttons != 0 &&
                       backend.Record(1)->Last().buttons != 0;
            }));
            CHECK(backend.Alive() == 2);

            // The connection goes with no goodbye from the controllers: the forwarder never gets to release them.
            rig.client->Disconnect();
            CHECK(WaitUntil([&]() { return backend.Alive() == 0; }, 5000));
            CHECK(backend.Record(0)->Last().IsNeutral());
            CHECK(backend.Record(1)->Last().IsNeutral());
        });

        RunTest("the server says why when it can't make a controller", []() {
            FakeBackend backend;
            backend.failWith = Wire::GamepadResult::Status::NoDriver;
            backend.failMessage = "Game controllers need the ViGEmBus driver, which isn't installed on this computer.";
            Rig rig(&backend);
            FakeSource source;
            Gamepad::GamepadForwarder forwarder(source, *rig.client);
            source.Set(0, true, Wire::GamepadButton::A);
            std::string notice;
            CHECK(rig.Pump(forwarder, [&]() {
                notice = forwarder.TakeNotice();
                return !notice.empty();
            }));
            CHECK(notice == backend.failMessage);
            CHECK(forwarder.GetConnectedCount() == 0);
            CHECK(backend.Created() == 0);
        });

        RunTest("viewer controllers that arrive from a hostile viewer are bounded", []() {
            // States for slots with no controller are ignored, and a slot out of range ends the connection.
            FakeBackend backend;
            Rig rig(&backend);
            Wire::GamepadState state = StateWith(0, 1, Wire::GamepadButton::A);
            CHECK(rig.client->SendGamepadState(state)); // no controller in slot 0: ignored
            CHECK(rig.client->SendGamepadConnect(ConnectMessage(0)));
            CHECK(WaitUntil([&]() { return backend.Created() == 1; }, 5000));
            CHECK(backend.Record(0)->Count() == 0); // the ignored state never reached it
            Wire::GamepadState wild = StateWith(3, 1, 0xFFFF);
            wild.pad = 3;
            CHECK(rig.client->SendGamepadState(wild)); // slot 3 has none: ignored
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            CHECK(backend.Record(0)->Count() == 0 && backend.Created() == 1);
        });
    }
} // namespace MarconiServerTest
