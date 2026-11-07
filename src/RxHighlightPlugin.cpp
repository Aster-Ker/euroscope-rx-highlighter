#include "RxHighlightPlugin.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <sstream>

#include "RxRadarScreen.h"
#include "Version.h"

namespace RxHighlight {

namespace {

constexpr const char* kHandler = "RX Highlighter";
constexpr const char* kCommand = ".rxh";

void CopyItem(char dest[16], const std::string& text)
{
    const std::size_t n = std::min<std::size_t>(text.size(), 15);
    text.copy(dest, n);
    dest[n] = '\0';
}

std::vector<std::string> Split(const std::string& line)
{
    std::istringstream in(line);
    std::vector<std::string> parts;
    std::string part;
    while (in >> part) {
        parts.push_back(part);
    }
    return parts;
}

std::string Lower(std::string s)
{
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

bool ParseInt(const std::string& text, long min, long max, long& out)
{
    char* end = nullptr;
    long value = std::strtol(text.c_str(), &end, 10);
    if (end == text.c_str() || *end != '\0' || value < min || value > max) {
        return false;
    }
    out = value;
    return true;
}

// Parses "R,G,B" (each 0-255).
bool ParseRgb(const std::string& text, COLORREF& out)
{
    long rgb[3] = {0, 0, 0};
    std::size_t start = 0;
    for (int i = 0; i < 3; ++i) {
        const std::size_t comma = text.find(',', start);
        const bool last = i == 2;
        if (last != (comma == std::string::npos)) {
            return false;
        }
        const std::string part = text.substr(start, last ? std::string::npos : comma - start);
        if (!ParseInt(part, 0, 255, rgb[i])) {
            return false;
        }
        start = comma + 1;
    }
    out = RGB(rgb[0], rgb[1], rgb[2]);
    return true;
}

// Parses three separate command arguments R G B.
bool ParseRgbArgs(const std::vector<std::string>& args, std::size_t first, COLORREF& out)
{
    return args.size() == first + 3 && ParseRgb(args[first] + "," + args[first + 1] + "," + args[first + 2], out);
}

std::string RgbText(COLORREF c)
{
    return std::to_string(GetRValue(c)) + "," + std::to_string(GetGValue(c)) + "," + std::to_string(GetBValue(c));
}

} // namespace

RxHighlightPlugin::RxHighlightPlugin()
    : CPlugIn(
          EuroScopePlugIn::COMPATIBILITY_CODE,
          RXH_PLUGIN_NAME,
          RXH_VERSION_STRING,
          RXH_AUTHOR,
          "Released under the MIT licence")
{
    RegisterTagItemType("RX Highlighter: Callsign (lights up on RX)", TAG_ITEM_RX_CALLSIGN);
    RegisterTagItemType("RX Highlighter: Transmitting marker", TAG_ITEM_RX_MARKER);

    LoadSettings();

    voice_ = std::make_unique<VoiceClient>(state_, port_);
    voice_->Start();

    Message(std::string("Version ") + RXH_VERSION_STRING + " loaded. Type " + kCommand + " for help.");
}

RxHighlightPlugin::~RxHighlightPlugin()
{
    if (voice_) {
        voice_->Stop();
    }
    for (auto* screen : screens_) {
        screen->Detach();
    }
}

void RxHighlightPlugin::LoadSettings()
{
    if (const char* v = GetDataFromSettings("RxColour")) {
        ParseRgb(v, colour_);
    }
    if (const char* v = GetDataFromSettings("RxNormalColour")) {
        // "default" (or anything that isn't R,G,B) = EuroScope's own colours.
        normalColourSet_ = ParseRgb(v, normalColour_);
    }
    if (const char* v = GetDataFromSettings("RxLingerMs")) {
        long ms = 0;
        if (ParseInt(v, 0, 10000, ms)) {
            linger_ = std::chrono::milliseconds(ms);
        }
    }
    if (const char* v = GetDataFromSettings("RxMarker")) {
        if (*v != '\0') {
            marker_ = v;
        }
    }
    if (const char* v = GetDataFromSettings("RxPort")) {
        long port = 0;
        if (ParseInt(v, 1, 65535, port)) {
            port_ = static_cast<std::uint16_t>(port);
        }
    }
}

void RxHighlightPlugin::Message(const std::string& text, bool flash)
{
    DisplayUserMessage(kHandler, kHandler, text.c_str(), true, true, false, flash, false);
}

bool RxHighlightPlugin::IsTransmitting(const std::string& normalisedCallsign) const
{
    return transmitting_.count(normalisedCallsign) != 0;
}

void RxHighlightPlugin::OnGetTagItem(
    EuroScopePlugIn::CFlightPlan FlightPlan,
    EuroScopePlugIn::CRadarTarget RadarTarget,
    int ItemCode,
    int /*TagData*/,
    char sItemString[16],
    int* pColorCode,
    COLORREF* pRGB,
    double* /*pFontSize*/)
{
    if (ItemCode != TAG_ITEM_RX_CALLSIGN && ItemCode != TAG_ITEM_RX_MARKER) {
        return;
    }

    const char* raw = nullptr;
    if (FlightPlan.IsValid()) {
        raw = FlightPlan.GetCallsign();
    } else if (RadarTarget.IsValid()) {
        raw = RadarTarget.GetCallsign();
    }
    if (raw == nullptr) {
        return;
    }

    const bool rx = !transmitting_.empty() && IsTransmitting(NormaliseCallsign(raw));

    if (ItemCode == TAG_ITEM_RX_CALLSIGN) {
        CopyItem(sItemString, raw);
    } else {
        CopyItem(sItemString, rx ? marker_ : std::string());
    }

    if (rx) {
        *pColorCode = EuroScopePlugIn::TAG_COLOR_RGB_DEFINED;
        *pRGB = colour_;
    } else if (normalColourSet_ && ItemCode == TAG_ITEM_RX_CALLSIGN) {
        *pColorCode = EuroScopePlugIn::TAG_COLOR_RGB_DEFINED;
        *pRGB = normalColour_;
    }
    // Otherwise we leave the colour alone, so EuroScope applies its normal
    // assumed / notified / non-concerned colour.
}

void RxHighlightPlugin::UpdateTransmitting()
{
    const auto now = Clock::now();
    std::set<std::string> next = state_.Snapshot(linger_, now);

    tests_.erase(
        std::remove_if(tests_.begin(), tests_.end(), [&](const auto& t) { return t.second <= now; }), tests_.end());
    for (const auto& t : tests_) {
        next.insert(t.first);
    }

    if (next != transmitting_) {
        transmitting_.swap(next);
        RefreshScreens();
    }
}

void RxHighlightPlugin::RefreshScreens()
{
    for (auto* screen : screens_) {
        screen->RequestRefresh();
    }
}

void RxHighlightPlugin::OnTimer(int /*Counter*/)
{
    UpdateTransmitting();

    const ConnectionStatus status = voice_ ? voice_->Status() : ConnectionStatus::Disconnected;
    if (status != lastStatus_) {
        if (status == ConnectionStatus::TrackAudioWebSocket) {
            Message("Connected to TrackAudio.");
        } else if (status == ConnectionStatus::HttpPolling) {
            Message("Connected to voice client (HTTP /transmitting).");
        } else {
            Message("Lost connection to the voice client - will keep retrying.");
        }
        lastStatus_ = status;
    }
}

EuroScopePlugIn::CRadarScreen* RxHighlightPlugin::OnRadarScreenCreated(
    const char* /*sDisplayName*/, bool /*NeedRadarContent*/, bool /*GeoReferenced*/, bool /*CanBeSaved*/, bool /*CanBeCreated*/)
{
    auto* screen = new RxRadarScreen(this);
    screens_.push_back(screen);
    return screen;
}

void RxHighlightPlugin::RadarScreenClosed(RxRadarScreen* screen)
{
    screens_.erase(std::remove(screens_.begin(), screens_.end(), screen), screens_.end());
}

void RxHighlightPlugin::RestartVoiceClient()
{
    if (voice_) {
        voice_->Stop();
    }
    state_.Clear();
    voice_ = std::make_unique<VoiceClient>(state_, port_);
    voice_->Start();
}

void RxHighlightPlugin::ShowHelp()
{
    Message(std::string(kCommand) + " status               - connection and settings");
    Message(std::string(kCommand) + " colour R G B         - highlight colour, e.g. .rxh colour 255 230 0");
    Message(std::string(kCommand) + " normal R G B         - callsign colour when NOT transmitting (default white)");
    Message(std::string(kCommand) + " normal default       - use EuroScope's own colours when not transmitting");
    Message(std::string(kCommand) + " linger MS            - keep lit for MS after RX ends (0-10000)");
    Message(std::string(kCommand) + " marker TEXT          - text for the 'Transmitting marker' item");
    Message(std::string(kCommand) + " port N               - voice client port (default 49080)");
    Message(std::string(kCommand) + " test CALLSIGN [SEC]  - pretend CALLSIGN is transmitting");
}

void RxHighlightPlugin::ShowStatus()
{
    const ConnectionStatus status = voice_ ? voice_->Status() : ConnectionStatus::Disconnected;
    const char* statusText = status == ConnectionStatus::TrackAudioWebSocket ? "connected to TrackAudio (WebSocket)"
                             : status == ConnectionStatus::HttpPolling      ? "connected (HTTP /transmitting)"
                                                                           : "not connected - is TrackAudio running?";
    std::string normalText = "EuroScope default";
    if (normalColourSet_) {
        normalText = std::to_string(GetRValue(normalColour_)) + " " + std::to_string(GetGValue(normalColour_)) + " " +
                     std::to_string(GetBValue(normalColour_));
    }
    char buf[300];
    std::snprintf(
        buf,
        sizeof(buf),
        "Voice: %s on port %u | colour %d %d %d | normal %s | linger %ld ms | marker '%s'",
        statusText,
        static_cast<unsigned>(port_),
        GetRValue(colour_),
        GetGValue(colour_),
        GetBValue(colour_),
        normalText.c_str(),
        static_cast<long>(linger_.count()),
        marker_.c_str());
    Message(buf);

    std::string who;
    for (const auto& cs : transmitting_) {
        who += (who.empty() ? "" : ", ") + cs;
    }
    Message("Lit up now: " + (who.empty() ? std::string("nobody") : who));
}

bool RxHighlightPlugin::OnCompileCommand(const char* sCommandLine)
{
    const std::vector<std::string> args = Split(sCommandLine != nullptr ? sCommandLine : "");
    if (args.empty() || Lower(args[0]) != kCommand) {
        return false; // not ours - let UKCP / vFPC / EuroScope handle it
    }

    const std::string sub = args.size() > 1 ? Lower(args[1]) : "help";

    if (sub == "status") {
        ShowStatus();
    } else if ((sub == "colour" || sub == "color") && args.size() == 5) {
        if (!ParseRgbArgs(args, 2, colour_)) {
            Message("Colour values must be 0-255, e.g. .rxh colour 255 230 0");
            return true;
        }
        SaveDataToSettings("RxColour", "RX Highlighter colour (R,G,B)", RgbText(colour_).c_str());
        RefreshScreens();
        Message("Highlight colour set to " + RgbText(colour_) + ".");
    } else if (sub == "normal" && args.size() == 3 && Lower(args[2]) == "default") {
        normalColourSet_ = false;
        SaveDataToSettings("RxNormalColour", "RX Highlighter colour when not transmitting", "default");
        RefreshScreens();
        Message("Callsigns use EuroScope's own colours when not transmitting.");
    } else if (sub == "normal" && args.size() == 5) {
        if (!ParseRgbArgs(args, 2, normalColour_)) {
            Message("Colour values must be 0-255, e.g. .rxh normal 255 255 255");
            return true;
        }
        normalColourSet_ = true;
        SaveDataToSettings(
            "RxNormalColour", "RX Highlighter colour when not transmitting", RgbText(normalColour_).c_str());
        RefreshScreens();
        Message("Callsign colour when not transmitting set to " + RgbText(normalColour_) + ".");
    } else if (sub == "linger" && args.size() == 3) {
        long ms = 0;
        if (!ParseInt(args[2], 0, 10000, ms)) {
            Message("Linger must be 0-10000 ms.");
            return true;
        }
        linger_ = std::chrono::milliseconds(ms);
        SaveDataToSettings("RxLingerMs", "RX Highlighter linger (ms)", std::to_string(ms).c_str());
        Message("Linger set to " + std::to_string(ms) + " ms.");
    } else if (sub == "marker" && args.size() >= 3) {
        marker_ = args[2];
        if (marker_.size() > 15) {
            marker_.resize(15);
        }
        SaveDataToSettings("RxMarker", "RX Highlighter marker text", marker_.c_str());
        Message("Marker set to '" + marker_ + "'.");
    } else if (sub == "port" && args.size() == 3) {
        long port = 0;
        if (!ParseInt(args[2], 1, 65535, port)) {
            Message("Port must be 1-65535.");
            return true;
        }
        port_ = static_cast<std::uint16_t>(port);
        SaveDataToSettings("RxPort", "RX Highlighter voice client port", std::to_string(port).c_str());
        RestartVoiceClient();
        Message("Reconnecting on port " + std::to_string(port) + ".");
    } else if (sub == "test" && (args.size() == 3 || args.size() == 4)) {
        long seconds = 5;
        if (args.size() == 4 && !ParseInt(args[3], 1, 60, seconds)) {
            Message("Test duration must be 1-60 seconds.");
            return true;
        }
        const std::string cs = NormaliseCallsign(args[2]);
        tests_.emplace_back(cs, Clock::now() + std::chrono::seconds(seconds));
        UpdateTransmitting();
        Message("Lighting up " + cs + " for " + std::to_string(seconds) + " s.");
    } else {
        ShowHelp();
    }
    return true;
}

} // namespace RxHighlight
