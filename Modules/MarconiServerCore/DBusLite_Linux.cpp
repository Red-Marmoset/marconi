// Copyright (c) 2026 Red Marmoset Studios Ltd.
// Linux only: the D-Bus client in DBusLite.h, on libdbus-1 loaded with dlopen (the value tree is in DBusValue.cpp).
//
// The declarations below are copies of the small, stable part of libdbus's public C interface that is used here
// (checked against the real <dbus/dbus.h> with a throwaway program while writing this), so no D-Bus development
// package is needed to build the server.

#include "DBusLite.h"

#include <dlfcn.h>
#include <unistd.h>

#include <chrono>
#include <cstring>
#include <map>
#include <mutex>

namespace Marconi
{
    namespace Server
    {
        namespace
        {
            // ---- libdbus declarations (copied from its public header) --------------------------------------

            struct LdbConnection;
            struct LdbMessage;
            struct LdbError
            {
                const char* name;
                const char* message;
                unsigned int dummies;
                void* padding;
            };
            // DBusMessageIter is a struct of a few pointers and ints, 72 bytes today; this is bigger, aligned.
            struct alignas(16) LdbIter
            {
                unsigned char data[128];
            };

            constexpr int kTypeArray = 'a';
            constexpr int kTypeVariant = 'v';
            constexpr int kTypeStruct = 'r';
            constexpr int kTypeDictEntry = 'e';
            constexpr int kBusSession = 0;
            constexpr int kMessageCall = 1;
            constexpr int kMessageReturn = 2;
            constexpr int kMessageError = 3;
            constexpr int kMessageSignal = 4;
            constexpr unsigned kNameDoNotQueue = 4;

            struct Libdbus
            {
                void* handle = nullptr;
                int (*threadsInit)() = nullptr;
                void (*errorInit)(LdbError*) = nullptr;
                void (*errorFree)(LdbError*) = nullptr;
                LdbConnection* (*busGetPrivate)(int, LdbError*) = nullptr;
                LdbConnection* (*connectionOpenPrivate)(const char*, LdbError*) = nullptr;
                int (*busRegister)(LdbConnection*, LdbError*) = nullptr;
                void (*setExitOnDisconnect)(LdbConnection*, unsigned) = nullptr;
                void (*connectionClose)(LdbConnection*) = nullptr;
                void (*connectionUnref)(LdbConnection*) = nullptr;
                LdbMessage* (*newMethodCall)(const char*, const char*, const char*, const char*) = nullptr;
                LdbMessage* (*newSignal)(const char*, const char*, const char*) = nullptr;
                LdbMessage* (*newMethodReturn)(LdbMessage*) = nullptr;
                LdbMessage* (*newError)(LdbMessage*, const char*, const char*) = nullptr;
                void (*messageUnref)(LdbMessage*) = nullptr;
                LdbMessage* (*messageRef)(LdbMessage*) = nullptr;
                unsigned (*setNoReply)(LdbMessage*, unsigned) = nullptr;
                int (*messageGetType)(LdbMessage*) = nullptr;
                const char* (*getPath)(LdbMessage*) = nullptr;
                const char* (*getInterface)(LdbMessage*) = nullptr;
                const char* (*getMember)(LdbMessage*) = nullptr;
                const char* (*getSender)(LdbMessage*) = nullptr;
                const char* (*getErrorName)(LdbMessage*) = nullptr;
                uint32_t (*getSerial)(LdbMessage*) = nullptr;
                unsigned (*iterInit)(LdbMessage*, LdbIter*) = nullptr;
                void (*iterInitAppend)(LdbMessage*, LdbIter*) = nullptr;
                unsigned (*iterNext)(LdbIter*) = nullptr;
                int (*iterArgType)(LdbIter*) = nullptr;
                int (*iterElementType)(LdbIter*) = nullptr;
                void (*iterGetBasic)(LdbIter*, void*) = nullptr;
                void (*iterRecurse)(LdbIter*, LdbIter*) = nullptr;
                unsigned (*iterAppendBasic)(LdbIter*, int, const void*) = nullptr;
                unsigned (*iterOpenContainer)(LdbIter*, int, const char*, LdbIter*) = nullptr;
                unsigned (*iterCloseContainer)(LdbIter*, LdbIter*) = nullptr;
                LdbMessage* (*sendWithReplyAndBlock)(LdbConnection*, LdbMessage*, int, LdbError*) = nullptr;
                unsigned (*send)(LdbConnection*, LdbMessage*, uint32_t*) = nullptr;
                void (*flush)(LdbConnection*) = nullptr;
                unsigned (*readWrite)(LdbConnection*, int) = nullptr;
                LdbMessage* (*popMessage)(LdbConnection*) = nullptr;
                unsigned (*busAddMatch)(LdbConnection*, const char*, LdbError*) = nullptr;
                int (*busRequestName)(LdbConnection*, const char*, unsigned, LdbError*) = nullptr;
                const char* (*busGetUniqueName)(LdbConnection*) = nullptr;
                char* (*iterGetSignature)(LdbIter*) = nullptr;
                void (*dbusFree)(void*) = nullptr;
            };

