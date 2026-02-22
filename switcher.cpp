#define UNICODE
#define _UNICODE

#include <windows.h>
#include <commctrl.h>
#include <shellapi.h>
#include <dwmapi.h>
#include <string>
#include <vector>
#include <algorithm>
#include <atomic>
#include "ui_helpers.h"

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "dwmapi.lib")

// ============================================================
// CONFIG
// ============================================================

#define WIN_W       760
#define WIN_H_MIN   220
#define WIN_H_MAX   420
#define MAX_VISIBLE 8
#define PADDING_V   14
#define PADDING_H   14
#define ROUND_RAD   0
#define GAP_X       14

#define PREVIEW_W   280
#define PREVIEW_H   176
#define ICON_SIZE   16
#define BADGE_H     20
#define BADGE_PAD_X 8

#define WINDOW_ALPHA 245

#define TITLE_FONT_SIZE 15
#define SUB_FONT_SIZE   11

// Row height derived from font sizes (this prevents clipping)
#define ITEM_H 34

#define WM_SHOW_SWITCHER (WM_USER + 1)
#define WM_DO_SWITCH     (WM_USER + 2)
#define WM_MOVE_SEL      (WM_USER + 3)
#define WM_HIDE_SWITCHER (WM_USER + 4)
#define WM_TRAY          (WM_USER + 5)

#define ID_TRAY_EXIT       1001
#define ID_TRAY_STARTUP    1002
#define ID_TRAY_SETTINGS   1003

#define ID_SETTINGS_COMBO_THEME 2001

// ============================================================
// COLORS
// ============================================================

#define BG_COLOR             RGB(24, 24, 28)
#define PANEL_BG_COLOR       RGB(30, 30, 36)
#define PANEL_INNER_COLOR    RGB(38, 38, 45)
#define SEL_BG_COLOR         RGB(75, 75, 88)
#define TITLE_COLOR          RGB(245, 245, 250)
#define SUB_COLOR            RGB(165, 165, 180)
#define DIVIDER_COLOR        RGB(56, 56, 68)
#define BADGE_BG_COLOR       RGB(58, 58, 72)
#define BADGE_TEXT_COLOR     RGB(235, 235, 245)

// ============================================================
// STATE
// ============================================================

struct WindowInfo {
    HWND hwnd;
    std::wstring appName;
    std::wstring title;
    std::wstring displayName;
    HICON icon;
};

struct Theme {
    const wchar_t* name;
    COLORREF bgColor;
    COLORREF panelBgColor;
    COLORREF panelInnerColor;
    COLORREF selBgColor;
    COLORREF titleColor;
    COLORREF subColor;
    COLORREF dividerColor;
    COLORREF badgeBgColor;
    COLORREF badgeTextColor;
};

std::vector<WindowInfo> g_windows;

HWND g_popupWindow = NULL;
HWND g_listBox = NULL;
HWND g_settingsWindow = NULL;
HWND g_settingsThemeCombo = NULL;
RECT g_previewRect = {};
HTHUMBNAIL g_thumb = NULL;
HWND g_thumbSource = NULL;

std::atomic<bool> g_isReady{ false };

HHOOK g_keyboardHook = NULL;
DWORD g_hookThreadId = 0;

HFONT g_fontTitle = NULL;
HFONT g_fontSub = NULL;

HBRUSH g_bgBrush = NULL;
HBRUSH g_panelBrush = NULL;

NOTIFYICONDATAW g_nid = {};

Theme g_themes[] = {
    { L"Dark", BG_COLOR, PANEL_BG_COLOR, PANEL_INNER_COLOR, SEL_BG_COLOR, TITLE_COLOR, SUB_COLOR, DIVIDER_COLOR, BADGE_BG_COLOR, BADGE_TEXT_COLOR },
    { L"Light", RGB(241, 241, 245), RGB(252, 252, 255), RGB(245, 245, 250), RGB(214, 221, 235), RGB(36, 36, 42), RGB(92, 96, 110), RGB(206, 210, 220), RGB(224, 229, 239), RGB(52, 56, 68) },
    { L"Gruvbox", RGB(40, 40, 40), RGB(60, 56, 54), RGB(80, 73, 69), RGB(131, 124, 111), RGB(235, 219, 178), RGB(168, 153, 132), RGB(102, 92, 84), RGB(146, 131, 116), RGB(251, 241, 199) }
};

int g_currentTheme = 0;
COLORREF g_bgColor = BG_COLOR;
COLORREF g_panelBgColor = PANEL_BG_COLOR;
COLORREF g_panelInnerColor = PANEL_INNER_COLOR;
COLORREF g_selBgColor = SEL_BG_COLOR;
COLORREF g_titleColor = TITLE_COLOR;
COLORREF g_subColor = SUB_COLOR;
COLORREF g_dividerColor = DIVIDER_COLOR;
COLORREF g_badgeBgColor = BADGE_BG_COLOR;
COLORREF g_badgeTextColor = BADGE_TEXT_COLOR;

// ============================================================
// FORWARD DECLARATIONS
// ============================================================

