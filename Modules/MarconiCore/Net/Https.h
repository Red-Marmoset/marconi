// Copyright (c) 2026 Red Marmoset Studios Ltd.
// A small HTTPS GET over Socket and TlsStream, enough to ask GitHub's API a question: no proxies, no redirects, no
// compression. The certificate must be one this computer trusts.

#pragma once

#include <MarconiCore/Net/Tls.h>

#include <atomic>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>

namespace Marconi
{
    namespace Net
    {
        class Socket;

        struct HttpResponse
        {
            int status = 0; // e.g. 200; 0 when no response was read
            std::map<std::string, std::string> headers; // names lower-cased
            std::string body;
        };

        // Parses an HTTP/1.1 response (status line, headers, body with Content-Length, chunked, or to the end).
        // False with `error` if it doesn't look like one or is truncated.
        bool ParseHttpResponse(const std::string& raw, HttpResponse& out, std::string& error);

        class HttpsRequest
        {
        public:
            HttpsRequest();
            ~HttpsRequest();

            // Makes the encrypted stream. Empty means Net::CreateTlsStream; tests substitute their own.
            std::function<std::unique_ptr<TlsStream>(Socket&, const TlsOptions&)> createTlsStream;
            uint16_t port = 443;

            // GET https://`host``path`, with `headers` added to the request. Blocks; false with `error` on a
            // connection, TLS or protocol problem (an HTTP error status is a successful call: check out.status).
            bool Get(const std::string& host, const std::string& path, const std::map<std::string, std::string>& headers,
                     int timeoutMs, HttpResponse& out, std::string& error);

            // Makes a blocked Get fail promptly. Any thread.
            void Cancel();

        private:
            std::mutex m_mutex;
            std::unique_ptr<Socket> m_socket; // guarded by m_mutex
            std::atomic<bool> m_cancel{false};
        };
    } // namespace Net
} // namespace Marconi
