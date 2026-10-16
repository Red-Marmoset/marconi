// Copyright (c) 2026 Red Marmoset Studios Ltd.
// Reference implementation of Marconi::Net::TlsStream on OpenSSL. Not part of the build: it shows, and has been
// tested to meet, the behaviour Net/Tls.h asks for, so the real implementation (in the engine's Crypto module, on
// Schannel, Security.framework and OpenSSL) can follow it.
//
// How it works: OpenSSL runs on a pair of memory BIOs rather than on the socket, so this code moves the encrypted
// bytes itself. Every call into OpenSSL happens under one mutex, but no thread waits for the network while holding
// it: a reader that needs more bytes releases the mutex, blocks in Socket::Receive, then takes it again to feed
// OpenSSL. That is what lets the UI thread Write while the connection thread is blocked in Read.
//
// Trust: the handshake completes even if the certificate isn't trusted (verification only records the problem), and
// the decision is made before any application data flows: a certificate this computer trusts whose name matches is
// accepted; otherwise TlsOptions::trustDecision is asked, and a refusal closes the connection. Anonymous TLS (no
// certificate, for VeNCrypt's TLS* subtypes) needs TLS 1.2 with the anonymous Diffie-Hellman suites OpenSSL only
// allows at security level 0; Schannel has no such suites, so on Windows only the X509* subtypes can work.
//
// Build (Linux), for the end-to-end check in Tools/fake_vencrypt_server.py:
//   g++ -std=c++17 -IModules ... Tools/TlsStream_OpenSSL.cpp -lssl -lcrypto

#include <MarconiCore/Net/Tls.h>

#include <openssl/bio.h>
#include <openssl/err.h>
#include <openssl/ssl.h>
#include <openssl/x509.h>
#include <openssl/x509v3.h>

#include <algorithm>
#include <cstring>
#include <mutex>

namespace Marconi
{
    namespace Net
    {
        namespace
        {
            std::string LastOpenSslError()
            {
                char buffer[256];
                unsigned long code = ERR_get_error();
                if (code == 0)
                {
                    return "unknown error";
                }
                ERR_error_string_n(code, buffer, sizeof(buffer));
                ERR_clear_error();
                return buffer;
            }

            std::string BioToString(BIO* bio)
            {
                char* data = nullptr;
                long length = BIO_get_mem_data(bio, &data);
                return std::string(data, data + std::max<long>(length, 0));
            }

            std::string NameToString(X509_NAME* name)
            {
                BIO* bio = BIO_new(BIO_s_mem());
                X509_NAME_print_ex(bio, name, 0, XN_FLAG_RFC2253);
                std::string text = BioToString(bio);
                BIO_free(bio);
                return text;
            }

            std::string TimeToString(const ASN1_TIME* time)
            {
                BIO* bio = BIO_new(BIO_s_mem());
                ASN1_TIME_print(bio, time);
                std::string text = BioToString(bio);
                BIO_free(bio);
                return text;
            }

            TlsCertificate DescribeCertificate(X509* certificate, long verifyError)
            {
                TlsCertificate description;
                unsigned int length = 0;
                unsigned char digest[EVP_MAX_MD_SIZE];
                if (X509_digest(certificate, EVP_sha256(), digest, &length) == 1)
                {
                    description.sha256.assign(digest, digest + length);
                }
                description.subject = NameToString(X509_get_subject_name(certificate));
                description.issuer = NameToString(X509_get_issuer_name(certificate));
                description.validUntil = TimeToString(X509_get0_notAfter(certificate));
                description.problem = X509_verify_cert_error_string(verifyError);
                return description;
            }

            class OpenSslTlsStream : public TlsStream
            {
            public:
                OpenSslTlsStream(Socket& socket, TlsOptions options)
                    : m_socket(socket), m_options(std::move(options))
                {
                }