            Libdbus g_lib;
            std::string g_loadError;
            std::once_flag g_loadOnce;

            template <typename T>
            bool Resolve(T& target, const char* name)
            {
                target = reinterpret_cast<T>(dlsym(g_lib.handle, name));
                return target != nullptr;
            }

            void Load()
            {
                g_lib.handle = dlopen("libdbus-1.so.3", RTLD_NOW | RTLD_LOCAL);
                if (g_lib.handle == nullptr)
                {
                    g_lib.handle = dlopen("libdbus-1.so", RTLD_NOW | RTLD_LOCAL);
                }
                if (g_lib.handle == nullptr)
                {
                    g_loadError = "The D-Bus library (libdbus-1) isn't installed on this computer.";
                    return;
                }
                bool ok = Resolve(g_lib.threadsInit, "dbus_threads_init_default") &&
                          Resolve(g_lib.errorInit, "dbus_error_init") && Resolve(g_lib.errorFree, "dbus_error_free") &&
                          Resolve(g_lib.busGetPrivate, "dbus_bus_get_private") &&
                          Resolve(g_lib.connectionOpenPrivate, "dbus_connection_open_private") &&
                          Resolve(g_lib.busRegister, "dbus_bus_register") &&
                          Resolve(g_lib.setExitOnDisconnect, "dbus_connection_set_exit_on_disconnect") &&
                          Resolve(g_lib.connectionClose, "dbus_connection_close") &&
                          Resolve(g_lib.connectionUnref, "dbus_connection_unref") &&
                          Resolve(g_lib.newMethodCall, "dbus_message_new_method_call") &&
                          Resolve(g_lib.newSignal, "dbus_message_new_signal") &&
                          Resolve(g_lib.newMethodReturn, "dbus_message_new_method_return") &&
                          Resolve(g_lib.newError, "dbus_message_new_error") &&
                          Resolve(g_lib.messageUnref, "dbus_message_unref") &&
                          Resolve(g_lib.messageRef, "dbus_message_ref") &&
                          Resolve(g_lib.setNoReply, "dbus_message_set_no_reply") &&
                          Resolve(g_lib.messageGetType, "dbus_message_get_type") &&
                          Resolve(g_lib.getPath, "dbus_message_get_path") &&
                          Resolve(g_lib.getInterface, "dbus_message_get_interface") &&
                          Resolve(g_lib.getMember, "dbus_message_get_member") &&
                          Resolve(g_lib.getSender, "dbus_message_get_sender") &&
                          Resolve(g_lib.getErrorName, "dbus_message_get_error_name") &&
                          Resolve(g_lib.getSerial, "dbus_message_get_serial") &&
                          Resolve(g_lib.iterInit, "dbus_message_iter_init") &&
                          Resolve(g_lib.iterInitAppend, "dbus_message_iter_init_append") &&
                          Resolve(g_lib.iterNext, "dbus_message_iter_next") &&
                          Resolve(g_lib.iterArgType, "dbus_message_iter_get_arg_type") &&
                          Resolve(g_lib.iterElementType, "dbus_message_iter_get_element_type") &&
                          Resolve(g_lib.iterGetBasic, "dbus_message_iter_get_basic") &&
                          Resolve(g_lib.iterRecurse, "dbus_message_iter_recurse") &&
                          Resolve(g_lib.iterAppendBasic, "dbus_message_iter_append_basic") &&
                          Resolve(g_lib.iterOpenContainer, "dbus_message_iter_open_container") &&
                          Resolve(g_lib.iterCloseContainer, "dbus_message_iter_close_container") &&
                          Resolve(g_lib.sendWithReplyAndBlock, "dbus_connection_send_with_reply_and_block") &&
                          Resolve(g_lib.send, "dbus_connection_send") && Resolve(g_lib.flush, "dbus_connection_flush") &&
                          Resolve(g_lib.readWrite, "dbus_connection_read_write") &&
                          Resolve(g_lib.popMessage, "dbus_connection_pop_message") &&
                          Resolve(g_lib.busAddMatch, "dbus_bus_add_match") &&
                          Resolve(g_lib.busRequestName, "dbus_bus_request_name") &&
                          Resolve(g_lib.busGetUniqueName, "dbus_bus_get_unique_name") &&
                          Resolve(g_lib.iterGetSignature, "dbus_message_iter_get_signature") &&
                          Resolve(g_lib.dbusFree, "dbus_free");
                if (!ok)
                {
                    g_loadError = "The D-Bus library (libdbus-1) on this computer is not a version Marconi Server "
                                  "understands.";
                    g_lib.handle = nullptr;
                    return;
                }
                g_lib.threadsInit();
            }

