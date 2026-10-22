// Copyright (c) 2026 Red Marmoset Studios Ltd.
// A small HTTPS GET over Socket and TlsStream.

#include <MarconiCore/Net/Https.h>

#include <MarconiCore/Net/Socket.h>
#include <MarconiCore/Net/Tls.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>

namespace Marconi
{
    namespace Net
    {
        namespace
        {
            std::string Lower(std::string text)
            {
                std::transform(text.begin(), text.end(), text.begin(),
                               [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                return text;
            }

            std::string Trim(const std::string& text)
            {
                size_t start = text.find_first_not_of(" \t\r\n");
                size_t end = text.find_last_not_of(" \t\r\n");
                return start == std::string::npos ? "" : text.substr(start, end - start + 1);
            }

            // Joins chunked-encoded `data` back together. False if a chunk size is malformed or the data stops
            // before the last chunk.
            bool Unchunk(const std::string& data, std::string& out, std::string& error)
            {
                size_t p = 0;
                for (;;)
                {
                    size_t lineEnd = data.find("\r\n", p);
                    if (lineEnd == std::string::npos)
                    {
                        error = "the response stopped in the middle of a chunk header";
                        return false;
                    }
                    std::string sizeText = data.substr(p, lineEnd - p);
                    size_t semicolon = sizeText.find(';');
                    if (semicolon != std::string::npos)
                    {
                        sizeText.erase(semicolon);
                    }
                    char* end = nullptr;
                    unsigned long size = std::strtoul(Trim(sizeText).c_str(), &end, 16);
                    if (end == nullptr || *end != '\0' || Trim(sizeText).empty())
                    {
                        error = "a chunk size in the response wasn't a number";
                        return false;
                    }
                    p = lineEnd + 2;
                    if (size == 0)
                    {
                        return true; // the trailer, if any, is ignored
                    }
                    if (p + size + 2 > data.size())
                    {
                        error = "the response stopped in the middle of a chunk";
                        return false;
                    }
                    out.append(data, p, size);
                    p += size + 2;
                }
            }
        } // namespace

        bool ParseHttpResponse(const std::string& raw, HttpResponse& out, std::string& error)
        {
            out = HttpResponse();
            size_t headerEnd = raw.find("\r\n\r\n");
            if (headerEnd == std::string::npos)
            {
                error = "the response had no complete header";
                return false;
            }
            size_t lineEnd = raw.find("\r\n");
            std::string statusLine = raw.substr(0, lineEnd);
            if (statusLine.compare(0, 5, "HTTP/") != 0 || statusLine.size() < 12)
            {
                error = "the response didn't start with an HTTP status line";
                return false;
            }
            size_t space = statusLine.find(' ');
            out.status = space == std::string::npos ? 0 : std::atoi(statusLine.c_str() + space + 1);
            if (out.status < 100 || out.status > 599)
            {
                error = "the response's status code wasn't a number";
                return false;
            }
            size_t p = lineEnd + 2;
            while (p < headerEnd)
            {
                size_t next = raw.find("\r\n", p);
                std::string line = raw.substr(p, next - p);
                size_t colon = line.find(':');
                if (colon != std::string::npos)
                {
                    out.headers[Lower(Trim(line.substr(0, colon)))] = Trim(line.substr(colon + 1));
                }
                p = next + 2;
            }
            std::string body = raw.substr(headerEnd + 4);
            auto transfer = out.headers.find("transfer-encoding");
            if (transfer != out.headers.end() && Lower(transfer->second).find("chunked") != std::string::npos)
            {
                return Unchunk(body, out.body, error);
            }
            auto length = out.headers.find("content-length");
            if (length != out.headers.end())
            {
                size_t expected = static_cast<size_t>(std::strtoul(length->second.c_str(), nullptr, 10));
                if (body.size() < expected)
                {
                    error = "the response stopped before its declared length";
                    return false;
                }
                body.resize(expected);
            }
            out.body = body;
            return true;
        }

        HttpsRequest::HttpsRequest() = default;
        HttpsRequest::~HttpsRequest() = default;

        void HttpsRequest::Cancel()
        {
            m_cancel = true;
            std::lock_guard<std::mutex> lock(m_mutex);
            if (m_socket)
            {
                m_socket->Shutdown();
            }
        }

        bool HttpsRequest::Get(const std::string& host, const std::string& path,
                               const std::map<std::string, std::string>& headers, int timeoutMs, HttpResponse& out,
                               std::string& error)
        {
            Socket* socket = nullptr;
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                m_socket = std::make_unique<Socket>();
                socket = m_socket.get();
            }
            if (!socket->Connect(host, port, timeoutMs, error, &m_cancel))
            {
                return false;
            }
            socket->SetReceiveTimeout(timeoutMs);
            TlsOptions tlsOptions;
            tlsOptions.hostName = host; // no trust decision: only a certificate this computer trusts will do
            std::unique_ptr<TlsStream> tls =
                createTlsStream ? createTlsStream(*socket, tlsOptions) : CreateTlsStream(*socket, tlsOptions);
            if (!tls)
            {
                error = "this build can't make encrypted connections";
                return false;
            }
            if (!tls->Handshake(error))
            {
                return false;
            }
            std::string request = "GET " + path + " HTTP/1.1\r\nHost: " + host + "\r\nConnection: close\r\n";
            for (const auto& header : headers)
            {
                request += header.first + ": " + header.second + "\r\n";
            }
            request += "\r\n";
            if (!tls->Write(request.data(), request.size()))
            {
                error = "the request couldn't be sent";
                return false;
            }
            std::string raw;
            char buffer[4096];
            for (;;)
            {
                ptrdiff_t n = tls->Read(buffer, sizeof(buffer));
                if (n < 0)
                {
                    if (socket->TimedOut())
                    {
                        error = "the server didn't answer in time";
                        return false;
                    }
                    break; // the server closed the connection once it had sent everything
                }
                if (n == 0)
                {
                    break;
                }
                raw.append(buffer, static_cast<size_t>(n));
                if (raw.size() > 8 * 1024 * 1024)
                {
                    error = "the response was too large";
                    return false;
                }
            }
            if (m_cancel)
            {
                error = "cancelled";
                return false;
            }
            return ParseHttpResponse(raw, out, error);
        }
    } // namespace Net
} // namespace Marconi
