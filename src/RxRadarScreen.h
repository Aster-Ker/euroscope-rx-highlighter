#pragma once

#include <windows.h>

#include "EuroScopePlugIn.h"

namespace RxHighlight {

class RxHighlightPlugin;

// Draws nothing. It only exists so the plug-in can ask EuroScope to redraw
// (RequestRefresh) as soon as someone starts or stops transmitting, which
// makes the list update straight away. Nothing is drawn on the radar and no
// clickable screen objects are registered, so it cannot affect the radar
// picture or steal clicks/menus from UKCP, vFPC, TopSky etc.
class RxRadarScreen final : public EuroScopePlugIn::CRadarScreen
{
public:
    explicit RxRadarScreen(RxHighlightPlugin* owner);

    void OnAsrContentToBeClosed() override;

    // Called if the plugin is unloaded before the display is closed.
    void Detach() { owner_ = nullptr; }

private:
    RxHighlightPlugin* owner_;
};

} // namespace RxHighlight
