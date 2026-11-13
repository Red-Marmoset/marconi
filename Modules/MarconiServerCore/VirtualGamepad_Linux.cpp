// Copyright (c) 2026 Red Marmoset Studios Ltd.
// Virtual game controllers on Linux through uinput: the kernel makes an Xbox 360 style controller appear, which
// games see like a real one, and the shaking a game asks for comes back through the same device.
//
// Not run against the kernel yet: the machine this was written on has no /dev/uinput (the kernel was built without
// it), so the button and axis mapping and the rumble bookkeeping are tested (UinputMapping.h) but creating the
// device and the force feedback exchange were never run. Check it on a real Linux computer.

#include "UinputMapping.h"
#include "VirtualGamepad.h"

#include <Log/Log.h>

#include <linux/uinput.h>

#include <cerrno>
#include <chrono>
#include <cstring>
#include <fcntl.h>
#include <mutex>
#include <poll.h>
#include <sys/ioctl.h>
#include <thread>
#include <unistd.h>

namespace Marconi
{
    namespace Server
    {
        namespace
        {
            int64_t NowMs()
            {
                return std::chrono::duration_cast<std::chrono::milliseconds>(
                           std::chrono::steady_clock::now().time_since_epoch())
                    .count();
            }

            class UinputPad : public VirtualGamepad
            {
            public:
                explicit UinputPad(RumbleHandler onRumble) : m_onRumble(std::move(onRumble)) {}

                ~UinputPad() override
                {
                    if (m_thread.joinable())
                    {
                        const char stop = 1;
                        if (write(m_stopPipe[1], &stop, 1) < 0)
                        {
                            // The thread also notices the device closing; nothing more to do.
                        }
                        m_thread.join();
                    }
                    if (m_fd >= 0)
                    {
                        ioctl(m_fd, UI_DEV_DESTROY);
                        close(m_fd);
                    }
                    for (int end : m_stopPipe)
                    {
                        if (end >= 0)
                        {
                            close(end);
                        }
                    }
                }

                // Makes the device. False with `error` set (and `errorNumber`) if the kernel refused.
                bool Open(int& errorNumber)
                {
                    m_fd = open("/dev/uinput", O_RDWR | O_NONBLOCK | O_CLOEXEC);
                    if (m_fd < 0)
                    {
                        errorNumber = errno;
                        return false;
                    }
                    auto fail = [&]() {
                        errorNumber = errno;
                        return false;
                    };
                    if (ioctl(m_fd, UI_SET_EVBIT, EV_KEY) < 0 || ioctl(m_fd, UI_SET_EVBIT, EV_ABS) < 0 ||
                        ioctl(m_fd, UI_SET_EVBIT, EV_SYN) < 0)
                    {
                        return fail();
                    }
                    for (const UinputButton& button : GetUinputButtons())
                    {
                        if (ioctl(m_fd, UI_SET_KEYBIT, button.code) < 0)
                        {
                            return fail();
                        }
                    }
                    struct Axis
                    {
                        uint16_t code;
                        int32_t minimum;
                        int32_t maximum;
                        int32_t fuzz;
                        int32_t flat;
                    };
                    const Axis axes[] = {
                        {ABS_X, -32768, 32767, 16, 128},  {ABS_Y, -32768, 32767, 16, 128},
                        {ABS_RX, -32768, 32767, 16, 128}, {ABS_RY, -32768, 32767, 16, 128},
                        {ABS_Z, 0, 255, 0, 0},            {ABS_RZ, 0, 255, 0, 0},
                        {ABS_HAT0X, -1, 1, 0, 0},         {ABS_HAT0Y, -1, 1, 0, 0},
                    };
                    for (const Axis& axis : axes)
                    {
                        if (ioctl(m_fd, UI_SET_ABSBIT, axis.code) < 0)
                        {
                            return fail();
                        }
                        uinput_abs_setup setup;
                        std::memset(&setup, 0, sizeof(setup));
                        setup.code = axis.code;
                        setup.absinfo.minimum = axis.minimum;
                        setup.absinfo.maximum = axis.maximum;
                        setup.absinfo.fuzz = axis.fuzz;
                        setup.absinfo.flat = axis.flat;
                        if (ioctl(m_fd, UI_ABS_SETUP, &setup) < 0)
                        {
                            return fail();
                        }
                    }
                    // Shaking: a game uploads an effect to the device and then plays it. Rumble is the one kind.
                    if (ioctl(m_fd, UI_SET_EVBIT, EV_FF) < 0 || ioctl(m_fd, UI_SET_FFBIT, FF_RUMBLE) < 0)
                    {
                        return fail();
                    }

                    uinput_setup setup;
                    std::memset(&setup, 0, sizeof(setup));
                    setup.id.bustype = BUS_USB;
                    setup.id.vendor = kUinputVendor;
                    setup.id.product = kUinputProduct;
                    setup.id.version = kUinputVersion;
                    std::strncpy(setup.name, kUinputName, UINPUT_MAX_NAME_SIZE - 1);
                    setup.ff_effects_max = 16;
                    if (ioctl(m_fd, UI_DEV_SETUP, &setup) < 0 || ioctl(m_fd, UI_DEV_CREATE) < 0)
                    {
                        return fail();
                    }

                    if (pipe2(m_stopPipe, O_CLOEXEC) < 0)
                    {
                        return fail();
                    }
                    m_thread = std::thread([this]() { RunRumble(); });
                    return true;
                }

