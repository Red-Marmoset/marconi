// Copyright (c) 2026 Red Marmoset Studios Ltd.
// Encrypted (TLS) connection over a Socket: the engine's Crypto module does the TLS, this adapts it to a Socket

#include <MarconiCore/Net/Tls.h>

#include <Crypto/Tls.h>

namespace Marconi
{
    namespace Net
    {
        namespace
        {
            class EngineTlsStream : public TlsStream
            {
            public:
                EngineTlsStream(const ByteLink& link, const TlsOptions& options)
                {
                    Crypto::TlsClientOptions engineOptions;
                    engineOptions.hostName = options.hostName;
                    engineOptions.anonymous = options.anonymous;
                    if (options.trustDecision)
                    {
                        std::function<bool(const TlsCertificate&)> decision = options.trustDecision;
                        engineOptions.trustDecision = [decision](const Crypto::TlsCertificateInfo& info) {
                            TlsCertificate certificate;
                            certificate.sha256 = info.sha256;
                            certificate.subject = info.subject;
                            certificate.issuer = info.issuer;
                            certificate.validUntil = info.validUntil;
                            certificate.problem = info.problem;
                            return decision(certificate);
                        };
                    }
                    engineOptions.receive = link.receive;
                    engineOptions.sendAll = link.sendAll;
                    m_client = Crypto::TlsClient::Create(std::move(engineOptions));
                }

                bool Handshake(std::string& error) override
                {
                    if (!m_client)
                    {
                        error = "this build can't make encrypted connections";
                        return false;
                    }
                    return m_client->Handshake(error);
                }

                ptrdiff_t Read(void* buffer, size_t length) override { return m_client ? m_client->Read(buffer, length) : -1; }
                bool Write(const void* data, size_t length) override { return m_client && m_client->Write(data, length); }

                void Shutdown() override
                {
                    if (m_client)
                    {
                        m_client->Shutdown();
                    }
                }

                std::string GetProtocolName() const override { return m_client ? m_client->GetProtocolName() : ""; }

            private:
                std::unique_ptr<Crypto::TlsClient> m_client;
            };
        } // namespace

        bool IsTlsAvailable()
        {
            return Crypto::TlsClient::IsAvailable();
        }

        bool IsAnonymousTlsAvailable()
        {
            return Crypto::TlsClient::IsAnonymousAvailable();
        }

        std::unique_ptr<TlsStream> CreateTlsStream(Socket& socket, const TlsOptions& options)
        {
            ByteLink link;
            link.receive = [&socket](void* buffer, size_t length) { return socket.Receive(buffer, length); };
            link.sendAll = [&socket](const void* data, size_t length) { return socket.SendAll(data, length); };
            return CreateTlsStream(link, options);
        }

        std::unique_ptr<TlsStream> CreateTlsStream(const ByteLink& link, const TlsOptions& options)
        {
            if (!IsTlsAvailable())
            {
                return nullptr;
            }
            return std::make_unique<EngineTlsStream>(link, options);
        }
    } // namespace Net
} // namespace Marconi