            bool EnsureLoaded(std::string& error)
            {
                std::call_once(g_loadOnce, Load);
                if (g_lib.handle == nullptr)
                {
                    error = g_loadError;
                    return false;
                }
                return true;
            }

            // ---- converting values to and from libdbus messages ------------------------------------------

            void AppendValue(LdbIter* iter, const DBusValue& value)
            {
                using Kind = DBusValue::Kind;
                switch (value.kind)
                {
                    case Kind::Boolean:
                    {
                        uint32_t v = value.integer != 0 ? 1 : 0;
                        g_lib.iterAppendBasic(iter, 'b', &v);
                        break;
                    }
                    case Kind::Byte:
                    {
                        uint8_t v = static_cast<uint8_t>(value.integer);
                        g_lib.iterAppendBasic(iter, 'y', &v);
                        break;
                    }
                    case Kind::Int32:
                    {
                        int32_t v = static_cast<int32_t>(value.integer);
                        g_lib.iterAppendBasic(iter, 'i', &v);
                        break;
                    }
                    case Kind::Uint32:
                    {
                        uint32_t v = static_cast<uint32_t>(value.integer);
                        g_lib.iterAppendBasic(iter, 'u', &v);
                        break;
                    }
                    case Kind::Int64:
                    {
                        int64_t v = value.integer;
                        g_lib.iterAppendBasic(iter, 'x', &v);
                        break;
                    }
                    case Kind::Uint64:
                    {
                        uint64_t v = static_cast<uint64_t>(value.integer);
                        g_lib.iterAppendBasic(iter, 't', &v);
                        break;
                    }
                    case Kind::Double:
                    {
                        double v = value.real;
                        g_lib.iterAppendBasic(iter, 'd', &v);
                        break;
                    }
                    case Kind::String:
                    case Kind::ObjectPath:
                    case Kind::Signature:
                    {
                        const char* text = value.text.c_str();
                        int type = value.kind == Kind::String ? 's' : value.kind == Kind::ObjectPath ? 'o' : 'g';
                        g_lib.iterAppendBasic(iter, type, &text);
                        break;
                    }
                    case Kind::UnixFd:
                    {
                        int v = static_cast<int>(value.integer);
                        g_lib.iterAppendBasic(iter, 'h', &v);
                        break;
                    }
                    case Kind::Variant:
                    {
                        LdbIter sub;
                        std::string signature = value.items.empty() ? "s" : value.items[0].Signature();
                        g_lib.iterOpenContainer(iter, kTypeVariant, signature.c_str(), &sub);
                        if (!value.items.empty())
                        {
                            AppendValue(&sub, value.items[0]);
                        }
                        g_lib.iterCloseContainer(iter, &sub);
                        break;
                    }
                    case Kind::Array:
                    {
                        LdbIter sub;
                        g_lib.iterOpenContainer(iter, kTypeArray, value.elementType.c_str(), &sub);
                        for (const DBusValue& item : value.items)
                        {
                            AppendValue(&sub, item);
                        }
                        g_lib.iterCloseContainer(iter, &sub);
                        break;
                    }
                    case Kind::Struct:
                    case Kind::DictEntry:
                    {
                        LdbIter sub;
                        g_lib.iterOpenContainer(iter, value.kind == Kind::Struct ? kTypeStruct : kTypeDictEntry,
                                                nullptr, &sub);
                        for (const DBusValue& item : value.items)
                        {
                            AppendValue(&sub, item);
                        }
                        g_lib.iterCloseContainer(iter, &sub);
                        break;
                    }
                }
            }