void RegisterStartup();
void UnregisterStartup();
void UpdatePreviewThumbnail();
void ClearPreviewThumbnail();
void ApplyTheme(int themeIndex);
void RefreshThemeBrushes();
void OpenSettingsWindow(HINSTANCE hInstance);
LRESULT CALLBACK SettingsWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
void EnsureSelectionVisible();
void DrawMinimalScrollIndicator(HDC dc);

// ============================================================
// ACRYLIC BLUR (Windows 10/11 undocumented API)
// ============================================================

typedef enum _ACCENT_STATE {
    ACCENT_DISABLED = 0,
    ACCENT_ENABLE_GRADIENT = 1,
    ACCENT_ENABLE_TRANSPARENTGRADIENT = 2,
    ACCENT_ENABLE_BLURBEHIND = 3,
    ACCENT_ENABLE_ACRYLICBLURBEHIND = 4,
    ACCENT_ENABLE_HOSTBACKDROP = 5
} ACCENT_STATE;

typedef struct _ACCENT_POLICY {
    ACCENT_STATE AccentState;
    DWORD AccentFlags;
    DWORD GradientColor;
    DWORD AnimationId;
} ACCENT_POLICY;

typedef struct _WINDOWCOMPOSITIONATTRIBDATA {
    int Attrib;
    PVOID pvData;
    SIZE_T cbData;
} WINDOWCOMPOSITIONATTRIBDATA;

typedef BOOL(WINAPI* pSetWindowCompositionAttribute)(
    HWND, WINDOWCOMPOSITIONATTRIBDATA*
);

void EnableAcrylic(HWND hwnd)
{
    HMODULE hUser = GetModuleHandleW(L"user32.dll");
    if (!hUser) return;

    auto SetWCA = (pSetWindowCompositionAttribute)GetProcAddress(hUser, "SetWindowCompositionAttribute");
    if (!SetWCA) return;

    ACCENT_POLICY policy = {};
    policy.AccentState = ACCENT_ENABLE_ACRYLICBLURBEHIND;
    policy.AccentFlags = 2;

    // GradientColor = AABBGGRR
    // AA = opacity (lower = more transparent)
    policy.GradientColor = (0xE0 << 24) | (0x141414);

    WINDOWCOMPOSITIONATTRIBDATA data = {};
    data.Attrib = 19; // WCA_ACCENT_POLICY
    data.pvData = &policy;
    data.cbData = sizeof(policy);

    SetWCA(hwnd, &data);
}

// ============================================================
// HELPERS
// ============================================================

void RefreshThemeBrushes()
{
    if (g_bgBrush) DeleteObject(g_bgBrush);
    if (g_panelBrush) DeleteObject(g_panelBrush);
    g_bgBrush = CreateSolidBrush(g_bgColor);
    g_panelBrush = CreateSolidBrush(g_panelBgColor);
}

void ApplyTheme(int themeIndex)
{
    if (themeIndex < 0 || themeIndex >= (int)(sizeof(g_themes) / sizeof(g_themes[0])))
        return;

    g_currentTheme = themeIndex;
    const Theme& t = g_themes[themeIndex];

    g_bgColor = t.bgColor;
    g_panelBgColor = t.panelBgColor;
    g_panelInnerColor = t.panelInnerColor;
    g_selBgColor = t.selBgColor;
    g_titleColor = t.titleColor;
    g_subColor = t.subColor;
    g_dividerColor = t.dividerColor;
    g_badgeBgColor = t.badgeBgColor;
    g_badgeTextColor = t.badgeTextColor;

    RefreshThemeBrushes();

    if (g_listBox)
        InvalidateRect(g_listBox, NULL, TRUE);
    if (g_popupWindow)
        InvalidateRect(g_popupWindow, NULL, TRUE);
    if (g_settingsWindow)
        InvalidateRect(g_settingsWindow, NULL, TRUE);
}

bool IsCloakedWindow(HWND hwnd)
{
    typedef enum { WM_CLOAKED = 0 } DWMWINDOWATTRIBUTE;
    BOOL isCloaked = FALSE;
    DwmGetWindowAttribute(hwnd, WM_CLOAKED, &isCloaked, sizeof(isCloaked));
    return isCloaked;
}

bool IsTaskbarWindow(HWND hwnd)
{
    if (!IsWindowVisible(hwnd)) return false;
    if (IsCloakedWindow(hwnd)) return false;

    HWND owner = GetWindow(hwnd, GW_OWNER);
    LONG exStyle = GetWindowLong(hwnd, GWL_EXSTYLE);

    if (exStyle & WS_EX_TOOLWINDOW) return false;
    if (owner && !(exStyle & WS_EX_APPWINDOW)) return false;

    wchar_t title[2];
    GetWindowTextW(hwnd, title, 2);
    if (wcslen(title) == 0) return false;

    return true;
}

HICON GetWindowIcon(HWND hwnd)
{
    HICON icon = (HICON)SendMessageW(hwnd, WM_GETICON, ICON_BIG, 0);
    if (icon) return icon;

    icon = (HICON)SendMessageW(hwnd, WM_GETICON, ICON_SMALL2, 0);
    if (icon) return icon;

    icon = (HICON)SendMessageW(hwnd, WM_GETICON, ICON_SMALL, 0);
    if (icon) return icon;

    icon = (HICON)GetClassLongPtrW(hwnd, GCLP_HICON);
    if (icon) return icon;

    icon = (HICON)GetClassLongPtrW(hwnd, GCLP_HICONSM);
    if (icon) return icon;

    return LoadIcon(NULL, IDI_APPLICATION);
}

