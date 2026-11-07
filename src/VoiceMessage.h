#pragma once
// Minimal parser for the TrackAudio WebSocket messages this plugin needs.
// Portable C++17 so it can be unit tested off Windows.

#include <cstdint>
#include <set>
#include <string>

namespace RxHighlight {

enum class VoiceEventType
{
    Unknown,
    RxBegin,             // {"type":"kRxBegin","value":{"callsign":"BAW123","pFrequencyHz":118500000}}
    RxEnd,               // {"type":"kRxEnd","value":{"callsign":"BAW123","pFrequencyHz":118500000}}
    VoiceConnected,      // {"type":"kVoiceConnectedState","value":{"connected":true}}
    VoiceDisconnected,   // {"type":"kVoiceConnectedState","value":{"connected":false}}
};

struct VoiceEvent
{
    VoiceEventType type = VoiceEventType::Unknown;
    std::string callsign;
    std::int64_t frequencyHz = 0;
};

VoiceEvent ParseVoiceMessage(const std::string& json);

// Parses the body of GET /transmitting ("BAW123,EZY45" or empty).
std::set<std::string> ParseTransmittingList(const std::string& body);

} // namespace RxHighlight
