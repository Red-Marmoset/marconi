// Copyright (c) 2026 Red Marmoset Studios Ltd.
// Reading an access password from a file the installer left

#include "PasswordFile.h"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <system_error>
#include <vector>

namespace Marconi
{
    namespace Server
    {
        namespace
        {
            void AppendUtf8(std::string& out, uint32_t codePoint)
            {
                if (codePoint < 0x80)
                {
                    out += static_cast<char>(codePoint);
                }
                else if (codePoint < 0x800)
                {
                    out += static_cast<char>(0xC0 | (codePoint >> 6));
                    out += static_cast<char>(0x80 | (codePoint & 0x3F));
                }
                else if (codePoint < 0x10000)
                {
                    out += static_cast<char>(0xE0 | (codePoint >> 12));
                    out += static_cast<char>(0x80 | ((codePoint >> 6) & 0x3F));
                    out += static_cast<char>(0x80 | (codePoint & 0x3F));
                }
                else
                {
                    out += static_cast<char>(0xF0 | (codePoint >> 18));
                    out += static_cast<char>(0x80 | ((codePoint >> 12) & 0x3F));
                    out += static_cast<char>(0x80 | ((codePoint >> 6) & 0x3F));
                    out += static_cast<char>(0x80 | (codePoint & 0x3F));
                }
            }

            // UTF-16 little-endian (after the mark) to UTF-8. False on a lone surrogate or an odd byte count.
            bool Utf16ToUtf8(const std::vector<unsigned char>& bytes, size_t start, std::string& out)
            {
                if ((bytes.size() - start) % 2 != 0)
                {
                    return false;
                }
                for (size_t i = start; i < bytes.size(); i += 2)
                {
                    uint32_t unit = static_cast<uint32_t>(bytes[i]) | (static_cast<uint32_t>(bytes[i + 1]) << 8);
                    if (unit >= 0xD800 && unit <= 0xDBFF)
                    {
                        if (i + 3 >= bytes.size())
                        {
                            return false;
                        }
                        uint32_t low = static_cast<uint32_t>(bytes[i + 2]) | (static_cast<uint32_t>(bytes[i + 3]) << 8);
                        if (low < 0xDC00 || low > 0xDFFF)
                        {
                            return false;
                        }
                        AppendUtf8(out, 0x10000 + ((unit - 0xD800) << 10) + (low - 0xDC00));
                        i += 2;
                    }
                    else if (unit >= 0xDC00 && unit <= 0xDFFF)
                    {
                        return false;
                    }
                    else
                    {
                        AppendUtf8(out, unit);
                    }
                }
                return true;
            }
        } // namespace

        bool ReadPasswordFile(const std::string& path, std::string& passwordOut, std::string& error)
        {
            passwordOut.clear();
            std::ifstream file(path, std::ios::binary);
            if (!file)
            {
                error = "the password file could not be opened";
                return false;
            }
            std::vector<unsigned char> bytes((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
            if (bytes.size() > 4096)
            {
                error = "the password file is too big to be a password";
                return false;
            }

            std::string text;
            if (bytes.size() >= 2 && bytes[0] == 0xFF && bytes[1] == 0xFE)
            {
                if (!Utf16ToUtf8(bytes, 2, text))
                {
                    error = "the password file is not valid UTF-16 text";
                    return false;
                }
            }
            else
            {
                size_t start = (bytes.size() >= 3 && bytes[0] == 0xEF && bytes[1] == 0xBB && bytes[2] == 0xBF) ? 3 : 0;
                text.assign(bytes.begin() + static_cast<std::ptrdiff_t>(start), bytes.end());
            }

            if (text.size() >= 2 && text.compare(text.size() - 2, 2, "\r\n") == 0)
            {
                text.resize(text.size() - 2);
            }
            else if (!text.empty() && (text.back() == '\n' || text.back() == '\r'))
            {
                text.pop_back();
            }
            if (text.empty())
            {
                error = "the password file is empty";
                return false;
            }
            passwordOut = text;
            return true;
        }

        bool ErasePasswordFile(const std::string& path)
        {
            std::error_code ec;
            uintmax_t size = std::filesystem::file_size(path, ec);
            if (!ec && size > 0)
            {
                std::fstream file(path, std::ios::in | std::ios::out | std::ios::binary);
                if (file)
                {
                    std::vector<char> zeros(static_cast<size_t>(size), 0);
                    file.write(zeros.data(), static_cast<std::streamsize>(zeros.size()));
                    file.flush();
                }
            }
            return std::filesystem::remove(path, ec) && !ec;
        }
    } // namespace Server
} // namespace Marconi