std::wstring GetProcessNameFromWindow(HWND hwnd)
{
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (pid == 0) return L"App";

    HANDLE hProcess = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!hProcess) return L"App";

    wchar_t path[MAX_PATH] = {};
    DWORD size = MAX_PATH;
    std::wstring appName = L"App";

    if (QueryFullProcessImageNameW(hProcess, 0, path, &size))
    {
        std::wstring full(path);
        size_t pos = full.find_last_of(L"\\/");
        appName = (pos == std::wstring::npos) ? full : full.substr(pos + 1);

        size_t dot = appName.find_last_of(L'.');
        if (dot != std::wstring::npos)
            appName = appName.substr(0, dot);
    }

    CloseHandle(hProcess);
    return appName;
}

BOOL CALLBACK EnumWindowsProc(HWND hwnd, LPARAM)
{
    if (!IsTaskbarWindow(hwnd))
        return TRUE;

    wchar_t title[512];
    GetWindowTextW(hwnd, title, 512);

    if (wcslen(title) == 0) return TRUE;
    if (wcscmp(title, L"Program Manager") == 0) return TRUE;

    // Filter out common utility windows
    wchar_t className[256];
    GetClassNameW(hwnd, className, 256);
    
    if (wcscmp(className, L"Windows.UI.Core.CoreWindow") == 0) return TRUE;
    if (wcscmp(className, L"XAML_WindowClass") == 0) return TRUE;
    if (wcscmp(className, L"Windows.Internal.CompositionSwapChainPanel") == 0) return TRUE;

    HICON icon = GetWindowIcon(hwnd);

    std::wstring appName = GetProcessNameFromWindow(hwnd);
    std::wstring display = appName + L" - " + title;
    g_windows.push_back({ hwnd, appName, title, display, icon });
    return TRUE;
}

void PopulateList()
{
    g_windows.clear();
    EnumWindows(EnumWindowsProc, 0);

    SendMessageW(g_listBox, LB_RESETCONTENT, 0, 0);

    for (auto& w : g_windows)
        SendMessageW(g_listBox, LB_ADDSTRING, 0, (LPARAM)w.displayName.c_str());

    SendMessageW(g_listBox, LB_SETCURSEL, 0, 0);
}

void ReleaseAltKey()
{
    INPUT input = {};
    input.type = INPUT_KEYBOARD;
    input.ki.wVk = VK_MENU;
    input.ki.dwFlags = KEYEVENTF_KEYUP;
    SendInput(1, &input, sizeof(INPUT));
}

void ForceForeground(HWND hwnd)
{
    AllowSetForegroundWindow(ASFW_ANY);
    ShowWindow(hwnd, SW_SHOW);
    SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
    SetForegroundWindow(hwnd);
    BringWindowToTop(hwnd);
}

