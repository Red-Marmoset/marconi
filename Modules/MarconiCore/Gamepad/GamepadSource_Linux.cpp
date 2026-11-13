// Copyright (c) 2026 Red Marmoset Studios Ltd.
// Game controllers on Linux through evdev (/dev/input/event*): finds the ones plugged in, reads their buttons and
// sticks, and makes them shake with the kernel's force feedback.
//
// Never run against a real controller: the machine this was written on has no input devices. The reading code is
// plain evdev and follows what the kernel's Xbox driver (xpad) sends; a PlayStation controller's face buttons are
// swapped to match, which is from reading the kernel's documentation and has not been checked. The mapping of axes
// and buttons is covered by tests only through the protocol and forwarder, not through a device.

#include <MarconiCore/Gamepad/GamepadSource.h>

#include <linux/input.h>

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <mutex>
#include <string>
#include <sys/ioctl.h>
#include <unistd.h>

namespace Marconi
{
    namespace Gamepad
    {
        namespace
        {
            constexpr uint16_t kSonyVendor = 0x054c;
            // The name of the controller the Linux server makes (VirtualGamepad_Linux.cpp): a viewer on the same
            // computer must not send it back to itself.
            constexpr const char kVirtualName[] = "Marconi virtual Xbox 360 controller";

            bool TestBit(const uint8_t* bits, int bit)
            {
                return (bits[bit / 8] & (1 << (bit % 8))) != 0;
            }

            struct Axis
            {
                bool present = false;
                int32_t minimum = 0;
                int32_t maximum = 0;
            };

            // `value` from its own range to [low, high].
            int32_t Scale(int32_t value, const Axis& axis, int32_t low, int32_t high)
            {
                if (!axis.present || axis.maximum <= axis.minimum)
                {
                    return 0;
                }
                int64_t scaled = low + (static_cast<int64_t>(value - axis.minimum) * (high - low)) /
                                           (axis.maximum - axis.minimum);
                return static_cast<int32_t>(std::max<int64_t>(low, std::min<int64_t>(high, scaled)));
            }

            struct Device
            {
                int fd = -1;
                std::string path;
                PadInfo info;
                uint16_t vendor = 0;
                bool canRumble = false;
                int rumbleId = -1;
                Axis x, y, rx, ry, z, rz;
                int32_t rawX = 0, rawY = 0, rawRX = 0, rawRY = 0, rawZ = 0, rawRZ = 0;
                int hatX = 0;
                int hatY = 0;
                uint16_t buttons = 0;
            };

            class EvdevSource : public GamepadSource
            {
            public:
                ~EvdevSource() override
                {
                    for (Device& device : m_devices)
                    {
                        Close(device);
                    }
                }

                int GetSlotCount() const override { return Wire::kMaxGamepads; }

                bool Read(int slot, PadInfo& info, Wire::GamepadState& state) override
                {
                    std::lock_guard<std::mutex> lock(m_mutex);
                    Rescan();
                    if (slot < 0 || slot >= Wire::kMaxGamepads || m_devices[slot].fd < 0)
                    {
                        return false;
                    }
                    Device& device = m_devices[slot];
                    input_event event;
                    while (true)
                    {
                        ssize_t got = read(device.fd, &event, sizeof(event));
                        if (got == static_cast<ssize_t>(sizeof(event)))
                        {
                            Apply(device, event);
                            continue;
                        }
                        if (got < 0 && errno != EAGAIN && errno != EINTR)
                        {
                            Close(device); // unplugged
                            return false;
                        }
                        break;
                    }
                    info = device.info;
                    state.buttons = device.buttons;
                    if (device.hatX < 0) state.buttons |= Wire::GamepadButton::DpadLeft;
                    if (device.hatX > 0) state.buttons |= Wire::GamepadButton::DpadRight;
                    if (device.hatY < 0) state.buttons |= Wire::GamepadButton::DpadUp;
                    if (device.hatY > 0) state.buttons |= Wire::GamepadButton::DpadDown;
                    state.leftX = static_cast<int16_t>(Scale(device.rawX, device.x, -32768, 32767));
                    // evdev has down positive; the protocol has up positive.
                    state.leftY = static_cast<int16_t>(~Scale(device.rawY, device.y, -32768, 32767));
                    state.rightX = static_cast<int16_t>(Scale(device.rawRX, device.rx, -32768, 32767));
                    state.rightY = static_cast<int16_t>(~Scale(device.rawRY, device.ry, -32768, 32767));
                    state.leftTrigger = static_cast<uint8_t>(Scale(device.rawZ, device.z, 0, 255));
                    state.rightTrigger = static_cast<uint8_t>(Scale(device.rawRZ, device.rz, 0, 255));
                    state.battery = Wire::kBatteryUnknown;
                    return true;
                }