            // The signature of the value the iterator points at ("a{sv}", "(ua{sv})"...), which libdbus works out
            // even for an empty array.
            std::string SignatureOfIter(LdbIter* iter)
            {
                char* signature = g_lib.iterGetSignature(iter);
                std::string result = signature != nullptr ? signature : "";
                if (signature != nullptr)
                {
                    g_lib.dbusFree(signature);
                }
                return result;
            }

            DBusValue ReadValue(LdbIter* iter)
            {
                using Kind = DBusValue::Kind;
                DBusValue value;
                int type = g_lib.iterArgType(iter);
                switch (type)
                {
                    case 'b':
                    {
                        uint32_t v = 0;
                        g_lib.iterGetBasic(iter, &v);
                        value = DBusValue::Bool(v != 0);
                        break;
                    }
                    case 'y':
                    {
                        uint8_t v = 0;
                        g_lib.iterGetBasic(iter, &v);
                        value = DBusValue::Byte(v);
                        break;
                    }
                    case 'n':
                    {
                        int16_t v = 0;
                        g_lib.iterGetBasic(iter, &v);
                        value = DBusValue::Int(v);
                        break;
                    }
                    case 'q':
                    {
                        uint16_t v = 0;
                        g_lib.iterGetBasic(iter, &v);
                        value = DBusValue::Uint(v);
                        break;
                    }
                    case 'i':
                    {
                        int32_t v = 0;
                        g_lib.iterGetBasic(iter, &v);
                        value = DBusValue::Int(v);
                        break;
                    }
                    case 'u':
                    {
                        uint32_t v = 0;
                        g_lib.iterGetBasic(iter, &v);
                        value = DBusValue::Uint(v);
                        break;
                    }
                    case 'x':
                    {
                        int64_t v = 0;
                        g_lib.iterGetBasic(iter, &v);
                        value = DBusValue::Long(v);
                        break;
                    }
                    case 't':
                    {
                        uint64_t v = 0;
                        g_lib.iterGetBasic(iter, &v);
                        value = DBusValue::ULong(v);
                        break;
                    }
                    case 'd':
                    {
                        double v = 0;
                        g_lib.iterGetBasic(iter, &v);
                        value = DBusValue::Double(v);
                        break;
                    }
                    case 's':
                    case 'o':
                    case 'g':
                    {
                        const char* text = nullptr;
                        g_lib.iterGetBasic(iter, &text);
                        value.kind = type == 's' ? Kind::String : type == 'o' ? Kind::ObjectPath : Kind::Signature;
                        value.text = text != nullptr ? text : "";
                        break;
                    }
                    case 'h':
                    {
                        int fd = -1;
                        g_lib.iterGetBasic(iter, &fd);
                        value = DBusValue::Fd(fd); // libdbus hands over a new descriptor: the caller closes it
                        break;
                    }
                    case kTypeVariant:
                    {
                        LdbIter sub;
                        g_lib.iterRecurse(iter, &sub);
                        value.kind = Kind::Variant;
                        if (g_lib.iterArgType(&sub) != 0)
                        {
                            value.items.push_back(ReadValue(&sub));
                        }
                        break;
                    }
                    case kTypeArray:
                    {
                        value.kind = Kind::Array;
                        value.elementType = SignatureOfIter(iter).substr(1);
                        LdbIter sub;
                        g_lib.iterRecurse(iter, &sub);
                        while (g_lib.iterArgType(&sub) != 0)
                        {
                            value.items.push_back(ReadValue(&sub));
                            if (g_lib.iterNext(&sub) == 0)
                            {
                                break;
                            }
                        }
                        break;
                    }
                    case kTypeStruct:
                    case kTypeDictEntry:
                    {
                        value.kind = type == kTypeStruct ? Kind::Struct : Kind::DictEntry;
                        LdbIter sub;
                        g_lib.iterRecurse(iter, &sub);
                        while (g_lib.iterArgType(&sub) != 0)
                        {
                            value.items.push_back(ReadValue(&sub));
                            if (g_lib.iterNext(&sub) == 0)
                            {
                                break;
                            }
                        }
                        break;
                    }
                    default: break;
                }
                return value;
            }