void ResizeToFitList()
{
    const int previewBlockHeight = PREVIEW_H + 28; // preview + caption line
    int contentHeight = previewBlockHeight;
    int height = PADDING_V + contentHeight + PADDING_V;

    int listX = PADDING_H + PREVIEW_W + GAP_X;
    int listW = WIN_W - listX - PADDING_H;
    int listY = PADDING_V;
    int listHeight = contentHeight;

    int previewY = PADDING_V;
    g_previewRect.left = PADDING_H;
    g_previewRect.top = previewY;
    g_previewRect.right = g_previewRect.left + PREVIEW_W;
    g_previewRect.bottom = g_previewRect.top + PREVIEW_H;

    POINT cursor;
    GetCursorPos(&cursor);

    HMONITOR mon = MonitorFromPoint(cursor, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi = {};
    mi.cbSize = sizeof(mi);
    GetMonitorInfo(mon, &mi);

    int x = mi.rcWork.left + (mi.rcWork.right - mi.rcWork.left - WIN_W) / 2;
    int y = mi.rcWork.top + (mi.rcWork.bottom - mi.rcWork.top - height) / 2;

    SetWindowPos(g_popupWindow, HWND_TOPMOST, x, y, WIN_W, height, SWP_SHOWWINDOW);
    SetWindowPos(g_listBox, NULL, listX, listY, listW, listHeight, SWP_NOZORDER);

    HRGN rgn = CreateRoundRectRgn(0, 0, WIN_W + 1, height + 1, ROUND_RAD, ROUND_RAD);
    SetWindowRgn(g_popupWindow, rgn, TRUE);

    UpdatePreviewThumbnail();
    EnsureSelectionVisible();
}

void MoveSelection(int delta)
{
    int count = (int)SendMessageW(g_listBox, LB_GETCOUNT, 0, 0);
    if (count <= 0) return;

    int sel = (int)SendMessageW(g_listBox, LB_GETCURSEL, 0, 0);
    if (sel == LB_ERR) sel = 0;

    sel = (sel + delta + count) % count;
    SendMessageW(g_listBox, LB_SETCURSEL, sel, 0);
    EnsureSelectionVisible();
    UpdatePreviewThumbnail();
    InvalidateRect(g_listBox, NULL, FALSE);
    InvalidateRect(g_popupWindow, NULL, FALSE);
}

void EnsureSelectionVisible()
{
    if (!g_listBox) return;

    int count = (int)SendMessageW(g_listBox, LB_GETCOUNT, 0, 0);
    if (count <= 0) return;

    int sel = (int)SendMessageW(g_listBox, LB_GETCURSEL, 0, 0);
    if (sel == LB_ERR) return;

    RECT rc = {};
    GetClientRect(g_listBox, &rc);
    int visibleRows = (rc.bottom - rc.top) / ITEM_H;
    if (visibleRows <= 0) visibleRows = 1;

    int top = (int)SendMessageW(g_listBox, LB_GETTOPINDEX, 0, 0);
    if (sel < top)
    {
        SendMessageW(g_listBox, LB_SETTOPINDEX, sel, 0);
    }
    else if (sel >= top + visibleRows)
    {
        int newTop = sel - visibleRows + 1;
        if (newTop < 0) newTop = 0;
        SendMessageW(g_listBox, LB_SETTOPINDEX, newTop, 0);
    }
}

void HideSwitcher()
{
    g_isReady.store(false, std::memory_order_release);
    ClearPreviewThumbnail();
    ShowWindow(g_popupWindow, SW_HIDE);
    ReleaseAltKey();
}

void SwitchToSelected()
{
    int sel = (int)SendMessageW(g_listBox, LB_GETCURSEL, 0, 0);

    if (sel == LB_ERR || sel < 0 || sel >= (int)g_windows.size())
    {
        HideSwitcher();
        return;
    }

    HWND target = g_windows[sel].hwnd;

    g_isReady.store(false, std::memory_order_release);
    ClearPreviewThumbnail();
    ShowWindow(g_popupWindow, SW_HIDE);

    if (!IsWindow(target))
        return;

    // Restore if minimized
    if (IsIconic(target))
        ShowWindow(target, SW_RESTORE);
    else
        ShowWindow(target, SW_SHOW);

    // Simple, direct foreground switch
    AllowSetForegroundWindow(ASFW_ANY);
    SetForegroundWindow(target);
    BringWindowToTop(target);

    ReleaseAltKey();
}

void ShowSwitcher()
{
    PopulateList();
    ResizeToFitList();
    UpdatePreviewThumbnail();

    ForceForeground(g_popupWindow);
    SetFocus(g_listBox);

    g_isReady.store(true, std::memory_order_release);
}

// ============================================================
// TRAY ICON
// ============================================================

void AddTrayIcon(HWND hwnd)
{
    g_nid = {};
    g_nid.cbSize = sizeof(g_nid);
    g_nid.hWnd = hwnd;
    g_nid.uID = 1;
    g_nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    g_nid.uCallbackMessage = WM_TRAY;
    g_nid.hIcon = LoadIcon(NULL, IDI_APPLICATION);

    wcscpy_s(g_nid.szTip, L"Better Alt+Tab");

    Shell_NotifyIconW(NIM_ADD, &g_nid);
}

void RemoveTrayIcon()
{
    Shell_NotifyIconW(NIM_DELETE, &g_nid);
}

// ============================================================
// OWNER DRAW LISTBOX
// ============================================================

void DrawListItem(DRAWITEMSTRUCT* dis)
{
    if (dis->itemID == (UINT)-1)
        return;
    if (dis->itemID >= g_windows.size())
        return;

    bool selected = (dis->itemState & ODS_SELECTED) != 0;

    HDC dc = dis->hDC;
    RECT rc = dis->rcItem;

    HBRUSH bg = CreateSolidBrush(selected ? g_selBgColor : g_panelBgColor);
    FillRect(dc, &rc, bg);
    DeleteObject(bg);

    const WindowInfo& item = g_windows[dis->itemID];

    SetBkMode(dc, TRANSPARENT);

    // Title (single-line, vertically centered)
    SelectObject(dc, g_fontTitle);
    SetTextColor(dc, g_titleColor);

    RECT titleRc = rc;
    titleRc.left = rc.left + 12;
    titleRc.top += (ITEM_H - TITLE_FONT_SIZE) / 2 - 1;
    titleRc.right -= 10;
    titleRc.bottom = titleRc.top + TITLE_FONT_SIZE + 4;

    int badgeW = UiHelpers::DrawAppBadge(
        dc, rc, titleRc.left, rc.top + (ITEM_H / 2), (rc.right - rc.left) / 2,
        item.appName, item.icon, g_fontSub, g_badgeBgColor, g_badgeTextColor,
        ICON_SIZE, BADGE_H, BADGE_PAD_X, 10
    );
    if (badgeW > 0)
        titleRc.left += badgeW + 8;

    SelectObject(dc, g_fontTitle);
    SetTextColor(dc, g_titleColor);
    const std::wstring& primaryText = item.title.empty() ? item.appName : item.title;
    DrawTextW(dc, primaryText.c_str(), -1, &titleRc,
        DT_SINGLELINE | DT_END_ELLIPSIS | DT_LEFT | DT_VCENTER);
}

void ClearPreviewThumbnail()
{
    if (g_thumb)
    {
        DwmUnregisterThumbnail(g_thumb);
        g_thumb = NULL;
        g_thumbSource = NULL;
    }
}

void UpdatePreviewThumbnail()
{
    int sel = (int)SendMessageW(g_listBox, LB_GETCURSEL, 0, 0);
    if (sel == LB_ERR || sel < 0 || sel >= (int)g_windows.size())
    {
        ClearPreviewThumbnail();
        return;
    }

    HWND target = g_windows[sel].hwnd;
    if (!IsWindow(target))
    {
        ClearPreviewThumbnail();
        return;
    }

    if (g_thumb && g_thumbSource != target)
        ClearPreviewThumbnail();

    if (!g_thumb)
    {
        HRESULT hr = DwmRegisterThumbnail(g_popupWindow, target, &g_thumb);
        if (FAILED(hr))
        {
            g_thumb = NULL;
            g_thumbSource = NULL;
            return;
        }
        g_thumbSource = target;
    }

    RECT dest = g_previewRect;
    InflateRect(&dest, -10, -10);

    SIZE source = {};
    if (SUCCEEDED(DwmQueryThumbnailSourceSize(g_thumb, &source)))
        dest = UiHelpers::FitRectPreserveAspectNoUpscale(dest, source);

    DWM_THUMBNAIL_PROPERTIES props = {};
    props.dwFlags = DWM_TNP_RECTDESTINATION |
        DWM_TNP_VISIBLE |
        DWM_TNP_SOURCECLIENTAREAONLY |
        DWM_TNP_OPACITY;
    props.rcDestination = dest;
    props.fVisible = TRUE;
    props.fSourceClientAreaOnly = FALSE;
    props.opacity = 255;

    DwmUpdateThumbnailProperties(g_thumb, &props);
}

void DrawPreviewPane(HDC dc)
{
    RECT paneRect = g_previewRect;
    UiHelpers::FillRoundedRect(dc, paneRect, g_panelBgColor, 14);

    RECT inner = paneRect;
    InflateRect(&inner, -10, -10);
    UiHelpers::FillRoundedRect(dc, inner, g_panelInnerColor, 10);

    int sel = (int)SendMessageW(g_listBox, LB_GETCURSEL, 0, 0);
    if (sel == LB_ERR || sel < 0 || sel >= (int)g_windows.size())
        return;

    RECT caption = paneRect;
    caption.top = paneRect.bottom + 8;
    caption.bottom = caption.top + 20;

    SetBkMode(dc, TRANSPARENT);
    SelectObject(dc, g_fontSub);
    SetTextColor(dc, g_subColor);
    const WindowInfo& item = g_windows[sel];

    RECT titleCaption = caption;
    titleCaption.left = caption.left;
    titleCaption.right = paneRect.right;

    int previewBadgeW = UiHelpers::DrawAppBadge(
        dc, caption, caption.left, caption.top + (BADGE_H / 2), (paneRect.right - paneRect.left) / 2,
        item.appName, item.icon, g_fontSub, g_badgeBgColor, g_badgeTextColor,
        ICON_SIZE, BADGE_H, BADGE_PAD_X, 10
    );
    if (previewBadgeW > 0)
        titleCaption.left = caption.left + previewBadgeW + 8;

    SelectObject(dc, g_fontSub);
    SetTextColor(dc, g_subColor);
    const std::wstring& previewTitle = item.title.empty() ? item.appName : item.title;
    DrawTextW(dc, previewTitle.c_str(), -1, &titleCaption,
        DT_SINGLELINE | DT_LEFT | DT_END_ELLIPSIS | DT_VCENTER);
}

void DrawMinimalScrollIndicator(HDC dc)
{
    if (!g_listBox) return;

    int count = (int)SendMessageW(g_listBox, LB_GETCOUNT, 0, 0);
    if (count <= 0) return;

    RECT listClient = {};
    GetClientRect(g_listBox, &listClient);
    int visibleRows = (listClient.bottom - listClient.top) / ITEM_H;
    if (visibleRows <= 0 || count <= visibleRows) return;

    int top = (int)SendMessageW(g_listBox, LB_GETTOPINDEX, 0, 0);
    if (top < 0) top = 0;
    int maxTop = count - visibleRows;
    if (top > maxTop) top = maxTop;

    RECT listRect = {};
    GetWindowRect(g_listBox, &listRect);
    MapWindowPoints(HWND_DESKTOP, g_popupWindow, (LPPOINT)&listRect, 2);

    RECT track = {};
    track.left = listRect.right - 5;
    track.right = track.left + 2;
    track.top = listRect.top + 6;
    track.bottom = listRect.bottom - 6;

    int trackH = track.bottom - track.top;
    if (trackH < 12) return;

    int thumbH = (trackH * visibleRows) / count;
    if (thumbH < 16) thumbH = 16;
    if (thumbH > trackH) thumbH = trackH;

    int thumbY = track.top;
    if (maxTop > 0)
        thumbY = track.top + ((trackH - thumbH) * top) / maxTop;

    RECT thumb = {};
    thumb.left = track.left;
    thumb.right = track.right;
    thumb.top = thumbY;
    thumb.bottom = thumbY + thumbH;

    UiHelpers::FillRoundedRect(dc, thumb, g_dividerColor, 2);
}

void OpenSettingsWindow(HINSTANCE hInstance)
{
    if (g_settingsWindow && IsWindow(g_settingsWindow))
    {
        ShowWindow(g_settingsWindow, SW_SHOW);
        SetForegroundWindow(g_settingsWindow);
        return;
    }

    g_settingsWindow = CreateWindowExW(
        WS_EX_TOOLWINDOW,
        L"AltTabSettingsClass",
        L"Minimal Alt-Tab Settings",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        CW_USEDEFAULT, CW_USEDEFAULT, 360, 170,
        NULL, NULL, hInstance, NULL
    );

    ShowWindow(g_settingsWindow, SW_SHOW);
    UpdateWindow(g_settingsWindow);
}

LRESULT CALLBACK SettingsWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg)
    {
    case WM_CREATE:
    {
        CreateWindowW(
            L"STATIC", L"Theme",
            WS_CHILD | WS_VISIBLE,
            20, 20, 80, 22,
            hwnd, NULL, GetModuleHandleW(NULL), NULL
        );

        g_settingsThemeCombo = CreateWindowW(
            L"COMBOBOX", NULL,
            WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL,
            20, 45, 300, 220,
            hwnd, (HMENU)ID_SETTINGS_COMBO_THEME, GetModuleHandleW(NULL), NULL
        );

        for (int i = 0; i < (int)(sizeof(g_themes) / sizeof(g_themes[0])); ++i)
            SendMessageW(g_settingsThemeCombo, CB_ADDSTRING, 0, (LPARAM)g_themes[i].name);

        SendMessageW(g_settingsThemeCombo, CB_SETCURSEL, g_currentTheme, 0);
        return 0;
    }
    case WM_COMMAND:
        if (LOWORD(wp) == ID_SETTINGS_COMBO_THEME && HIWORD(wp) == CBN_SELCHANGE)
        {
            int sel = (int)SendMessageW(g_settingsThemeCombo, CB_GETCURSEL, 0, 0);
            ApplyTheme(sel);
            return 0;
        }
        break;
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLORLISTBOX:
    case WM_CTLCOLOREDIT:
    {
        HDC dc = (HDC)wp;
        SetBkMode(dc, TRANSPARENT);
        SetBkColor(dc, g_bgColor);
        SetTextColor(dc, g_titleColor);
        return (LRESULT)g_bgBrush;
    }
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(hwnd, &ps);
        RECT rc;
        GetClientRect(hwnd, &rc);
        FillRect(dc, &rc, g_bgBrush);
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_CLOSE:
        ShowWindow(hwnd, SW_HIDE);
        return 0;
    case WM_DESTROY:
        if (g_settingsWindow == hwnd)
        {
            g_settingsWindow = NULL;
            g_settingsThemeCombo = NULL;
        }
        return 0;
    }

    return DefWindowProcW(hwnd, msg, wp, lp);
}