                void SetRumble(int slot, uint16_t strong, uint16_t weak) override
                {
                    std::lock_guard<std::mutex> lock(m_mutex);
                    if (slot < 0 || slot >= Wire::kMaxGamepads)
                    {
                        return;
                    }
                    Device& device = m_devices[slot];
                    if (device.fd < 0 || !device.canRumble)
                    {
                        return;
                    }
                    ff_effect effect;
                    std::memset(&effect, 0, sizeof(effect));
                    effect.type = FF_RUMBLE;
                    effect.id = static_cast<int16_t>(device.rumbleId);
                    effect.replay.length = 0xFFFF; // as long as the kernel allows; the server sends a stop when due
                    effect.u.rumble.strong_magnitude = strong;
                    effect.u.rumble.weak_magnitude = weak;
                    if (ioctl(device.fd, EVIOCSFF, &effect) < 0)
                    {
                        return;
                    }
                    device.rumbleId = effect.id;
                    input_event play;
                    std::memset(&play, 0, sizeof(play));
                    play.type = EV_FF;
                    play.code = static_cast<uint16_t>(device.rumbleId);
                    play.value = (strong != 0 || weak != 0) ? 1 : 0;
                    if (write(device.fd, &play, sizeof(play)) < 0)
                    {
                        // The controller went away; Read notices.
                    }
                }

            private:
                static void Close(Device& device)
                {
                    if (device.fd >= 0)
                    {
                        close(device.fd);
                    }
                    device = Device();
                }

                // Looks for controllers plugged in since the last look, at most once a second.
                void Rescan()
                {
                    const auto now = std::chrono::steady_clock::now();
                    if (m_scanned && now - m_lastScan < std::chrono::seconds(1))
                    {
                        return;
                    }
                    m_scanned = true;
                    m_lastScan = now;
                    DIR* directory = opendir("/dev/input");
                    if (directory == nullptr)
                    {
                        return;
                    }
                    while (dirent* entry = readdir(directory))
                    {
                        if (std::strncmp(entry->d_name, "event", 5) != 0)
                        {
                            continue;
                        }
                        const std::string path = std::string("/dev/input/") + entry->d_name;
                        bool known = false;
                        int free = -1;
                        for (int i = 0; i < Wire::kMaxGamepads; ++i)
                        {
                            if (m_devices[i].fd >= 0 && m_devices[i].path == path)
                            {
                                known = true;
                            }
                            if (m_devices[i].fd < 0 && free < 0)
                            {
                                free = i;
                            }
                        }
                        if (known || free < 0)
                        {
                            continue;
                        }
                        OpenIfGamepad(path, m_devices[free]);
                    }
                    closedir(directory);
                }

                static void OpenIfGamepad(const std::string& path, Device& device)
                {
                    int fd = open(path.c_str(), O_RDWR | O_NONBLOCK | O_CLOEXEC); // read-write so it can shake
                    bool writable = fd >= 0;
                    if (fd < 0)
                    {
                        fd = open(path.c_str(), O_RDONLY | O_NONBLOCK | O_CLOEXEC);
                    }
                    if (fd < 0)
                    {
                        return;
                    }
                    uint8_t events[(EV_MAX + 8) / 8] = {};
                    uint8_t keys[(KEY_MAX + 8) / 8] = {};
                    uint8_t absolute[(ABS_MAX + 8) / 8] = {};
                    ioctl(fd, EVIOCGBIT(0, sizeof(events)), events);
                    ioctl(fd, EVIOCGBIT(EV_KEY, sizeof(keys)), keys);
                    ioctl(fd, EVIOCGBIT(EV_ABS, sizeof(absolute)), absolute);
                    char name[128] = {};
                    ioctl(fd, EVIOCGNAME(sizeof(name) - 1), name);
                    // A controller has a gamepad button and a stick; a mouse or keyboard has neither.
                    const bool isPad = TestBit(events, EV_KEY) && TestBit(events, EV_ABS) &&
                                       TestBit(keys, BTN_SOUTH) && TestBit(absolute, ABS_X) && TestBit(absolute, ABS_Y);
                    if (!isPad || std::strcmp(name, kVirtualName) == 0)
                    {
                        close(fd);
                        return;
                    }
                    input_id id;
                    std::memset(&id, 0, sizeof(id));
                    ioctl(fd, EVIOCGID, &id);

                    device = Device();
                    device.fd = fd;
                    device.path = path;
                    device.vendor = id.vendor;
                    device.info.name = name;
                    device.info.type = id.vendor == kSonyVendor ? Wire::GamepadType::PlayStation
                                       : id.vendor == 0x045e      ? Wire::GamepadType::Xbox
                                                                  : Wire::GamepadType::Generic;
                    auto axis = [&](int code, Axis& out, int32_t& raw) {
                        if (!TestBit(absolute, code))
                        {
                            return;
                        }
                        input_absinfo info;
                        std::memset(&info, 0, sizeof(info));
                        if (ioctl(fd, EVIOCGABS(code), &info) == 0)
                        {
                            out.present = true;
                            out.minimum = info.minimum;
                            out.maximum = info.maximum;
                            raw = info.value;
                        }
                    };
                    axis(ABS_X, device.x, device.rawX);
                    axis(ABS_Y, device.y, device.rawY);
                    axis(ABS_RX, device.rx, device.rawRX);
                    axis(ABS_RY, device.ry, device.rawRY);
                    axis(ABS_Z, device.z, device.rawZ);
                    axis(ABS_RZ, device.rz, device.rawRZ);
                    uint8_t feedback[(FF_MAX + 8) / 8] = {};
                    if (writable && TestBit(events, EV_FF) && ioctl(fd, EVIOCGBIT(EV_FF, sizeof(feedback)), feedback) >= 0)
                    {
                        device.canRumble = TestBit(feedback, FF_RUMBLE);
                    }
                    // A controller already holding a button when it is found starts from rest; the next press is seen.
                }

