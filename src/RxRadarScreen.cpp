#include "RxRadarScreen.h"

#include "RxHighlightPlugin.h"

namespace RxHighlight {

RxRadarScreen::RxRadarScreen(RxHighlightPlugin* owner) : owner_(owner)
{
}

void RxRadarScreen::OnRefresh(HDC hDC, int Phase)
{
    if (owner_ == nullptr || Phase != EuroScopePlugIn::REFRESH_PHASE_BEFORE_TAGS || !owner_->HaloEnabled()) {
        return;
    }
    const auto& transmitting = owner_->Transmitting();
    if (transmitting.empty()) {
        return;
    }

    const RECT area = GetRadarArea();
    HPEN pen = CreatePen(PS_SOLID, 2, owner_->HighlightColour());
    HGDIOBJ oldPen = SelectObject(hDC, pen);
    HGDIOBJ oldBrush = SelectObject(hDC, GetStockObject(HOLLOW_BRUSH));

    constexpr int radius = 14;
    for (const auto& callsign : transmitting) {
        EuroScopePlugIn::CRadarTarget target = owner_->RadarTargetSelect(callsign.c_str());
        if (!target.IsValid()) {
            continue;
        }
        const POINT p = ConvertCoordFromPositionToPixel(target.GetPosition().GetPosition());
        if (p.x < area.left || p.x > area.right || p.y < area.top || p.y > area.bottom) {
            continue;
        }
        Ellipse(hDC, p.x - radius, p.y - radius, p.x + radius, p.y + radius);
    }

    SelectObject(hDC, oldBrush);
    SelectObject(hDC, oldPen);
    DeleteObject(pen);
}

void RxRadarScreen::OnAsrContentToBeClosed()
{
    if (owner_ != nullptr) {
        owner_->RadarScreenClosed(this);
    }
    delete this;
}

} // namespace RxHighlight
