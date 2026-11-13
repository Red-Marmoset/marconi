// Copyright (c) 2026 Red Marmoset Studios Ltd.
// The D-Bus value tree of DBusLite.h: building values and working out their type signatures.

#include "DBusLite.h"

namespace Marconi
{
    namespace Server
    {
        // ---- DBusValue ----------------------------------------------------------------------------------------

        DBusValue DBusValue::Bool(bool value)
        {
            DBusValue v;
            v.kind = Kind::Boolean;
            v.integer = value ? 1 : 0;
            return v;
        }
        DBusValue DBusValue::Byte(uint8_t value)
        {
            DBusValue v;
            v.kind = Kind::Byte;
            v.integer = value;
            return v;
        }
        DBusValue DBusValue::Int(int32_t value)
        {
            DBusValue v;
            v.kind = Kind::Int32;
            v.integer = value;
            return v;
        }
        DBusValue DBusValue::Uint(uint32_t value)
        {
            DBusValue v;
            v.kind = Kind::Uint32;
            v.integer = value;
            return v;
        }
        DBusValue DBusValue::Long(int64_t value)
        {
            DBusValue v;
            v.kind = Kind::Int64;
            v.integer = value;
            return v;
        }
        DBusValue DBusValue::ULong(uint64_t value)
        {
            DBusValue v;
            v.kind = Kind::Uint64;
            v.integer = static_cast<int64_t>(value);
            return v;
        }
        DBusValue DBusValue::Double(double value)
        {
            DBusValue v;
            v.kind = Kind::Double;
            v.real = value;
            return v;
        }
        DBusValue DBusValue::String(const std::string& value)
        {
            DBusValue v;
            v.kind = Kind::String;
            v.text = value;
            return v;
        }
        DBusValue DBusValue::Path(const std::string& value)
        {
            DBusValue v;
            v.kind = Kind::ObjectPath;
            v.text = value;
            return v;
        }
        DBusValue DBusValue::Fd(int descriptor)
        {
            DBusValue v;
            v.kind = Kind::UnixFd;
            v.integer = descriptor;
            return v;
        }
        DBusValue DBusValue::Variant(const DBusValue& inner)
        {
            DBusValue v;
            v.kind = Kind::Variant;
            v.items.push_back(inner);
            return v;
        }
        DBusValue DBusValue::StructOf(std::vector<DBusValue> members)
        {
            DBusValue v;
            v.kind = Kind::Struct;
            v.items = std::move(members);
            return v;
        }
        DBusValue DBusValue::Dict()
        {
            DBusValue v;
            v.kind = Kind::Array;
            v.elementType = "{sv}";
            return v;
        }
        DBusValue& DBusValue::Add(const std::string& key, const DBusValue& value)
        {
            DBusValue entry;
            entry.kind = Kind::DictEntry;
            entry.items.push_back(String(key));
            entry.items.push_back(Variant(value));
            items.push_back(entry);
            return *this;
        }

        std::string DBusValue::Signature() const
        {
            switch (kind)
            {
                case Kind::Boolean: return "b";
                case Kind::Byte: return "y";
                case Kind::Int32: return "i";
                case Kind::Uint32: return "u";
                case Kind::Int64: return "x";
                case Kind::Uint64: return "t";
                case Kind::Double: return "d";
                case Kind::String: return "s";
                case Kind::ObjectPath: return "o";
                case Kind::Signature: return "g";
                case Kind::UnixFd: return "h";
                case Kind::Variant: return "v";
                case Kind::Array: return "a" + elementType;
                case Kind::Struct:
                {
                    std::string s = "(";
                    for (const DBusValue& item : items)
                    {
                        s += item.Signature();
                    }
                    return s + ")";
                }
                case Kind::DictEntry:
                    return "{" + (items.size() == 2 ? items[0].Signature() + items[1].Signature() : std::string()) + "}";
            }
            return std::string();
        }

        const DBusValue* DBusValue::Find(const std::string& key) const
        {
            for (const DBusValue& entry : items)
            {
                if (entry.kind == Kind::DictEntry && entry.items.size() == 2 && entry.items[0].text == key)
                {
                    const DBusValue& value = entry.items[1];
                    return value.kind == Kind::Variant && !value.items.empty() ? &value.items[0] : &value;
                }
            }
            return nullptr;
        }
    } // namespace Server
} // namespace Marconi
