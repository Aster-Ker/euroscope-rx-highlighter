#include "WebSocketCodec.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>

namespace RxHighlight {

namespace {
constexpr std::uint64_t kMaxFrame = 4 * 1024 * 1024;

std::string Lower(std::string s)
{
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}
} // namespace

bool WsReader::Next(WsFrame& frame, bool& error)
{
    error = false;
    if (buffer_.size() < 2) {
        return false;
    }
    const auto* b = reinterpret_cast<const unsigned char*>(buffer_.data());
    const bool fin = (b[0] & 0x80) != 0;
    const auto opcode = static_cast<WsOpcode>(b[0] & 0x0F);
    const bool masked = (b[1] & 0x80) != 0;
    std::uint64_t length = b[1] & 0x7F;
    std::size_t offset = 2;

    if (length == 126) {
        if (buffer_.size() < 4) {
            return false;
        }
        length = (static_cast<std::uint64_t>(b[2]) << 8) | b[3];
        offset = 4;
    } else if (length == 127) {
        if (buffer_.size() < 10) {
            return false;
        }
        length = 0;
        for (int i = 0; i < 8; ++i) {
            length = (length << 8) | b[2 + i];
        }
        offset = 10;
    }
    if (length > kMaxFrame) {
        error = true;
        return false;
    }

    unsigned char mask[4] = {0, 0, 0, 0};
    if (masked) {
        if (buffer_.size() < offset + 4) {
            return false;
        }
        std::copy(b + offset, b + offset + 4, mask);
        offset += 4;
    }
    if (buffer_.size() < offset + length) {
        return false;
    }

    frame.fin = fin;
    frame.opcode = opcode;
    frame.payload.assign(buffer_, offset, static_cast<std::size_t>(length));
    if (masked) {
        for (std::size_t i = 0; i < frame.payload.size(); ++i) {
            frame.payload[i] = static_cast<char>(frame.payload[i] ^ mask[i % 4]);
        }
    }
    buffer_.erase(0, offset + static_cast<std::size_t>(length));
    return true;
}

std::string EncodeClientFrame(WsOpcode opcode, const std::string& payload, std::uint32_t maskKey)
{
    std::string out;
    out.push_back(static_cast<char>(0x80 | static_cast<std::uint8_t>(opcode)));
    const std::uint64_t n = payload.size();
    if (n < 126) {
        out.push_back(static_cast<char>(0x80 | n));
    } else if (n <= 0xFFFF) {
        out.push_back(static_cast<char>(0x80 | 126));
        out.push_back(static_cast<char>((n >> 8) & 0xFF));
        out.push_back(static_cast<char>(n & 0xFF));
    } else {
        out.push_back(static_cast<char>(0x80 | 127));
        for (int i = 7; i >= 0; --i) {
            out.push_back(static_cast<char>((n >> (8 * i)) & 0xFF));
        }
    }
    const unsigned char mask[4] = {
        static_cast<unsigned char>(maskKey >> 24),
        static_cast<unsigned char>(maskKey >> 16),
        static_cast<unsigned char>(maskKey >> 8),
        static_cast<unsigned char>(maskKey)};
    out.append(reinterpret_cast<const char*>(mask), 4);
    for (std::size_t i = 0; i < payload.size(); ++i) {
        out.push_back(static_cast<char>(payload[i] ^ mask[i % 4]));
    }
    return out;
}

std::string BuildUpgradeRequest(const std::string& host, std::uint16_t port, const std::string& path, const std::string& key)
{
    return "GET " + path + " HTTP/1.1\r\n"
           "Host: " + host + ":" + std::to_string(port) + "\r\n"
           "Upgrade: websocket\r\n"
           "Connection: Upgrade\r\n"
           "Sec-WebSocket-Key: " + key + "\r\n"
           "Sec-WebSocket-Version: 13\r\n"
           "User-Agent: RxHighlighter-EuroScope\r\n"
           "\r\n";
}

bool ParseHttpResponse(const std::string& raw, int& status, std::string& body)
{
    const std::size_t headerEnd = raw.find("\r\n\r\n");
    if (headerEnd == std::string::npos || raw.compare(0, 5, "HTTP/") != 0) {
        return false;
    }
    const std::size_t space = raw.find(' ');
    if (space == std::string::npos || space > headerEnd) {
        return false;
    }
    status = std::atoi(raw.c_str() + space + 1);

    const std::string headers = Lower(raw.substr(0, headerEnd));
    std::string rest = raw.substr(headerEnd + 4);

    if (headers.find("transfer-encoding: chunked") != std::string::npos) {
        body.clear();
        std::size_t pos = 0;
        for (;;) {
            const std::size_t lineEnd = rest.find("\r\n", pos);
            if (lineEnd == std::string::npos) {
                return false;
            }
            const unsigned long size = std::strtoul(rest.c_str() + pos, nullptr, 16);
            pos = lineEnd + 2;
            if (size == 0) {
                return true;
            }
            if (pos + size > rest.size()) {
                return false;
            }
            body.append(rest, pos, size);
            pos += size + 2;
        }
    }

    const std::size_t cl = headers.find("content-length:");
    if (cl != std::string::npos) {
        const unsigned long len = std::strtoul(headers.c_str() + cl + 15, nullptr, 10);
        if (rest.size() < len) {
            return false;
        }
        rest.resize(len);
    }
    body = rest;
    return true;
}

} // namespace RxHighlight