            std::vector<DBusValue> ReadArgs(LdbMessage* message)
            {
                std::vector<DBusValue> args;
                LdbIter iter;
                if (g_lib.iterInit(message, &iter) != 0)
                {
                    while (g_lib.iterArgType(&iter) != 0)
                    {
                        args.push_back(ReadValue(&iter));
                        if (g_lib.iterNext(&iter) == 0)
                        {
                            break;
                        }
                    }
                }
                return args;
            }

            void AppendArgs(LdbMessage* message, const std::vector<DBusValue>& args)
            {
                LdbIter iter;
                g_lib.iterInitAppend(message, &iter);
                for (const DBusValue& arg : args)
                {
                    AppendValue(&iter, arg);
                }
            }

            class Link : public DBusLink
            {
            public:
                ~Link() override
                {
                    for (auto& entry : m_calls)
                    {
                        g_lib.messageUnref(entry.second);
                    }
                    if (m_connection != nullptr)
                    {
                        g_lib.connectionClose(m_connection);
                        g_lib.connectionUnref(m_connection);
                    }
                }

                bool Open(const std::string& address, std::string& error)
                {
                    LdbError dbusError;
                    g_lib.errorInit(&dbusError);
                    if (address.empty())
                    {
                        const char* session = std::getenv("DBUS_SESSION_BUS_ADDRESS");
                        if (session == nullptr || *session == '\0')
                        {
                            error = "There is no D-Bus session (DBUS_SESSION_BUS_ADDRESS is not set), so this "
                                    "program can't ask the desktop for permission to share the screen. Start it from "
                                    "inside the desktop session.";
                            return false;
                        }
                        m_connection = g_lib.busGetPrivate(kBusSession, &dbusError);
                    }
                    else
                    {
                        m_connection = g_lib.connectionOpenPrivate(address.c_str(), &dbusError);
                        if (m_connection != nullptr && g_lib.busRegister(m_connection, &dbusError) == 0)
                        {
                            g_lib.connectionClose(m_connection);
                            g_lib.connectionUnref(m_connection);
                            m_connection = nullptr;
                        }
                    }
                    if (m_connection == nullptr)
                    {
                        error = std::string("Could not connect to the desktop's message bus: ") +
                                (dbusError.message != nullptr ? dbusError.message : "unknown reason");
                        g_lib.errorFree(&dbusError);
                        return false;
                    }
                    g_lib.errorFree(&dbusError);
                    g_lib.setExitOnDisconnect(m_connection, 0);
                    return true;
                }

                std::string GetUniqueName() override
                {
                    std::lock_guard<std::mutex> lock(m_mutex);
                    const char* name = g_lib.busGetUniqueName(m_connection);
                    return name != nullptr ? name : "";
                }