                ~OpenSslTlsStream() override
                {
                    if (m_ssl != nullptr)
                    {
                        SSL_free(m_ssl); // frees the BIOs too
                    }
                    if (m_context != nullptr)
                    {
                        SSL_CTX_free(m_context);
                    }
                }

                bool Handshake(std::string& error) override
                {
                    m_context = SSL_CTX_new(TLS_client_method());
                    if (m_context == nullptr)
                    {
                        error = LastOpenSslError();
                        return false;
                    }
                    SSL_CTX_set_min_proto_version(m_context, TLS1_2_VERSION);
                    SSL_CTX_set_options(m_context, SSL_OP_NO_RENEGOTIATION | SSL_OP_NO_COMPRESSION);
                    if (m_options.anonymous)
                    {
                        // Anonymous Diffie-Hellman: TLS 1.2 only, and only at security level 0.
                        SSL_CTX_set_max_proto_version(m_context, TLS1_2_VERSION);
                        SSL_CTX_set_security_level(m_context, 0);
                        if (SSL_CTX_set_cipher_list(m_context, "aNULL:@SECLEVEL=0") != 1)
                        {
                            error = "anonymous TLS isn't available: " + LastOpenSslError();
                            return false;
                        }
                        SSL_CTX_set_verify(m_context, SSL_VERIFY_NONE, nullptr);
                    }
                    else
                    {
                        SSL_CTX_set_default_verify_paths(m_context);
                        // Record problems instead of failing the handshake: the user decides afterwards.
                        SSL_CTX_set_verify(m_context, SSL_VERIFY_PEER, [](int, X509_STORE_CTX*) { return 1; });
                    }

                    m_ssl = SSL_new(m_context);
                    m_in = BIO_new(BIO_s_mem());  // bytes from the network, for OpenSSL to read
                    m_out = BIO_new(BIO_s_mem()); // bytes OpenSSL produced, to send
                    if (m_ssl == nullptr || m_in == nullptr || m_out == nullptr)
                    {
                        error = LastOpenSslError();
                        return false;
                    }
                    SSL_set_bio(m_ssl, m_in, m_out);
                    SSL_set_connect_state(m_ssl);
                    if (!m_options.anonymous && !m_options.hostName.empty())
                    {
                        SSL_set1_host(m_ssl, m_options.hostName.c_str());
                        SSL_set_tlsext_host_name(m_ssl, m_options.hostName.c_str());
                    }

                    std::unique_lock<std::mutex> lock(m_mutex);
                    for (;;)
                    {
                        int result = SSL_do_handshake(m_ssl);
                        int code = SSL_get_error(m_ssl, result);
                        if (!Flush(lock))
                        {
                            error = "connection lost during the TLS handshake";
                            return false;
                        }
                        if (result == 1)
                        {
                            break;
                        }
                        if (code != SSL_ERROR_WANT_READ)
                        {
                            error = "TLS handshake failed: " + LastOpenSslError();
                            return false;
                        }
                        if (!FeedFromSocket(lock))
                        {
                            error = "the server closed the connection during the TLS handshake";
                            return false;
                        }
                    }

                    if (!m_options.anonymous)
                    {
                        long verifyResult = SSL_get_verify_result(m_ssl);
                        X509* certificate = SSL_get1_peer_certificate(m_ssl);
                        if (certificate == nullptr)
                        {
                            error = "the server sent no certificate";
                            return false;
                        }
                        bool trusted = verifyResult == X509_V_OK;
                        if (!trusted)
                        {
                            TlsCertificate description = DescribeCertificate(certificate, verifyResult);
                            lock.unlock(); // the decision may take a while (a dialog); nothing else runs meanwhile
                            trusted = m_options.trustDecision && m_options.trustDecision(description);
                            lock.lock();
                        }
                        X509_free(certificate);
                        if (!trusted)
                        {
                            SSL_shutdown(m_ssl);
                            Flush(lock);
                            error = "the server's certificate was not accepted";
                            return false;
                        }
                    }
                    return true;
                }

