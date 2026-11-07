// winsock2.h must come before windows.h
#include <winsock2.h>
#include <ws2tcpip.h>

#include "VoiceClient.h"

#include <chrono>
#include <random>
#include <vector>

#include "VoiceMessage.h"
#include "WebSocketCodec.h"

namespace RxHighlight {

namespace {

constexpr const char* kHost = "127.0.0.1";
constexpr int kReconnectDelayMs = 3000;
constexpr int kPollIntervalMs = 200;
constexpr int kConnectTimeoutMs = 1000;
constexpr int kHttpTimeoutMs = 1000;
constexpr int kSliceMs = 200; // longest a single wait can block Stop()

std::string Base64(const unsigned char* data, std::size_t size)
{
    static const char* table = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    for (std::size_t i = 0; i < size; i += 3) {
        std::uint32_t n = static_cast<std::uint32_t>(data[i]) << 16;
        if (i + 1 < size) n |= static_cast<std::uint32_t>(data[i + 1]) << 8;
        if (i + 2 < size) n |= data[i + 2];
        out.push_back(table[(n >> 18) & 63]);
        out.push_back(table[(n >> 12) & 63]);
        out.push_back(i + 1 < size ? table[(n >> 6) & 63] : '=');
        out.push_back(i + 2 < size ? table[n & 63] : '=');
    }
    return out;
}

std::mt19937& Rng()
{
    thread_local std::mt19937 rng{std::random_device{}()};
    return rng;
}

} // namespace

VoiceClient::VoiceClient(RxState& state, std::uint16_t port, std::function<void()> onChange)
    : state_(state), port_(port), onChange_(std::move(onChange))
{
}

VoiceClient::~VoiceClient()
{
    Stop();
}

void VoiceClient::Start()
{
    if (running_.exchange(true)) {
        return;
    }
    thread_ = std::thread(&VoiceClient::Run, this);
}

void VoiceClient::Stop()
{
    {
        std::lock_guard<std::mutex> lock(sleepMutex_);
        running_ = false;
    }
    sleepCv_.notify_all();
    if (thread_.joinable()) {
        thread_.join();
    }
    status_ = ConnectionStatus::Disconnected;
}

void VoiceClient::InterruptibleSleep(int ms)
{
    std::unique_lock<std::mutex> lock(sleepMutex_);
    sleepCv_.wait_for(lock, std::chrono::milliseconds(ms), [this] { return !running_.load(); });
}

void VoiceClient::Run()
{
    WSADATA wsa{};
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        return;
    }

    while (running_) {
        if (!RunWebSocket() && running_) {
            RunHttpPolling();
        }

        state_.Clear();
        status_ = ConnectionStatus::Disconnected;
        Notify();

        if (running_) {
            InterruptibleSleep(kReconnectDelayMs);
        }
    }

    WSACleanup();
}

bool VoiceClient::Connect(Socket& out)
{
    SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == INVALID_SOCKET) {
        return false;
    }
    out = static_cast<Socket>(s);

    u_long nonBlocking = 1;
    ioctlsocket(s, FIONBIO, &nonBlocking);

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port_);
    inet_pton(AF_INET, kHost, &addr.sin_addr);

    if (connect(s, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == SOCKET_ERROR &&
        WSAGetLastError() != WSAEWOULDBLOCK) {
        CloseSocket(out);
        return false;
    }

    for (int waited = 0; running_ && waited < kConnectTimeoutMs; waited += kSliceMs) {
        fd_set writeSet;
        fd_set errorSet;
        FD_ZERO(&writeSet);
        FD_ZERO(&errorSet);
        FD_SET(s, &writeSet);
        FD_SET(s, &errorSet);
        timeval tv{0, kSliceMs * 1000};
        const int r = select(0, nullptr, &writeSet, &errorSet, &tv);
        if (r == SOCKET_ERROR || FD_ISSET(s, &errorSet)) {
            break;
        }
        if (r > 0 && FD_ISSET(s, &writeSet)) {
            int err = 0;
            int len = sizeof(err);
            getsockopt(s, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&err), &len);
            if (err != 0) {
                break;
            }
            BOOL noDelay = TRUE;
            setsockopt(s, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&noDelay), sizeof(noDelay));
            return true;
        }
    }
    CloseSocket(out);
    return false;
}