                bool Call(const std::string& destination, const std::string& path, const std::string& interfaceName,
                          const std::string& method, const std::vector<DBusValue>& args,
                          std::vector<DBusValue>& reply, std::string& errorName, std::string& error,
                          int timeoutMs) override
                {
                    errorName.clear();
                    LdbMessage* message = g_lib.newMethodCall(destination.c_str(), path.c_str(),
                                                              interfaceName.c_str(), method.c_str());
                    if (message == nullptr)
                    {
                        error = "Out of memory";
                        return false;
                    }
                    AppendArgs(message, args);
                    LdbError dbusError;
                    g_lib.errorInit(&dbusError);
                    // Not under the mutex: a slow answer (the person reading a permission prompt) must not stop
                    // input being sent. libdbus allows this from several threads.
                    LdbMessage* answer = g_lib.sendWithReplyAndBlock(m_connection, message, timeoutMs, &dbusError);
                    g_lib.messageUnref(message);
                    if (answer == nullptr)
                    {
                        errorName = dbusError.name != nullptr ? dbusError.name : "";
                        error = dbusError.message != nullptr ? dbusError.message : "no answer";
                        g_lib.errorFree(&dbusError);
                        return false;
                    }
                    reply = ReadArgs(answer);
                    g_lib.messageUnref(answer);
                    return true;
                }

                bool CallNoReply(const std::string& destination, const std::string& path,
                                 const std::string& interfaceName, const std::string& method,
                                 const std::vector<DBusValue>& args) override
                {
                    LdbMessage* message = g_lib.newMethodCall(destination.c_str(), path.c_str(),
                                                              interfaceName.c_str(), method.c_str());
                    if (message == nullptr)
                    {
                        return false;
                    }
                    g_lib.setNoReply(message, 1);
                    AppendArgs(message, args);
                    bool ok = g_lib.send(m_connection, message, nullptr) != 0;
                    g_lib.messageUnref(message);
                    g_lib.flush(m_connection);
                    return ok;
                }

                bool AddMatch(const std::string& rule, std::string& error) override
                {
                    LdbError dbusError;
                    g_lib.errorInit(&dbusError);
                    g_lib.busAddMatch(m_connection, rule.c_str(), &dbusError);
                    if (dbusError.name != nullptr)
                    {
                        error = dbusError.message != nullptr ? dbusError.message : dbusError.name;
                        g_lib.errorFree(&dbusError);
                        return false;
                    }
                    return true;
                }

                bool Receive(DBusMessageIn& out, int timeoutMs) override
                {
                    std::lock_guard<std::mutex> lock(m_receiveMutex);
                    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
                    while (true)
                    {
                        LdbMessage* message = g_lib.popMessage(m_connection);
                        if (message != nullptr)
                        {
                            Fill(message, out);
                            return true;
                        }
                        auto left = std::chrono::duration_cast<std::chrono::milliseconds>(
                                        deadline - std::chrono::steady_clock::now())
                                        .count();
                        if (left < 0)
                        {
                            left = 0;
                        }
                        // One round of reading and writing (a timeout of 0 still reads what has arrived). While it
                        // waits, libdbus holds the connection's I/O, so a sender in another thread waits too: callers
                        // that must not delay sends poll with a zero timeout and sleep in between.
                        if (g_lib.readWrite(m_connection, static_cast<int>(left < 50 ? left : 50)) == 0)
                        {
                            return false; // disconnected
                        }
                        LdbMessage* arrived = g_lib.popMessage(m_connection);
                        if (arrived != nullptr)
                        {
                            Fill(arrived, out);
                            return true;
                        }
                        if (left == 0)
                        {
                            return false;
                        }
                    }
                }

                bool RequestName(const std::string& name, std::string& error) override
                {
                    LdbError dbusError;
                    g_lib.errorInit(&dbusError);
                    int result = g_lib.busRequestName(m_connection, name.c_str(), kNameDoNotQueue, &dbusError);
                    if (result != 1)
                    {
                        error = dbusError.message != nullptr ? dbusError.message : "name is taken";
                        g_lib.errorFree(&dbusError);
                        return false;
                    }
                    return true;
                }