LRESULT CALLBACK ListSubclassProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp,
    UINT_PTR, DWORD_PTR)
{
    if (msg == WM_KEYDOWN)
    {
        if (wp == VK_RETURN) { SwitchToSelected(); return 0; }
        if (wp == VK_ESCAPE) { HideSwitcher(); return 0; }
        if (wp == VK_DOWN)   { MoveSelection(1); return 0; }
        if (wp == VK_UP)     { MoveSelection(-1); return 0; }
        if (wp == VK_RIGHT)  { MoveSelection(1); return 0; }
        if (wp == VK_LEFT)   { MoveSelection(-1); return 0; }
    }

    if (msg == WM_LBUTTONDBLCLK)
    {
        SwitchToSelected();
        return 0;
    }

    if (msg == WM_VSCROLL || msg == WM_MOUSEWHEEL)
    {
        LRESULT result = DefSubclassProc(hwnd, msg, wp, lp);
        InvalidateRect(g_popupWindow, NULL, FALSE);
        return result;
    }

    return DefSubclassProc(hwnd, msg, wp, lp);
}

// ============================================================
// HOOK
// ============================================================

LRESULT CALLBACK KeyboardProc(int nCode, WPARAM wp, LPARAM lp)
{
    if (nCode != HC_ACTION)
        return CallNextHookEx(g_keyboardHook, nCode, wp, lp);

    KBDLLHOOKSTRUCT* kb = (KBDLLHOOKSTRUCT*)lp;

    if (kb->flags & LLKHF_INJECTED)
        return CallNextHookEx(g_keyboardHook, nCode, wp, lp);

    bool isAltDown = (kb->flags & LLKHF_ALTDOWN) != 0;
    bool isKeyDown = (wp == WM_KEYDOWN || wp == WM_SYSKEYDOWN);
    bool isKeyUp = (wp == WM_KEYUP || wp == WM_SYSKEYUP);

    if (kb->vkCode == VK_TAB && isAltDown && isKeyDown)
    {
        bool ready = g_isReady.load(std::memory_order_acquire);

        if (!ready)
        {
            PostMessageW(g_popupWindow, WM_SHOW_SWITCHER, 0, 0);
            return 1;
        }

        bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
        PostMessageW(g_popupWindow, WM_MOVE_SEL, shift ? -1 : 1, 0);
        return 1;
    }

    if (g_isReady.load(std::memory_order_acquire) &&
        isKeyUp &&
        (kb->vkCode == VK_MENU || kb->vkCode == VK_LMENU || kb->vkCode == VK_RMENU))
    {
        AllowSetForegroundWindow(GetCurrentProcessId());
        PostMessageW(g_popupWindow, WM_DO_SWITCH, 0, 0);
        return 1;
    }

    if (g_isReady.load(std::memory_order_acquire) &&
        isKeyDown &&
        kb->vkCode == VK_ESCAPE)
    {
        PostMessageW(g_popupWindow, WM_HIDE_SWITCHER, 0, 0);
        return 1;
    }

    if (g_isReady.load(std::memory_order_acquire) && isKeyDown)
    {
        if (kb->vkCode == VK_DOWN || kb->vkCode == VK_RIGHT)
        {
            PostMessageW(g_popupWindow, WM_MOVE_SEL, 1, 0);
            return 1;
        }
        if (kb->vkCode == VK_UP || kb->vkCode == VK_LEFT)
        {
            PostMessageW(g_popupWindow, WM_MOVE_SEL, -1, 0);
            return 1;
        }
    }

    return CallNextHookEx(g_keyboardHook, nCode, wp, lp);
}

