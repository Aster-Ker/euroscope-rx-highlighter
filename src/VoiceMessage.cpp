#include "VoiceMessage.h"

#include <cctype>
#include <cstdlib>

namespace RxHighlight {

namespace {

// Finds `"key"` followed by ':' and returns the index of the first character
// of the value, or npos.
std::size_t FindValue(const std::string& json, const std::string& key)
{
    const std::string quoted = "\"" + key + "\"";
    std::size_t pos = 0;
    while ((pos = json.find(quoted, pos)) != std::string::npos) {
        std::size_t i = pos + quoted.size();
        while (i < json.size() && std::isspace(static_cast<unsigned char>(json[i]))) {
            ++i;
        }
        if (i < json.size() && json[i] == ':') {
            ++i;
            while (i < json.size() && std::isspace(static_cast<unsigned char>(json[i]))) {
                ++i;
            }
            return i < json.size() ? i : std::string::npos;
        }
        pos += quoted.size();
    }
    return std::string::npos;
}

bool GetString(const std::string& json, const std::string& key, std::string& out)
{
    std::size_t i = FindValue(json, key);
    if (i == std::string::npos || json[i] != '"') {
        return false;
    }
    out.clear();
    for (++i; i < json.size(); ++i) {
        char c = json[i];
        if (c == '\\' && i + 1 < json.size()) {
            out.push_back(json[++i]);
        } else if (c == '"') {
            return true;
        } else {
            out.push_back(c);
        }
    }
    return false;
}

bool GetInt(const std::string& json, const std::string& key, std::int64_t& out)
{
    std::size_t i = FindValue(json, key);
    if (i == std::string::npos) {
        return false;
    }
    const char* begin = json.c_str() + i;
    char* end = nullptr;
    double value = std::strtod(begin, &end);
    if (end == begin) {
        return false;
    }
    out = static_cast<std::int64_t>(value);
    return true;
}

bool GetBool(const std::string& json, const std::string& key, bool& out)
{
    std::size_t i = FindValue(json, key);
    if (i == std::string::npos) {
        return false;
    }
    if (json.compare(i, 4, "true") == 0) {
        out = true;
        return true;
    }
    if (json.compare(i, 5, "false") == 0) {
        out = false;
        return true;
    }
    return false;
}

} // namespace

VoiceEvent ParseVoiceMessage(const std::string& json)
{
    VoiceEvent event;
    std::string type;
    if (!GetString(json, "type", type)) {
        return event;
    }

    if (type == "kRxBegin" || type == "kRxEnd") {
        if (!GetString(json, "callsign", event.callsign) || event.callsign.empty()) {
            return event;
        }
        GetInt(json, "pFrequencyHz", event.frequencyHz);
        event.type = type == "kRxBegin" ? VoiceEventType::RxBegin : VoiceEventType::RxEnd;
    } else if (type == "kVoiceConnectedState") {
        bool connected = false;
        if (GetBool(json, "connected", connected)) {
            event.type = connected ? VoiceEventType::VoiceConnected : VoiceEventType::VoiceDisconnected;
        }
    }
    return event;
}

std::set<std::string> ParseTransmittingList(const std::string& body)
{
    std::set<std::string> result;
    std::string current;
    auto flush = [&]() {
        std::size_t s = 0;
        std::size_t e = current.size();
        while (s < e && std::isspace(static_cast<unsigned char>(current[s]))) ++s;
        while (e > s && std::isspace(static_cast<unsigned char>(current[e - 1]))) --e;
        if (e > s) {
            result.insert(current.substr(s, e - s));
        }
        current.clear();
    };
    for (char c : body) {
        if (c == ',' || c == '\n' || c == '\r') {
            flush();
        } else {
            current.push_back(c);
        }
    }
    flush();
    return result;
}

} // namespace RxHighlight
