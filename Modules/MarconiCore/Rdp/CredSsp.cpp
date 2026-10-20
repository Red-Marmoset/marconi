// Copyright (c) 2026 Red Marmoset Studios Ltd.
// CredSSP message layer: the ASN.1 DER encoding of TSRequest and the credentials (MS-CSSP 2.2.1).

#include <MarconiCore/Rdp/CredSsp.h>

namespace Marconi
{
    namespace Rdp
    {
        namespace
        {
            // Minimal DER, enough for CredSSP's structures. Lengths use the short form under 128 and the long form
            // above; this writer always emits the shortest valid length.
            enum : uint8_t
            {
                kTagInteger = 0x02,
                kTagOctetString = 0x04,
                kTagSequence = 0x30,
                kContext = 0xA0, // context-specific, constructed: [0] is 0xA0, [1] 0xA1, ...
            };

            void Append(Bytes& a, const Bytes& b) { a.insert(a.end(), b.begin(), b.end()); }

            void PutLength(Bytes& out, size_t length)
            {
                if (length < 0x80)
                {
                    out.push_back(static_cast<uint8_t>(length));
                    return;
                }
                Bytes bytes;
                while (length > 0)
                {
                    bytes.push_back(static_cast<uint8_t>(length & 0xFF));
                    length >>= 8;
                }
                out.push_back(static_cast<uint8_t>(0x80 | bytes.size()));
                for (size_t i = bytes.size(); i > 0; --i)
                {
                    out.push_back(bytes[i - 1]);
                }
            }

            // A complete TLV from a tag and its already-encoded contents.
            Bytes Tlv(uint8_t tag, const Bytes& contents)
            {
                Bytes out;
                out.push_back(tag);
                PutLength(out, contents.size());
                out.insert(out.end(), contents.begin(), contents.end());
                return out;
            }

            Bytes EncodeInteger(uint32_t value)
            {
                Bytes contents;
                if (value == 0)
                {
                    contents.push_back(0);
                }
                else
                {
                    Bytes be;
                    uint32_t v = value;
                    while (v > 0)
                    {
                        be.push_back(static_cast<uint8_t>(v & 0xFF));
                        v >>= 8;
                    }
                    if (be.back() & 0x80)
                    {
                        be.push_back(0); // a leading zero keeps the integer positive
                    }
                    for (size_t i = be.size(); i > 0; --i)
                    {
                        contents.push_back(be[i - 1]);
                    }
                }
                return Tlv(kTagInteger, contents);
            }

            Bytes Context(uint8_t index, const Bytes& inner) { return Tlv(kContext | index, inner); }

            Bytes Utf16Le(const std::string& ascii)
            {
                Bytes out;
                for (char c : ascii)
                {
                    out.push_back(static_cast<uint8_t>(c));
                    out.push_back(0x00);
                }
                return out;
            }

            // A DER reader over a buffer. Each step checks bounds; `ok` turns false on the first malformed byte.
            struct Reader
            {
                const uint8_t* p;
                const uint8_t* end;
                bool ok = true;

                bool ReadTagLength(uint8_t expectedTag, const uint8_t*& contents, size_t& length)
                {
                    if (!ok || p >= end || *p != expectedTag)
                    {
                        return ok = false;
                    }
                    ++p;
                    if (p >= end)
                    {
                        return ok = false;
                    }
                    size_t len = *p++;
                    if (len & 0x80)
                    {
                        size_t count = len & 0x7F;
                        if (count == 0 || count > 4 || static_cast<size_t>(end - p) < count)
                        {
                            return ok = false;
                        }
                        len = 0;
                        for (size_t i = 0; i < count; ++i)
                        {
                            len = (len << 8) | *p++;
                        }
                    }
                    if (static_cast<size_t>(end - p) < len)
                    {
                        return ok = false;
                    }
                    contents = p;
                    length = len;
                    p += len;
                    return true;
                }

                bool Peek(uint8_t tag) const { return ok && p < end && *p == tag; }
            };

            bool ReadInteger(Reader& r, uint32_t& value)
            {
                const uint8_t* contents = nullptr;
                size_t length = 0;
                if (!r.ReadTagLength(kTagInteger, contents, length) || length == 0 || length > 5)
                {
                    return false;
                }
                value = 0;
                for (size_t i = 0; i < length; ++i)
                {
                    value = (value << 8) | contents[i];
                }
                return true;
            }
        } // namespace

