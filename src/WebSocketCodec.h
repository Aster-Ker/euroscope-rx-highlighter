#pragma once
// Minimal RFC 6455 client-side framing (portable, unit tested).

#include <cstdint>
#include <string>

namespace RxHighlight {

enum class WsOpcode : std::uint8_t
{
    Continuation = 0x0,
    Text = 0x1,
    Binary = 0x2,
    Close = 0x8,
    Ping = 0x9,
    Pong = 0xA,
};

struct WsFrame
{
    bool fin = true;
    WsOpcode opcode = WsOpcode::Text;
    std::string payload;
};

// Accumulates bytes received from the server and splits them into frames.
class WsReader
{
public:
    void Feed(const char* data, std::size_t size) { buffer_.append(data, size); }

    // Returns true and fills `frame` when a full frame is available.
    // Sets `error` if the stream is invalid (e.g. absurd length).
    bool Next(WsFrame& frame, bool& error);

private:
    std::string buffer_;
};

// Encodes a masked client->server frame (clients must mask).
std::string EncodeClientFrame(WsOpcode opcode, const std::string& payload, std::uint32_t maskKey);

// Builds the HTTP upgrade request.
std::string BuildUpgradeRequest(const std::string& host, std::uint16_t port, const std::string& path, const std::string& key);

// Splits a raw HTTP response into status code and decoded body
// (handles Content-Length, chunked and read-until-close bodies).
bool ParseHttpResponse(const std::string& raw, int& status, std::string& body);

} // namespace RxHighlight