                bool Reply(const DBusMessageIn& call, const std::vector<DBusValue>& values) override
                {
                    LdbMessage* original = TakeCall(call.serial);
                    if (original == nullptr)
                    {
                        return false;
                    }
                    LdbMessage* message = g_lib.newMethodReturn(original);
                    g_lib.messageUnref(original);
                    if (message == nullptr)
                    {
                        return false;
                    }
                    AppendArgs(message, values);
                    bool ok = g_lib.send(m_connection, message, nullptr) != 0;
                    g_lib.messageUnref(message);
                    g_lib.flush(m_connection);
                    return ok;
                }

                bool ReplyError(const DBusMessageIn& call, const std::string& errorName,
                                const std::string& text) override
                {
                    LdbMessage* original = TakeCall(call.serial);
                    if (original == nullptr)
                    {
                        return false;
                    }
                    LdbMessage* message = g_lib.newError(original, errorName.c_str(), text.c_str());
                    g_lib.messageUnref(original);
                    if (message == nullptr)
                    {
                        return false;
                    }
                    bool ok = g_lib.send(m_connection, message, nullptr) != 0;
                    g_lib.messageUnref(message);
                    g_lib.flush(m_connection);
                    return ok;
                }

                bool EmitSignal(const std::string& path, const std::string& interfaceName, const std::string& member,
                                const std::vector<DBusValue>& args) override
                {
                    LdbMessage* message = g_lib.newSignal(path.c_str(), interfaceName.c_str(), member.c_str());
                    if (message == nullptr)
                    {
                        return false;
                    }
                    AppendArgs(message, args);
                    bool ok = g_lib.send(m_connection, message, nullptr) != 0;
                    g_lib.messageUnref(message);
                    g_lib.flush(m_connection);
                    return ok;
                }

            private:
                void Fill(LdbMessage* message, DBusMessageIn& out)
                {
                    out = DBusMessageIn();
                    int type = g_lib.messageGetType(message);
                    out.type = type == kMessageCall     ? DBusMessageIn::Type::Call
                               : type == kMessageReturn ? DBusMessageIn::Type::Return
                               : type == kMessageError  ? DBusMessageIn::Type::Error
                                                        : DBusMessageIn::Type::Signal;
                    (void)kMessageSignal;
                    auto text = [](const char* s) { return std::string(s != nullptr ? s : ""); };
                    out.sender = text(g_lib.getSender(message));
                    out.path = text(g_lib.getPath(message));
                    out.interfaceName = text(g_lib.getInterface(message));
                    out.member = text(g_lib.getMember(message));
                    out.errorName = text(g_lib.getErrorName(message));
                    out.serial = g_lib.getSerial(message);
                    out.args = ReadArgs(message);
                    if (out.type == DBusMessageIn::Type::Call)
                    {
                        std::lock_guard<std::mutex> lock(m_mutex);
                        m_calls[out.serial] = message; // kept until answered
                    }
                    else
                    {
                        g_lib.messageUnref(message);
                    }
                }

                LdbMessage* TakeCall(uint32_t serial)
                {
                    std::lock_guard<std::mutex> lock(m_mutex);
                    auto found = m_calls.find(serial);
                    if (found == m_calls.end())
                    {
                        return nullptr;
                    }
                    LdbMessage* message = found->second;
                    m_calls.erase(found);
                    return message;
                }

                LdbConnection* m_connection = nullptr;
                std::mutex m_mutex;
                std::mutex m_receiveMutex;
                std::map<uint32_t, LdbMessage*> m_calls;
            };
        } // namespace

        bool DBusLink::IsAvailable(std::string& error) { return EnsureLoaded(error); }

        std::unique_ptr<DBusLink> DBusLink::Connect(const std::string& address, std::string& error)
        {
            if (!EnsureLoaded(error))
            {
                return nullptr;
            }
            auto link = std::make_unique<Link>();
            if (!link->Open(address, error))
            {
                return nullptr;
            }
            return link;
        }
    } // namespace Server
} // namespace Marconi
