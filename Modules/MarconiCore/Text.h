// Copyright (c) 2026 Red Marmoset Studios Ltd.
// Text encoding helpers: RFB uses Latin-1 for passwords and clipboard text, the UI uses UTF-8

#pragma once

#include <string>

namespace Marconi
{
    // Characters outside Latin-1 become '?'. Invalid UTF-8 bytes are passed through as Latin-1.
    std::string Utf8ToLatin1(const std::string& utf8);
    std::string Latin1ToUtf8(const std::string& latin1);

    // "\r\n" -> "\n": RFB clipboard text uses bare newlines.
    std::string ToNetworkLineEndings(const std::string& text);
} // namespace Marconi
