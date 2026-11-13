// Copyright (c) 2026 Red Marmoset Studios Ltd.
// Linux only: a small D-Bus client (and just enough of a service, for tests) on top of libdbus-1, which is loaded
// while the program runs. The server therefore starts, and says what is missing, on a computer without it. It has
// its own copy of the few libdbus declarations it uses, so building needs no D-Bus development files.

#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace Marconi
{
    namespace Server
    {
        // One D-Bus value, as a tree: basic values, arrays, structs, dictionary entries and variants.
        struct DBusValue
        {
            enum class Kind
            {
                Boolean,
                Byte,
                Int32,
                Uint32,
                Int64,
                Uint64,
                Double,
                String,
                ObjectPath,
                Signature,
                UnixFd,
                Variant,
                Array,
                Struct,
                DictEntry
            };

            Kind kind = Kind::Uint32;
            int64_t integer = 0;       // every integer kind, Boolean (0/1) and UnixFd (the descriptor)
            double real = 0.0;         // Double
            std::string text;          // String, ObjectPath, Signature
            std::string elementType;   // Array: the signature of one element (needed when it is empty)
            std::vector<DBusValue> items; // Variant: 1 item; Array: elements; Struct: members; DictEntry: key, value

            static DBusValue Bool(bool value);
            static DBusValue Byte(uint8_t value);
            static DBusValue Int(int32_t value);
            static DBusValue Uint(uint32_t value);
            static DBusValue Long(int64_t value);
            static DBusValue ULong(uint64_t value);
            static DBusValue Double(double value);
            static DBusValue String(const std::string& value);
            static DBusValue Path(const std::string& value);
            static DBusValue Fd(int descriptor);
            static DBusValue Variant(const DBusValue& inner);
            static DBusValue StructOf(std::vector<DBusValue> members);
            // An a{sv}: the usual "options" argument. Add() wraps each value in a variant.
            static DBusValue Dict();
            DBusValue& Add(const std::string& key, const DBusValue& value);

            // The D-Bus type signature of this value ("u", "a{sv}", "(ua{sv})"...).
            std::string Signature() const;

            // In an a{sv}: the value stored under `key` (looking through its variant), or nullptr.
            const DBusValue* Find(const std::string& key) const;

            // The number held, for the integer kinds and Boolean (0 for anything else); strings via `text`.
            uint32_t AsUint() const { return static_cast<uint32_t>(integer); }
        };

        // One message received from the bus.
        struct DBusMessageIn
        {
            enum class Type
            {
                Call,
                Return,
                Error,
                Signal
            };
            Type type = Type::Signal;
            std::string sender;
            std::string path;
            std::string interfaceName;
            std::string member;
            std::string errorName;   // Error
            uint32_t serial = 0;
            std::vector<DBusValue> args;
        };

        // A connection to a bus. Calls are safe from several threads (one at a time).
        class DBusLink
        {
        public:
            virtual ~DBusLink() = default;

            // Connects to the bus at `address`, or the session bus (DBUS_SESSION_BUS_ADDRESS) when it is empty.
            // On failure returns nullptr and `error` says why in plain words (for example that libdbus-1 isn't
            // installed).
            static std::unique_ptr<DBusLink> Connect(const std::string& address, std::string& error);

            // True when libdbus-1 can be loaded at all.
            static bool IsAvailable(std::string& error);

            virtual std::string GetUniqueName() = 0;

            // Calls a method and waits for the reply. False on an error reply or a timeout; `errorName` is then the
            // D-Bus error name (empty for a timeout or a failure to send) and `error` a readable text.
            virtual bool Call(const std::string& destination, const std::string& path, const std::string& interfaceName,
                              const std::string& method, const std::vector<DBusValue>& args,
                              std::vector<DBusValue>& reply, std::string& errorName, std::string& error,
                              int timeoutMs = 25000) = 0;

            // Sends a method call without waiting for any answer.
            virtual bool CallNoReply(const std::string& destination, const std::string& path,
                                     const std::string& interfaceName, const std::string& method,
                                     const std::vector<DBusValue>& args) = 0;

            // Asks the bus to deliver the signals matching `rule` ("type='signal',interface='...'").
            virtual bool AddMatch(const std::string& rule, std::string& error) = 0;

            // Waits up to `timeoutMs` for the next incoming message (a signal, or a call when this link is a
            // service). False when none arrived.
            virtual bool Receive(DBusMessageIn& out, int timeoutMs) = 0;

            // Service side (used by the tests' fake desktop portal): takes a bus name, answers a received call, and
            // sends a signal.
            virtual bool RequestName(const std::string& name, std::string& error) = 0;
            virtual bool Reply(const DBusMessageIn& call, const std::vector<DBusValue>& values) = 0;
            virtual bool ReplyError(const DBusMessageIn& call, const std::string& errorName,
                                    const std::string& text) = 0;
            virtual bool EmitSignal(const std::string& path, const std::string& interfaceName,
                                    const std::string& member, const std::vector<DBusValue>& args) = 0;
        };
    } // namespace Server
} // namespace Marconi
