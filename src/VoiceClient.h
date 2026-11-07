#pragma once
// Background connection to the local VATSIM voice client.
//
//  1. TrackAudio WebSocket  ws://127.0.0.1:<port>/ws  (kRxBegin / kRxEnd events)
//  2. Fallback: HTTP GET http://127.0.0.1:<port>/transmitting, polled
//     (the legacy AFV-compatible endpoint)
//
// Runs entirely on its own thread and never calls the EuroScope API.
// All socket waits are short select() calls, so Stop() returns within a
// fraction of a second and never freezes EuroScope on exit/unload.

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <thread>

#include "RxState.h"

namespace RxHighlight {

enum class ConnectionStatus : int
{
    Disconnected = 0,
    TrackAudioWebSocket = 1,
    HttpPolling = 2,
};

class VoiceClient
{
public:
    // `onChange` is called on the background thread whenever the RX state
    // changes; it must be cheap and thread-safe (e.g. MainThreadWaker::Wake).
    VoiceClient(RxState& state, std::uint16_t port, std::function<void()> onChange = {});
    ~VoiceClient();

    VoiceClient(const VoiceClient&) = delete;
    VoiceClient& operator=(const VoiceClient&) = delete;

    void Start();
    void Stop();

    ConnectionStatus Status() const { return status_.load(); }

private:
    using Socket = std::uintptr_t;

    void Run();
    bool RunWebSocket();   // true if a WebSocket session was established
    bool RunHttpPolling(); // true if at least one poll succeeded
    bool HttpGet(const std::string& path, int& status, std::string& body);

    bool Connect(Socket& sock);
    bool SendAll(Socket sock, const std::string& data);
    // >0 bytes read, 0 = timed out (nothing yet), -1 = closed / error / stopping
    int Receive(Socket sock, char* buffer, int size, int timeoutMs);
    void CloseSocket(Socket& sock);
    void InterruptibleSleep(int ms);

    void Notify() { if (onChange_) onChange_(); }

    RxState& state_;
    const std::uint16_t port_;
    std::function<void()> onChange_;
    std::atomic<bool> running_{false};
    std::atomic<ConnectionStatus> status_{ConnectionStatus::Disconnected};
    std::thread thread_;
    std::mutex sleepMutex_;
    std::condition_variable sleepCv_;
};

} // namespace RxHighlight
