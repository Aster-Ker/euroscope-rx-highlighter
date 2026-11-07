#include "RxRadarScreen.h"

#include "RxHighlightPlugin.h"

namespace RxHighlight {

RxRadarScreen::RxRadarScreen(RxHighlightPlugin* owner) : owner_(owner)
{
}

void RxRadarScreen::OnAsrContentToBeClosed()
{
    if (owner_ != nullptr) {
        owner_->RadarScreenClosed(this);
    }
    delete this;
}

} // namespace RxHighlight