DWORD WINAPI HookThread(LPVOID)
{
    g_hookThreadId = GetCurrentThreadId();

    g_keyboardHook = SetWindowsHookExW(
        WH_KEYBOARD_LL,
        KeyboardProc,
        GetModuleHandleW(NULL),
        0
    );

    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0))
    {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    UnhookWindowsHookEx(g_keyboardHook);
    return 0;
}

// ============================================================
// WINDOW PROC
// ============================================================

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg)
    {
    case WM_CREATE:
    {
        RefreshThemeBrushes();

        g_listBox = CreateWindowW(
            L"LISTBOX", NULL,
            WS_CHILD | WS_VISIBLE |
            LBS_OWNERDRAWFIXED |
            LBS_HASSTRINGS |
            LBS_NOINTEGRALHEIGHT |
            LBS_NOTIFY,
            0, PADDING_V, WIN_W, MAX_VISIBLE * ITEM_H,
            hwnd, (HMENU)1, GetModuleHandleW(NULL), NULL
        );

        // Fonts
        g_fontTitle = CreateFontW(
            TITLE_FONT_SIZE, 0, 0, 0,
            FW_NORMAL,
            FALSE, FALSE, FALSE,
            DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS,
            CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY,
            DEFAULT_PITCH,
            L"Segoe UI"
        );

        g_fontSub = CreateFontW(
            SUB_FONT_SIZE, 0, 0, 0,
            FW_NORMAL,
            FALSE, FALSE, FALSE,
            DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS,
            CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY,
            DEFAULT_PITCH,
            L"Segoe UI"
        );

        // IMPORTANT: tell listbox the actual row height (THIS FIXES CLIPPING)
        SendMessageW(g_listBox, LB_SETITEMHEIGHT, 0, ITEM_H);

        SetWindowSubclass(g_listBox, ListSubclassProc, 1, 0);

        AddTrayIcon(hwnd);

        EnableAcrylic(hwnd);

        return 0;
    }

    case WM_CTLCOLORLISTBOX:
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLOREDIT:
    {
        HDC dc = (HDC)wp;
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, g_titleColor);

        if ((HWND)lp == g_listBox)
        {
            SetBkColor(dc, g_panelBgColor);
            return (LRESULT)g_panelBrush;
        }

        SetBkColor(dc, g_bgColor);
        return (LRESULT)g_bgBrush;
    }

    case WM_DRAWITEM:
    {
        DRAWITEMSTRUCT* dis = (DRAWITEMSTRUCT*)lp;
        if (dis->CtlID == 1)
        {
            DrawListItem(dis);
            return TRUE;
        }
        break;
    }

    case WM_ERASEBKGND:
        return 1;

    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(hwnd, &ps);

        RECT rc;
        GetClientRect(hwnd, &rc);

        FillRect(dc, &rc, g_bgBrush);

        RECT listPanel;
        listPanel.left = g_previewRect.right + GAP_X;
        listPanel.top = PADDING_V;
        listPanel.right = rc.right - PADDING_H;
        listPanel.bottom = rc.bottom - PADDING_V;
        UiHelpers::FillRoundedRect(dc, listPanel, g_panelBgColor, 14);

        RECT divider = listPanel;
        divider.left -= (GAP_X / 2);
        divider.right = divider.left + 1;
        HBRUSH divBrush = CreateSolidBrush(g_dividerColor);
        FillRect(dc, &divider, divBrush);
        DeleteObject(divBrush);

        DrawPreviewPane(dc);
        DrawMinimalScrollIndicator(dc);

        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_SHOW_SWITCHER:
        ShowSwitcher();
        return 0;

    case WM_MOVE_SEL:
        MoveSelection((int)wp);
        return 0;

    case WM_DO_SWITCH:
        SwitchToSelected();
        return 0;

    case WM_HIDE_SWITCHER:
        HideSwitcher();
        return 0;

    case WM_TRAY:
    {
        if (lp == WM_RBUTTONUP)
        {
            POINT pt;
            GetCursorPos(&pt);

            HMENU menu = CreatePopupMenu();
            AppendMenuW(menu, MF_STRING, ID_TRAY_SETTINGS, L"Settings...");
            AppendMenuW(menu, MF_STRING, ID_TRAY_STARTUP, L"Run at startup");
            AppendMenuW(menu, MF_SEPARATOR, 0, NULL);
            AppendMenuW(menu, MF_STRING, ID_TRAY_EXIT, L"Exit");

            SetForegroundWindow(hwnd);
            TrackPopupMenu(menu, TPM_RIGHTBUTTON, pt.x, pt.y, 0, hwnd, NULL);

            DestroyMenu(menu);
        }
        return 0;
    }

    case WM_COMMAND:
        if (LOWORD(wp) == 1 && HIWORD(wp) == LBN_SELCHANGE)
        {
            UpdatePreviewThumbnail();
            InvalidateRect(g_popupWindow, NULL, FALSE);
            return 0;
        }
        if (LOWORD(wp) == ID_TRAY_SETTINGS)
        {
            OpenSettingsWindow(GetModuleHandleW(NULL));
            return 0;
        }
        if (LOWORD(wp) == ID_TRAY_STARTUP)
        {
            RegisterStartup();
            return 0;
        }
        if (LOWORD(wp) == ID_TRAY_EXIT)
        {
            RemoveTrayIcon();
            PostThreadMessageW(g_hookThreadId, WM_QUIT, 0, 0);
            PostQuitMessage(0);
            return 0;
        }
        break;

    case WM_KILLFOCUS:
        if (g_isReady.load(std::memory_order_acquire))
            HideSwitcher();
        return 0;

    case WM_DESTROY:
    {
        ClearPreviewThumbnail();
        RemoveTrayIcon();

        if (g_bgBrush) DeleteObject(g_bgBrush);
        if (g_panelBrush) DeleteObject(g_panelBrush);
        if (g_fontTitle) DeleteObject(g_fontTitle);
        if (g_fontSub) DeleteObject(g_fontSub);

        PostQuitMessage(0);
        return 0;
    }
    }

    return DefWindowProcW(hwnd, msg, wp, lp);
}