        Bytes EncodeTsCredentials(const std::string& domain, const std::string& user, const std::string& password)
        {
            Bytes passwordCreds;
            {
                Bytes inner;
                Append(inner, Context(0, Tlv(kTagOctetString, Utf16Le(domain))));
                Append(inner, Context(1, Tlv(kTagOctetString, Utf16Le(user))));
                Append(inner, Context(2, Tlv(kTagOctetString, Utf16Le(password))));
                passwordCreds = Tlv(kTagSequence, inner);
            }
            Bytes inner;
            Append(inner, Context(0, EncodeInteger(1))); // credType 1 = password
            Append(inner, Context(1, Tlv(kTagOctetString, passwordCreds)));
            return Tlv(kTagSequence, inner);
        }

        Bytes EncodeTsRequest(const TsRequest& request)
        {
            Bytes inner;
            Append(inner, Context(0, EncodeInteger(request.version)));
            if (!request.negoTokens.empty())
            {
                Bytes sequenceOf;
                for (const Bytes& token : request.negoTokens)
                {
                    Bytes negoToken = Context(0, Tlv(kTagOctetString, token)); // negoToken [0] OCTET STRING
                    Append(sequenceOf, Tlv(kTagSequence, negoToken));
                }
                Append(inner, Context(1, Tlv(kTagSequence, sequenceOf)));
            }
            if (!request.authInfo.empty())
            {
                Append(inner, Context(2, Tlv(kTagOctetString, request.authInfo)));
            }
            if (!request.pubKeyAuth.empty())
            {
                Append(inner, Context(3, Tlv(kTagOctetString, request.pubKeyAuth)));
            }
            if (request.hasErrorCode)
            {
                Append(inner, Context(4, EncodeInteger(request.errorCode)));
            }
            if (!request.clientNonce.empty())
            {
                Append(inner, Context(5, Tlv(kTagOctetString, request.clientNonce)));
            }
            return Tlv(kTagSequence, inner);
        }

        bool ParseTsRequest(const Bytes& der, TsRequest& out)
        {
            out = TsRequest();
            Reader top{der.data(), der.data() + der.size()};
            const uint8_t* body = nullptr;
            size_t bodyLength = 0;
            if (!top.ReadTagLength(kTagSequence, body, bodyLength))
            {
                return false;
            }
            Reader r{body, body + bodyLength};

            // version [0] INTEGER, mandatory.
            const uint8_t* ctx = nullptr;
            size_t ctxLength = 0;
            if (!r.ReadTagLength(kContext | 0, ctx, ctxLength))
            {
                return false;
            }
            Reader versionReader{ctx, ctx + ctxLength};
            if (!ReadInteger(versionReader, out.version))
            {
                return false;
            }

            if (r.Peek(kContext | 1)) // negoTokens
            {
                r.ReadTagLength(kContext | 1, ctx, ctxLength);
                Reader seqOf{ctx, ctx + ctxLength};
                const uint8_t* seq = nullptr;
                size_t seqLength = 0;
                if (!seqOf.ReadTagLength(kTagSequence, seq, seqLength))
                {
                    return false;
                }
                Reader items{seq, seq + seqLength};
                while (items.ok && items.p < items.end)
                {
                    const uint8_t* item = nullptr;
                    size_t itemLength = 0;
                    if (!items.ReadTagLength(kTagSequence, item, itemLength))
                    {
                        return false;
                    }
                    Reader one{item, item + itemLength};
                    const uint8_t* tokenCtx = nullptr;
                    size_t tokenCtxLength = 0;
                    if (!one.ReadTagLength(kContext | 0, tokenCtx, tokenCtxLength))
                    {
                        return false;
                    }
                    Reader octet{tokenCtx, tokenCtx + tokenCtxLength};
                    const uint8_t* token = nullptr;
                    size_t tokenLength = 0;
                    if (!octet.ReadTagLength(kTagOctetString, token, tokenLength))
                    {
                        return false;
                    }
                    out.negoTokens.emplace_back(token, token + tokenLength);
                }
            }

            auto readOctetField = [&](uint8_t index, Bytes& into) -> bool {
                if (!r.Peek(kContext | index))
                {
                    return true;
                }
                r.ReadTagLength(kContext | index, ctx, ctxLength);
                Reader octet{ctx, ctx + ctxLength};
                const uint8_t* data = nullptr;
                size_t length = 0;
                if (!octet.ReadTagLength(kTagOctetString, data, length))
                {
                    return false;
                }
                into.assign(data, data + length);
                return true;
            };
            if (!readOctetField(2, out.authInfo) || !readOctetField(3, out.pubKeyAuth))
            {
                return false;
            }
            if (r.Peek(kContext | 4))
            {
                r.ReadTagLength(kContext | 4, ctx, ctxLength);
                Reader errorReader{ctx, ctx + ctxLength};
                if (!ReadInteger(errorReader, out.errorCode))
                {
                    return false;
                }
                out.hasErrorCode = true;
            }
            if (!readOctetField(5, out.clientNonce))
            {
                return false;
            }
            return r.ok;
        }
    } // namespace Rdp
} // namespace Marconi
