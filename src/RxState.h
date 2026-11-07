#pragma once
// Thread-safe record of who is currently being received on frequency.
// Portable C++17 (no Windows/EuroScope dependencies) so it can be unit tested.

#include <chrono>
#include <cstdint>
#include <map>
#include <mutex>
#include <set>
#include <string>

namespace RxHighlight {

using Clock = std::chrono::steady_clock;

// Upper-cases and trims a callsign so voice and EuroScope callsigns compare equal.
std::string NormaliseCallsign(const std::string& callsign);

class RxState
{
public:
    // A station started being received on a frequency (frequencyHz = 0 if unknown).
    void RxBegin(const std::string& callsign, std::int64_t frequencyHz, Clock::time_point now = Clock::now());

    // A station stopped being received on a frequency.
    void RxEnd(const std::string& callsign, std::int64_t frequencyHz, Clock::time_point now = Clock::now());

    // Replace the whole picture with a polled list of transmitting callsigns
    // (used for the AFV-style HTTP /transmitting endpoint).
    void ReplaceTransmitting(const std::set<std::string>& callsigns, Clock::time_point now = Clock::now());

    // Forget everything (e.g. voice client disconnected).
    void Clear();

    // Callsigns that are transmitting now, or stopped less than `linger` ago.
    // Entries stuck "transmitting" for longer than `stuckTimeout` are dropped
    // in case an end-of-transmission message was missed.
    std::set<std::string> Snapshot(
        std::chrono::milliseconds linger,
        Clock::time_point now = Clock::now(),
        std::chrono::seconds stuckTimeout = std::chrono::seconds(120));

private:
    std::mutex mutex_;
    // callsign -> (frequency -> time the transmission started)
    std::map<std::string, std::map<std::int64_t, Clock::time_point>> active_;
    // callsign -> time the last transmission ended
    std::map<std::string, Clock::time_point> lastEnded_;
};

} // namespace RxHighlight
