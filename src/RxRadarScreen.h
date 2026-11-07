#pragma once

#include <windows.h>

#include "EuroScopePlugIn.h"

namespace RxHighlight {

class RxHighlightPlugin;

// Draws a ring around the radar target of whoever is transmitting.
// It never registers clickable screen objects, so it cannot steal clicks
// or menus from UK Controller Plugin / vFPC / TopSky etc.
class RxRadarScreen final : public EuroScopePlugIn::CRadarScreen
{
public:
    explicit RxRadarScreen(RxHighlightPlugin* owner);

    void OnRefresh(HDC hDC, int Phase) override;
    void OnAsrContentToBeClosed() override;

    // Called if the plugin is unloaded before the display is closed.
    void Detach() { owner_ = nullptr; }

private:
    RxHighlightPlugin* owner_;
};

} // namespace RxHighlight