bool VoiceClient::SendAll(Socket sock, const std::string& data)
{
    const SOCKET s = static_cast<SOCKET>(sock);
    std::size_t sent = 0;
    int waited = 0;
    while (sent < data.size() && running_) {
        const int n = send(s, data.data() + sent, static_cast<int>(data.size() - sent), 0);
        if (n > 0) {
            sent += static_cast<std::size_t>(n);
            continue;
        }
        if (n == SOCKET_ERROR && WSAGetLastError() == WSAEWOULDBLOCK && waited < kHttpTimeoutMs) {
            fd_set writeSet;
            FD_ZERO(&writeSet);
            FD_SET(s, &writeSet);
            timeval tv{0, kSliceMs * 1000};
            select(0, nullptr, &writeSet, nullptr, &tv);
            waited += kSliceMs;
            continue;
        }
        return false;
    }
    return sent == data.size();
}

int VoiceClient::Receive(Socket sock, char* buffer, int size, int timeoutMs)
{
    const SOCKET s = static_cast<SOCKET>(sock);
    int waited = 0;
    do {
        if (!running_) {
            return -1;
        }
        fd_set readSet;
        FD_ZERO(&readSet);
        FD_SET(s, &readSet);
        timeval tv{0, kSliceMs * 1000};
        const int r = select(0, &readSet, nullptr, nullptr, &tv);
        if (r == SOCKET_ERROR) {
            return -1;
        }
        if (r > 0) {
            const int n = recv(s, buffer, size, 0);
            if (n > 0) {
                return n;
            }
            if (n == SOCKET_ERROR && WSAGetLastError() == WSAEWOULDBLOCK) {
                continue;
            }
            return -1; // closed (0) or error
        }
        waited += kSliceMs;
    } while (timeoutMs < 0 || waited < timeoutMs);
    return 0;
}

void VoiceClient::CloseSocket(Socket& sock)
{
    if (sock != 0) {
        closesocket(static_cast<SOCKET>(sock));
        sock = 0;
    }
}

