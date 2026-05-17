/*
  Right-justified owner-drawn menu bar icons (spell check, always-on-top,
  light/dark). Items are inserted/removed dynamically depending on
  g_state.quickAccessIcons. We use MFT_OWNERDRAW + MFT_RIGHTJUSTIFY so the
  three icons land flush right on the menu bar without taking any extra
  vertical space. Glyphs are drawn with GDI primitives — no font/bitmap
  dependency, predictable across Windows versions.
*/

#include "quickicons.h"
#include "core/globals.h"
#include "core/types.h"
#include "theme.h"
#include "spellchecker.h"
#include "resource.h"
#include "ui.h"
#include <cmath>

static const int ICON_W = 30;
static const int ICON_H = 22;

static bool IconState(UINT id)
{
    switch (id)
    {
    case IDM_QUICK_SPELLCHECK: return g_state.spellCheckEnabled;
    case IDM_QUICK_ONTOP:      return g_state.alwaysOnTop;
    case IDM_QUICK_DARKMODE:   return IsDarkMode();
    }
    return false;
}

bool IsQuickIconId(UINT id)
{
    return id == IDM_QUICK_SPELLCHECK || id == IDM_QUICK_ONTOP || id == IDM_QUICK_DARKMODE;
}

static int FindMenuItemIndex(HMENU hMenu, UINT id)
{
    int count = GetMenuItemCount(hMenu);
    for (int i = 0; i < count; ++i)
        if (GetMenuItemID(hMenu, i) == id)
            return i;
    return -1;
}

void UpdateQuickIconsVisibility()
{
    HMENU hMenu = GetMenu(g_hwndMain);
    if (!hMenu)
        return;

    bool present = FindMenuItemIndex(hMenu, IDM_QUICK_SPELLCHECK) != -1;

    if (g_state.quickAccessIcons)
    {
        if (present)
        {
            DrawMenuBar(g_hwndMain);
            return;
        }
        // Insert three owner-drawn items at the end of the menu bar.
        // Only the first one needs MFT_RIGHTJUSTIFY — all items after a
        // right-justified one are also pushed right.
        const UINT ids[] = { IDM_QUICK_SPELLCHECK, IDM_QUICK_ONTOP, IDM_QUICK_DARKMODE };
        for (int i = 0; i < 3; ++i)
        {
            MENUITEMINFOW mii = {};
            mii.cbSize = sizeof(mii);
            mii.fMask = MIIM_FTYPE | MIIM_ID | MIIM_DATA;
            mii.fType = MFT_OWNERDRAW;
            if (i == 0)
                mii.fType |= MFT_RIGHTJUSTIFY;
            mii.wID = ids[i];
            mii.dwItemData = ids[i];
            InsertMenuItemW(hMenu, GetMenuItemCount(hMenu), TRUE, &mii);
        }
    }
    else
    {
        if (!present)
            return;
        int idx;
        while ((idx = FindMenuItemIndex(hMenu, IDM_QUICK_SPELLCHECK)) != -1)
            RemoveMenu(hMenu, idx, MF_BYPOSITION);
        while ((idx = FindMenuItemIndex(hMenu, IDM_QUICK_ONTOP)) != -1)
            RemoveMenu(hMenu, idx, MF_BYPOSITION);
        while ((idx = FindMenuItemIndex(hMenu, IDM_QUICK_DARKMODE)) != -1)
            RemoveMenu(hMenu, idx, MF_BYPOSITION);
    }
    DrawMenuBar(g_hwndMain);
}

void RefreshQuickIcons()
{
    if (g_state.quickAccessIcons)
        DrawMenuBar(g_hwndMain);
}

void MeasureQuickIconItem(MEASUREITEMSTRUCT *mis)
{
    mis->itemWidth = ICON_W;
    mis->itemHeight = ICON_H;
}

// ---- Glyph painters --------------------------------------------------------