// ============================================================
// STARTUP REGISTRY
// ============================================================

void RegisterStartup()
{
    wchar_t exePath[MAX_PATH];
    GetModuleFileNameW(NULL, exePath, MAX_PATH);

    HKEY hKey;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, 
        L"Software\\Microsoft\\Windows\\CurrentVersion\\Run", 
        0, KEY_SET_VALUE, &hKey) == ERROR_SUCCESS)
    {
        RegSetValueExW(hKey, L"MinimalAltTab", 0, REG_SZ, 
            (const BYTE*)exePath, (wcslen(exePath) + 1) * sizeof(wchar_t));
        RegCloseKey(hKey);
    }
}

void UnregisterStartup()
{
    HKEY hKey;
    if (RegOpenKeyExW(HKEY_CURRENT_USER,
        L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",
        0, KEY_SET_VALUE, &hKey) == ERROR_SUCCESS)
    {
        RegDeleteValueW(hKey, L"MinimalAltTab");
        RegCloseKey(hKey);
    }
}

// ============================================================
// MAIN
// ============================================================

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int)
{
    INITCOMMONCONTROLSEX icc = { sizeof(icc), ICC_STANDARD_CLASSES };
    InitCommonControlsEx(&icc);

    WNDCLASSW wc = {};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = L"AltTabPopupClass";
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);

    RegisterClassW(&wc);

    WNDCLASSW settingsWc = {};
    settingsWc.lpfnWndProc = SettingsWndProc;
    settingsWc.hInstance = hInstance;
    settingsWc.lpszClassName = L"AltTabSettingsClass";
    settingsWc.hCursor = LoadCursor(NULL, IDC_ARROW);
    settingsWc.hbrBackground = NULL;
    RegisterClassW(&settingsWc);

    g_popupWindow = CreateWindowExW(
        WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_LAYERED,
        wc.lpszClassName,
        L"Raycast AltTab Switcher",
        WS_POPUP,
        CW_USEDEFAULT, CW_USEDEFAULT, WIN_W, 400,
        NULL, NULL, hInstance, NULL
    );

    if (!g_popupWindow)
        return 0;

    SetLayeredWindowAttributes(g_popupWindow, 0, WINDOW_ALPHA, LWA_ALPHA);

    ShowWindow(g_popupWindow, SW_HIDE);

    CreateThread(NULL, 0, HookThread, NULL, 0, NULL);

    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0))
    {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    return 0;
}
