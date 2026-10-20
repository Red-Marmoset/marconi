// Copyright (c) 2026 Red Marmoset Studios Ltd.
// A stateful RC4 keystream, for NTLM's sealing where several messages share one continuous keystream (the engine's
// Crypto::Rc4 re-keys each call, which is right for one-shot use but not for this).

#pragma once

#include <cstdint>
#include <vector>

namespace Marconi
{
    namespace Rdp
    {
        class Rc4State
        {
        public:
            explicit Rc4State(const std::vector<uint8_t>& key)
            {
                for (int i = 0; i < 256; ++i)
                {
                    m_s[i] = static_cast<uint8_t>(i);
                }
                size_t keyLength = key.empty() ? 1 : key.size();
                int j = 0;
                for (int i = 0; i < 256; ++i)
                {
                    j = (j + m_s[i] + (key.empty() ? 0 : key[i % keyLength])) & 0xFF;
                    std::swap(m_s[i], m_s[j]);
                }
            }

            std::vector<uint8_t> Process(const std::vector<uint8_t>& data)
            {
                std::vector<uint8_t> out(data.size());
                for (size_t n = 0; n < data.size(); ++n)
                {
                    m_i = (m_i + 1) & 0xFF;
                    m_j = (m_j + m_s[m_i]) & 0xFF;
                    std::swap(m_s[m_i], m_s[m_j]);
                    out[n] = data[n] ^ m_s[(m_s[m_i] + m_s[m_j]) & 0xFF];
                }
                return out;
            }

        private:
            uint8_t m_s[256];
            int m_i = 0;
            int m_j = 0;
        };
    } // namespace Rdp
} // namespace Marconi