static void DrawSpellIcon(HDC hdc, RECT rc, bool active, bool dark)
{
    int cx = (rc.left + rc.right) / 2;
    int cy = (rc.top + rc.bottom) / 2;

    COLORREF c;
    if (active)
        c = dark ? RGB(120, 220, 130) : RGB(0, 130, 0);
    else
        c = dark ? RGB(170, 170, 170) : RGB(120, 120, 120);

    // "abc" text
    HFONT hFont = CreateFontW(13, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                              DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                              CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    HGDIOBJ oldFont = SelectObject(hdc, hFont);
    SetTextColor(hdc, c);
    SetBkMode(hdc, TRANSPARENT);
    SIZE sz{};
    GetTextExtentPoint32W(hdc, L"abc", 3, &sz);
    int tx = cx - sz.cx / 2;
    int ty = cy - sz.cy / 2 - 2;
    TextOutW(hdc, tx, ty, L"abc", 3);
    SelectObject(hdc, oldFont);
    DeleteObject(hFont);

    // Underline: wavy if active, straight gray if not
    HPEN pen = CreatePen(PS_SOLID, 1, c);
    HGDIOBJ oldPen = SelectObject(hdc, pen);
    int uy = ty + sz.cy - 1;
    int ux1 = tx;
    int ux2 = tx + sz.cx;
    MoveToEx(hdc, ux1, uy, nullptr);
    if (active)
    {
        int step = 2;
        bool up = true;
        for (int x = ux1; x <= ux2; x += step)
        {
            LineTo(hdc, x, uy + (up ? -1 : 1));
            up = !up;
        }
    }
    else
    {
        LineTo(hdc, ux2, uy);
    }
    SelectObject(hdc, oldPen);
    DeleteObject(pen);
}

static void DrawPinIcon(HDC hdc, RECT rc, bool active, bool dark)
{
    int cx = (rc.left + rc.right) / 2;
    int cy = (rc.top + rc.bottom) / 2;

    COLORREF c;
    if (active)
        c = dark ? RGB(120, 180, 255) : RGB(0, 100, 200);
    else
        c = dark ? RGB(170, 170, 170) : RGB(120, 120, 120);

    HPEN pen = CreatePen(PS_SOLID, 2, c);
    HBRUSH brush = active ? CreateSolidBrush(c) : reinterpret_cast<HBRUSH>(GetStockObject(HOLLOW_BRUSH));
    HGDIOBJ oldPen = SelectObject(hdc, pen);
    HGDIOBJ oldBrush = SelectObject(hdc, brush);

    // Thumbtack: triangular head + needle line
    POINT pts[4] = {
        { cx,     cy - 7 },
        { cx + 5, cy - 1 },
        { cx,     cy + 4 },
        { cx - 5, cy - 1 },
    };
    Polygon(hdc, pts, 4);
    MoveToEx(hdc, cx, cy + 4, nullptr);
    LineTo(hdc, cx, cy + 8);

    SelectObject(hdc, oldBrush);
    SelectObject(hdc, oldPen);
    DeleteObject(pen);
    if (active)
        DeleteObject(brush);
}

static void DrawDarkLightIcon(HDC hdc, RECT rc, bool isDark, bool dark)
{
    int cx = (rc.left + rc.right) / 2;
    int cy = (rc.top + rc.bottom) / 2;

    // When in dark mode, show a SUN (means: click to switch to light).
    // When in light mode, show a MOON (means: click to switch to dark).
    COLORREF c = dark ? RGB(230, 200, 100) : RGB(60, 60, 110);

    HPEN pen = CreatePen(PS_SOLID, 1, c);
    HBRUSH brush = CreateSolidBrush(c);
    HGDIOBJ oldPen = SelectObject(hdc, pen);
    HGDIOBJ oldBrush = SelectObject(hdc, brush);

    if (isDark)
    {
        // Sun: filled circle + 8 rays.
        Ellipse(hdc, cx - 3, cy - 3, cx + 4, cy + 4);
        const double PI = 3.14159265358979;
        for (int a = 0; a < 8; ++a)
        {
            double rad = a * PI / 4.0;
            int x1 = cx + static_cast<int>(5 * cos(rad));
            int y1 = cy + static_cast<int>(5 * sin(rad));
            int x2 = cx + static_cast<int>(7 * cos(rad));
            int y2 = cy + static_cast<int>(7 * sin(rad));
            MoveToEx(hdc, x1, y1, nullptr);
            LineTo(hdc, x2, y2);
        }
    }
    else
    {
        // Crescent moon: filled disk minus an overlapping disk in bg color.
        Ellipse(hdc, cx - 6, cy - 7, cx + 7, cy + 6);
        // Overlay with the menu-bar background color so the cutout looks
        // transparent.
        COLORREF bg = dark ? RGB(45, 45, 45) : GetSysColor(COLOR_MENU);
        HBRUSH bgBrush = CreateSolidBrush(bg);
        HPEN bgPen = CreatePen(PS_SOLID, 1, bg);
        SelectObject(hdc, bgBrush);
        SelectObject(hdc, bgPen);
        Ellipse(hdc, cx - 3, cy - 9, cx + 10, cy + 4);
        SelectObject(hdc, oldBrush);
        SelectObject(hdc, oldPen);
        DeleteObject(bgBrush);
        DeleteObject(bgPen);
    }

    SelectObject(hdc, oldBrush);
    SelectObject(hdc, oldPen);
    DeleteObject(pen);
    DeleteObject(brush);
}

// ---- Item drawing ----------------------------------------------------------

void DrawQuickIconItem(const DRAWITEMSTRUCT *dis)
{
    if (!dis || dis->CtlType != ODT_MENU)
        return;

    bool dark = IsDarkMode();
    bool hot = (dis->itemState & (ODS_SELECTED | ODS_HOTLIGHT)) != 0;

    COLORREF bg = dark ? RGB(45, 45, 45) : GetSysColor(COLOR_MENU);
    if (hot)
        bg = dark ? RGB(70, 70, 70) : RGB(220, 220, 220);

    HBRUSH hbr = CreateSolidBrush(bg);
    FillRect(dis->hDC, &dis->rcItem, hbr);
    DeleteObject(hbr);

    UINT id = static_cast<UINT>(dis->itemID);
    bool state = IconState(id);

    switch (id)
    {
    case IDM_QUICK_SPELLCHECK:
        DrawSpellIcon(dis->hDC, dis->rcItem, state, dark);
        break;
    case IDM_QUICK_ONTOP:
        DrawPinIcon(dis->hDC, dis->rcItem, state, dark);
        break;
    case IDM_QUICK_DARKMODE:
        DrawDarkLightIcon(dis->hDC, dis->rcItem, state, dark);
        break;
    }
}

void HandleQuickIconClick(UINT id)
{
    HMENU hMenu = GetMenu(g_hwndMain);
    switch (id)
    {
    case IDM_QUICK_SPELLCHECK:
        if (!IsSpellCheckAvailable())
            return;
        g_state.spellCheckEnabled = !g_state.spellCheckEnabled;
        if (hMenu)
            CheckMenuItem(hMenu, IDM_EDIT_SETTINGS_SPELLCHECK,
                          g_state.spellCheckEnabled ? MF_CHECKED : MF_UNCHECKED);
        if (g_state.spellCheckEnabled)
            ApplySpellingMarks();
        else
            ClearSpellingMarks();
        break;
    case IDM_QUICK_ONTOP:
        g_state.alwaysOnTop = !g_state.alwaysOnTop;
        SetWindowPos(g_hwndMain, g_state.alwaysOnTop ? HWND_TOPMOST : HWND_NOTOPMOST,
                     0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
        if (hMenu)
            CheckMenuItem(hMenu, IDM_VIEW_ALWAYSONTOP,
                          g_state.alwaysOnTop ? MF_CHECKED : MF_UNCHECKED);
        break;
    case IDM_QUICK_DARKMODE:
        ToggleDarkMode();
        break;
    }
    DrawMenuBar(g_hwndMain);
}