                bool Update(const Wire::GamepadState& state) override
                {
                    std::vector<input_event> events;
                    {
                        std::lock_guard<std::mutex> lock(m_writeMutex);
                        BuildUinputEvents(m_last, state, m_first, events);
                        m_last = state;
                        m_first = false;
                        if (events.empty())
                        {
                            return true;
                        }
                        const size_t bytes = events.size() * sizeof(input_event);
                        return write(m_fd, events.data(), bytes) == static_cast<ssize_t>(bytes);
                    }
                }

            private:
                // Answers the kernel's force feedback requests until told to stop. A game's "upload", "play" and
                // "erase" arrive as events on the device itself; an upload and an erase must be answered through
                // a short handshake of ioctls or the game's call blocks.
                void RunRumble()
                {
                    RumbleTracker tracker;
                    RumbleTracker::Result last;
                    while (true)
                    {
                        const int64_t now = NowMs();
                        const int64_t wait = tracker.NextExpiry(now);
                        pollfd fds[2];
                        fds[0] = {m_fd, POLLIN, 0};
                        fds[1] = {m_stopPipe[0], POLLIN, 0};
                        int ready = poll(fds, 2, wait < 0 ? -1 : static_cast<int>(wait) + 1);
                        if (ready < 0 && errno != EINTR)
                        {
                            return;
                        }
                        if (ready > 0 && (fds[1].revents & POLLIN) != 0)
                        {
                            return;
                        }
                        if (ready > 0 && (fds[0].revents & (POLLERR | POLLHUP | POLLNVAL)) != 0)
                        {
                            return;
                        }
                        if (ready > 0 && (fds[0].revents & POLLIN) != 0)
                        {
                            input_event event;
                            while (read(m_fd, &event, sizeof(event)) == static_cast<ssize_t>(sizeof(event)))
                            {
                                HandleEvent(event, tracker);
                            }
                        }
                        tracker.Expire(NowMs());
                        const RumbleTracker::Result now2 = tracker.Current();
                        if (!(now2 == last))
                        {
                            last = now2;
                            if (m_onRumble)
                            {
                                m_onRumble(now2.strong, now2.weak);
                            }
                        }
                    }
                }

                void HandleEvent(const input_event& event, RumbleTracker& tracker)
                {
                    if (event.type == EV_UINPUT && event.code == UI_FF_UPLOAD)
                    {
                        uinput_ff_upload upload;
                        std::memset(&upload, 0, sizeof(upload));
                        upload.request_id = event.value;
                        if (ioctl(m_fd, UI_BEGIN_FF_UPLOAD, &upload) == 0)
                        {
                            if (upload.effect.type == FF_RUMBLE)
                            {
                                tracker.Upload(upload.effect.id, upload.effect.u.rumble.strong_magnitude,
                                               upload.effect.u.rumble.weak_magnitude, upload.effect.replay.length);
                                upload.retval = 0;
                            }
                            else
                            {
                                upload.retval = -EINVAL;
                            }
                            ioctl(m_fd, UI_END_FF_UPLOAD, &upload);
                        }
                    }
                    else if (event.type == EV_UINPUT && event.code == UI_FF_ERASE)
                    {
                        uinput_ff_erase erase;
                        std::memset(&erase, 0, sizeof(erase));
                        erase.request_id = event.value;
                        if (ioctl(m_fd, UI_BEGIN_FF_ERASE, &erase) == 0)
                        {
                            tracker.Erase(static_cast<int>(erase.effect_id));
                            erase.retval = 0;
                            ioctl(m_fd, UI_END_FF_ERASE, &erase);
                        }
                    }
                    else if (event.type == EV_FF)
                    {
                        if (event.code == FF_GAIN)
                        {
                            tracker.SetGain(static_cast<uint16_t>(event.value < 0 ? 0 : (event.value > 0xFFFF ? 0xFFFF : event.value)));
                        }
                        else
                        {
                            tracker.Play(event.code, event.value, NowMs());
                        }
                    }
                }

                int m_fd = -1;
                int m_stopPipe[2] = {-1, -1};
                std::thread m_thread;
                RumbleHandler m_onRumble;
                std::mutex m_writeMutex;
                Wire::GamepadState m_last;
                bool m_first = true;
            };

            class UinputBackend : public VirtualGamepadBackend
            {
            public:
                std::string GetName() const override { return "uinput"; }

                std::unique_ptr<VirtualGamepad> Create(Wire::GamepadType, RumbleHandler onRumble,
                                                       Wire::GamepadResult::Status& status,
                                                       std::string& message) override
                {
                    // Every kind of controller is made as an Xbox 360 style one: that is the layout games expect.
                    auto pad = std::make_unique<UinputPad>(std::move(onRumble));
                    int errorNumber = 0;
                    if (pad->Open(errorNumber))
                    {
                        return pad;
                    }
                    status = Wire::GamepadResult::Status::NoDriver;
                    if (errorNumber == ENOENT || errorNumber == ENODEV)
                    {
                        message = "This computer can't make game controllers: its kernel has no uinput support "
                                  "(there is no /dev/uinput). Load the uinput module and try again.";
                    }
                    else if (errorNumber == EACCES || errorNumber == EPERM)
                    {
                        message = "Marconi Server isn't allowed to make game controllers. Give its user access to "
                                  "/dev/uinput (for example with a udev rule that puts it in the \"input\" group) "
                                  "and try again.";
                    }
                    else
                    {
                        status = Wire::GamepadResult::Status::Failed;
                        message = std::string("The computer wouldn't make a game controller: ") +
                                  std::strerror(errorNumber) + ".";
                    }
                    return nullptr;
                }
            };
        } // namespace

        std::unique_ptr<VirtualGamepadBackend> CreateSystemGamepadBackend()
        {
            return std::make_unique<UinputBackend>();
        }
    } // namespace Server
} // namespace Marconi