                ptrdiff_t Read(void* buffer, size_t length) override
                {
                    std::unique_lock<std::mutex> lock(m_mutex);
                    for (;;)
                    {
                        int got = SSL_read(m_ssl, buffer, static_cast<int>(std::min<size_t>(length, INT_MAX)));
                        int code = SSL_get_error(m_ssl, got);
                        Flush(lock); // SSL_read can produce output (key updates, alerts)
                        if (got > 0)
                        {
                            return got;
                        }
                        if (code == SSL_ERROR_ZERO_RETURN)
                        {
                            return 0; // clean TLS close
                        }
                        if (code != SSL_ERROR_WANT_READ)
                        {
                            return -1;
                        }
                        if (!FeedFromSocket(lock))
                        {
                            return 0; // the socket closed or was shut down
                        }
                    }
                }

                bool Write(const void* data, size_t length) override
                {
                    std::lock_guard<std::mutex> lock(m_mutex);
                    const uint8_t* bytes = static_cast<const uint8_t*>(data);
                    while (length > 0)
                    {
                        int written = SSL_write(m_ssl, bytes, static_cast<int>(std::min<size_t>(length, INT_MAX)));
                        if (written <= 0)
                        {
                            return false; // with renegotiation off, SSL_write never needs to read first
                        }
                        bytes += written;
                        length -= static_cast<size_t>(written);
                        if (!FlushLocked())
                        {
                            return false;
                        }
                    }
                    return true;
                }

                void Shutdown() override
                {
                    std::unique_lock<std::mutex> lock(m_mutex);
                    if (m_ssl != nullptr)
                    {
                        SSL_shutdown(m_ssl);
                        Flush(lock);
                    }
                }

                std::string GetProtocolName() const override
                {
                    std::lock_guard<std::mutex> lock(m_mutex);
                    std::string version = m_ssl != nullptr ? SSL_get_version(m_ssl) : "";
                    if (version.compare(0, 4, "TLSv") == 0)
                    {
                        version = "TLS " + version.substr(4); // "TLSv1.3" -> "TLS 1.3"
                    }
                    return version;
                }

            private:
                // Sends whatever OpenSSL has queued. The mutex stays held: SendAll on a blocking socket doesn't wait
                // for the peer beyond the kernel's send buffer, and interleaving another thread's output here would
                // corrupt the stream.
                bool FlushLocked()
                {
                    char buffer[16 * 1024];
                    for (;;)
                    {
                        int pending = BIO_read(m_out, buffer, sizeof(buffer));
                        if (pending <= 0)
                        {
                            return true;
                        }
                        if (!m_socket.SendAll(buffer, static_cast<size_t>(pending)))
                        {
                            return false;
                        }
                    }
                }

                bool Flush(std::unique_lock<std::mutex>&)
                {
                    return FlushLocked();
                }

                // Waits for bytes from the network with the mutex released, then hands them to OpenSSL. False at end
                // of stream or on error.
                bool FeedFromSocket(std::unique_lock<std::mutex>& lock)
                {
                    char buffer[16 * 1024];
                    lock.unlock();
                    ptrdiff_t got = m_socket.Receive(buffer, sizeof(buffer));
                    lock.lock();
                    if (got <= 0)
                    {
                        return false;
                    }
                    return BIO_write(m_in, buffer, static_cast<int>(got)) == got;
                }

                Socket& m_socket;
                TlsOptions m_options;
                mutable std::mutex m_mutex;
                SSL_CTX* m_context = nullptr;
                SSL* m_ssl = nullptr;
                BIO* m_in = nullptr;
                BIO* m_out = nullptr;
            };
        } // namespace

        // What Net/Tls.cpp would contain once this is the implementation in use.
        bool IsTlsAvailable()
        {
            return true;
        }

        std::unique_ptr<TlsStream> CreateTlsStream(Socket& socket, const TlsOptions& options)
        {
            return std::make_unique<OpenSslTlsStream>(socket, options);
        }
    } // namespace Net
} // namespace Marconi