bool VoiceClient::RunWebSocket()
{
    Socket sock = 0;
    if (!Connect(sock)) {
        return false;
    }

    unsigned char keyBytes[16];
    for (auto& b : keyBytes) {
        b = static_cast<unsigned char>(Rng()() & 0xFF);
    }
    if (!SendAll(sock, BuildUpgradeRequest(kHost, port_, "/ws", Base64(keyBytes, sizeof(keyBytes))))) {
        CloseSocket(sock);
        return false;
    }

    // Read the HTTP upgrade response.
    std::string header;
    std::vector<char> buffer(16384);
    int waited = 0;
    while (header.find("\r\n\r\n") == std::string::npos) {
        const int n = Receive(sock, buffer.data(), static_cast<int>(buffer.size()), kSliceMs);
        if (n < 0 || header.size() > 16384) {
            CloseSocket(sock);
            return false;
        }
        if (n == 0 && (waited += kSliceMs) >= kHttpTimeoutMs) {
            CloseSocket(sock);
            return false;
        }
        header.append(buffer.data(), static_cast<std::size_t>(n));
    }
    const std::size_t headerEnd = header.find("\r\n\r\n") + 4;
    if (header.compare(0, 12, "HTTP/1.1 101") != 0 && header.compare(0, 12, "HTTP/1.0 101") != 0) {
        CloseSocket(sock);
        return false;
    }

    WsReader reader;
    reader.Feed(header.data() + headerEnd, header.size() - headerEnd);

    status_ = ConnectionStatus::TrackAudioWebSocket;

    std::string message;
    bool open = true;
    while (open && running_) {
        WsFrame frame;
        bool error = false;
        while (open && reader.Next(frame, error)) {
            switch (frame.opcode) {
                case WsOpcode::Text:
                case WsOpcode::Binary:
                    message = frame.payload;
                    break;
                case WsOpcode::Continuation:
                    message += frame.payload;
                    break;
                case WsOpcode::Ping:
                    SendAll(sock, EncodeClientFrame(WsOpcode::Pong, frame.payload, Rng()()));
                    continue;
                case WsOpcode::Close:
                    SendAll(sock, EncodeClientFrame(WsOpcode::Close, std::string(), Rng()()));
                    open = false;
                    continue;
                default:
                    continue;
            }
            if (!frame.fin) {
                continue;
            }

            const VoiceEvent event = ParseVoiceMessage(message);
            message.clear();
            switch (event.type) {
                case VoiceEventType::RxBegin:
                    state_.RxBegin(event.callsign, event.frequencyHz);
                    Notify();
                    break;
                case VoiceEventType::RxEnd:
                    state_.RxEnd(event.callsign, event.frequencyHz);
                    Notify();
                    break;
                case VoiceEventType::VoiceDisconnected:
                    state_.Clear();
                    Notify();
                    break;
                default:
                    break;
            }
        }
        if (error || !open) {
            break;
        }

        // Wait (indefinitely, in short slices) for more data.
        const int n = Receive(sock, buffer.data(), static_cast<int>(buffer.size()), -1);
        if (n < 0) {
            break;
        }
        reader.Feed(buffer.data(), static_cast<std::size_t>(n));
    }

    CloseSocket(sock);
    return true;
}

bool VoiceClient::HttpGet(const std::string& path, int& status, std::string& body)
{
    Socket sock = 0;
    if (!Connect(sock)) {
        return false;
    }
    const std::string request = "GET " + path + " HTTP/1.1\r\nHost: " + kHost + ":" + std::to_string(port_) +
                                "\r\nUser-Agent: RxHighlighter-EuroScope\r\nConnection: close\r\n\r\n";
    if (!SendAll(sock, request)) {
        CloseSocket(sock);
        return false;
    }

    std::string raw;
    char buffer[4096];
    int waited = 0;
    // True once a response with an explicit length/chunked body is complete
    // (otherwise the body ends when the server closes the connection).
    auto complete = [&raw]() {
        int s = 0;
        std::string b;
        const bool framed = raw.find("Content-Length") != std::string::npos ||
                            raw.find("content-length") != std::string::npos ||
                            raw.find("chunked") != std::string::npos;
        return framed && ParseHttpResponse(raw, s, b);
    };
    for (;;) {
        const int n = Receive(sock, buffer, sizeof(buffer), kSliceMs);
        if (n < 0) {
            break; // server closed the connection: response complete
        }
        if (n == 0) {
            if ((waited += kSliceMs) >= kHttpTimeoutMs) {
                CloseSocket(sock);
                return false;
            }
            continue;
        }
        raw.append(buffer, static_cast<std::size_t>(n));
        if (raw.size() > 64 * 1024) {
            CloseSocket(sock);
            return false;
        }
        if (complete()) {
            break;
        }
    }
    CloseSocket(sock);
    return running_ && ParseHttpResponse(raw, status, body);
}

bool VoiceClient::RunHttpPolling()
{
    bool anySuccess = false;
    while (running_) {
        int status = 0;
        std::string body;
        if (!HttpGet("/transmitting", status, body) || status != 200) {
            break;
        }
        anySuccess = true;
        status_ = ConnectionStatus::HttpPolling;
        state_.ReplaceTransmitting(ParseTransmittingList(body));
        Notify();
        InterruptibleSleep(kPollIntervalMs);
    }
    return anySuccess;
}

} // namespace RxHighlight
