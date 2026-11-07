#pragma once

#include <chrono>
#include <cstdint>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include <windows.h>

#include "EuroScopePlugIn.h"
#include "RxState.h"
#include "VoiceClient.h"

namespace RxHighlight {

class RxRadarScreen;

// Tag item codes (scoped to this plugin by EuroScope, so they cannot clash
// with UK Controller Plugin, vFPC or any other plugin's items).
constexpr int TAG_ITEM_RX_CALLSIGN = 1;
constexpr int TAG_ITEM_RX_MARKER = 2;

class RxHighlightPlugin : public EuroScopePlugIn::CPlugIn
{
public:
    RxHighlightPlugin();
    ~RxHighlightPlugin() override;

    void OnGetTagItem(
        EuroScopePlugIn::CFlightPlan FlightPlan,
        EuroScopePlugIn::CRadarTarget RadarTarget,
        int ItemCode,
        int TagData,
        char sItemString[16],
        int* pColorCode,
        COLORREF* pRGB,
        double* pFontSize) override;

    void OnTimer(int Counter) override;
    bool OnCompileCommand(const char* sCommandLine) override;

    EuroScopePlugIn::CRadarScreen* OnRadarScreenCreated(
        const char* sDisplayName, bool NeedRadarContent, bool GeoReferenced, bool CanBeSaved, bool CanBeCreated) override;

    // Called by RxRadarScreen.
    void RadarScreenClosed(RxRadarScreen* screen);
    bool IsTransmitting(const std::string& normalisedCallsign) const;

private:
    void LoadSettings();
    void Message(const std::string& text, bool flash = false);
    void UpdateTransmitting();
    void RefreshScreens();
    void ShowHelp();
    void ShowStatus();
    void RestartVoiceClient();

    RxState state_;
    std::unique_ptr<VoiceClient> voice_;
    std::vector<RxRadarScreen*> screens_;

    // Main-thread copy of who is lit up; only touched on the EuroScope thread.
    std::set<std::string> transmitting_;

    // `.rxh test` simulated transmissions: callsign -> expiry.
    std::vector<std::pair<std::string, Clock::time_point>> tests_;

    ConnectionStatus lastStatus_ = ConnectionStatus::Disconnected;

    // Settings
    COLORREF colour_ = RGB(255, 230, 0);
    // Colour of the callsign when NOT transmitting. White by default;
    // `.rxh normal default` switches to EuroScope's own colours
    // (assumed / notified / non-concerned etc.).
    bool normalColourSet_ = true;
    COLORREF normalColour_ = RGB(255, 255, 255);
    std::chrono::milliseconds linger_{1500};
    std::string marker_ = "RX";
    std::uint16_t port_ = 49080;
};

} // namespace RxHighlight