                static void Apply(Device& device, const input_event& event)
                {
                    if (event.type == EV_KEY)
                    {
                        uint16_t bit = 0;
                        const bool sony = device.vendor == kSonyVendor;
                        switch (event.code)
                        {
                        case BTN_SOUTH: bit = Wire::GamepadButton::A; break;
                        case BTN_EAST: bit = Wire::GamepadButton::B; break;
                        // xpad calls its X button BTN_NORTH (0x133) and Y BTN_WEST (0x134); the PlayStation drivers
                        // name the buttons by where they sit, so there Square (west) is X and Triangle (north) is Y.
                        case BTN_NORTH: bit = sony ? Wire::GamepadButton::Y : Wire::GamepadButton::X; break;
                        case BTN_WEST: bit = sony ? Wire::GamepadButton::X : Wire::GamepadButton::Y; break;
                        case BTN_TL: bit = Wire::GamepadButton::LeftShoulder; break;
                        case BTN_TR: bit = Wire::GamepadButton::RightShoulder; break;
                        case BTN_SELECT: bit = Wire::GamepadButton::Back; break;
                        case BTN_START: bit = Wire::GamepadButton::Start; break;
                        case BTN_MODE: bit = Wire::GamepadButton::Guide; break;
                        case BTN_THUMBL: bit = Wire::GamepadButton::LeftStick; break;
                        case BTN_THUMBR: bit = Wire::GamepadButton::RightStick; break;
                        case BTN_DPAD_UP: bit = Wire::GamepadButton::DpadUp; break;
                        case BTN_DPAD_DOWN: bit = Wire::GamepadButton::DpadDown; break;
                        case BTN_DPAD_LEFT: bit = Wire::GamepadButton::DpadLeft; break;
                        case BTN_DPAD_RIGHT: bit = Wire::GamepadButton::DpadRight; break;
                        default: return;
                        }
                        if (event.value != 0)
                        {
                            device.buttons |= bit;
                        }
                        else
                        {
                            device.buttons &= static_cast<uint16_t>(~bit);
                        }
                    }
                    else if (event.type == EV_ABS)
                    {
                        switch (event.code)
                        {
                        case ABS_X: device.rawX = event.value; break;
                        case ABS_Y: device.rawY = event.value; break;
                        case ABS_RX: device.rawRX = event.value; break;
                        case ABS_RY: device.rawRY = event.value; break;
                        case ABS_Z: device.rawZ = event.value; break;
                        case ABS_RZ: device.rawRZ = event.value; break;
                        case ABS_HAT0X: device.hatX = event.value; break;
                        case ABS_HAT0Y: device.hatY = event.value; break;
                        default: break;
                        }
                    }
                }

                std::mutex m_mutex;
                Device m_devices[Wire::kMaxGamepads];
                bool m_scanned = false;
                std::chrono::steady_clock::time_point m_lastScan;
            };
        } // namespace

        std::unique_ptr<GamepadSource> CreateSystemGamepadSource(std::string&)
        {
            return std::make_unique<EvdevSource>();
        }
    } // namespace Gamepad
} // namespace Marconi
