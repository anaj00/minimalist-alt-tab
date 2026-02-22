#pragma once

#include <windows.h>
#include <string>

namespace UiHelpers
{
inline void FillRoundedRect(HDC dc, const RECT& rc, COLORREF color, int radius)
{
    HPEN pen = CreatePen(PS_SOLID, 1, color);
    HBRUSH brush = CreateSolidBrush(color);
    HPEN oldPen = (HPEN)SelectObject(dc, pen);
    HBRUSH oldBrush = (HBRUSH)SelectObject(dc, brush);
    RoundRect(dc, rc.left, rc.top, rc.right, rc.bottom, radius, radius);
    SelectObject(dc, oldPen);
    SelectObject(dc, oldBrush);
    DeleteObject(pen);
    DeleteObject(brush);
}

inline int DrawAppBadge(
    HDC dc,
    const RECT& rowRc,
    int leftX,
    int centerY,
    int maxWidth,
    const std::wstring& appName,
    HICON icon,
    HFONT font,
    COLORREF badgeBg,
    COLORREF badgeText,
    int iconSize,
    int badgeHeight,
    int badgePadX,
    int radius)
{
    if (appName.empty() || maxWidth <= 60)
        return 0;

    HFONT oldFont = (HFONT)SelectObject(dc, font);

    SIZE textSize = {};
    GetTextExtentPoint32W(dc, appName.c_str(), (int)appName.size(), &textSize);

    int desiredW = iconSize + 6 + textSize.cx + badgePadX * 2;
    int badgeW = (desiredW < maxWidth) ? desiredW : maxWidth;

    RECT badgeRc = {};
    badgeRc.left = leftX;
    badgeRc.right = leftX + badgeW;
    badgeRc.top = centerY - (badgeHeight / 2);
    badgeRc.bottom = badgeRc.top + badgeHeight;

    FillRoundedRect(dc, badgeRc, badgeBg, radius);

    int iconX = badgeRc.left + badgePadX;
    int iconY = badgeRc.top + (badgeHeight - iconSize) / 2;
    DrawIconEx(dc, iconX, iconY, icon, iconSize, iconSize, 0, NULL, DI_NORMAL);

    RECT textRc = badgeRc;
    textRc.left += badgePadX + iconSize + 6;
    textRc.right -= badgePadX;
    SetTextColor(dc, badgeText);
    SetBkMode(dc, TRANSPARENT);
    DrawTextW(dc, appName.c_str(), -1, &textRc,
        DT_SINGLELINE | DT_END_ELLIPSIS | DT_LEFT | DT_VCENTER);

    SelectObject(dc, oldFont);
    return badgeW;
}

inline RECT FitRectPreserveAspectNoUpscale(const RECT& bounds, const SIZE& source)
{
    RECT out = bounds;
    int boundW = bounds.right - bounds.left;
    int boundH = bounds.bottom - bounds.top;

    if (source.cx <= 0 || source.cy <= 0 || boundW <= 0 || boundH <= 0)
        return out;

    double sx = (double)boundW / (double)source.cx;
    double sy = (double)boundH / (double)source.cy;
    double scale = (sx < sy) ? sx : sy;
    if (scale > 1.0)
        scale = 1.0;

    int outW = (int)(source.cx * scale);
    int outH = (int)(source.cy * scale);
    if (outW < 1) outW = 1;
    if (outH < 1) outH = 1;

    int x = bounds.left + (boundW - outW) / 2;
    int y = bounds.top + (boundH - outH) / 2;
    out.left = x;
    out.top = y;
    out.right = x + outW;
    out.bottom = y + outH;
    return out;
}
}

