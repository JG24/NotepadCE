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
#include "snippets.h"
#include "resource.h"
#include "ui.h"
#include "lang/lang.h"
#include <commctrl.h>
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
    case IDM_QUICK_SNIPPETS:   return g_state.snippetsPanelVisible;
    }
    return false;
}

bool IsQuickIconId(UINT id)
{
    return id == IDM_QUICK_SPELLCHECK || id == IDM_QUICK_ONTOP ||
           id == IDM_QUICK_DARKMODE || id == IDM_QUICK_INSERTCHAR ||
           id == IDM_QUICK_SNIPPETS;
}

// The nine glyphs offered by the insert-special-character popup, in the
// order they appear. Shared by the popup builder and the click handler.
struct SpecialChar
{
    UINT id;
    const wchar_t *glyph;
};
static const SpecialChar kSpecialChars[] = {
    { IDM_INSCHAR_EURO,       L"€" }, // €
    { IDM_INSCHAR_POUND,      L"£" }, // £
    { IDM_INSCHAR_COPYRIGHT,  L"©" }, // ©
    { IDM_INSCHAR_REGISTERED, L"®" }, // ®
    { IDM_INSCHAR_TRADEMARK,  L"™" }, // ™
    { IDM_INSCHAR_SECTION,    L"§" }, // §
    { IDM_INSCHAR_DEGREE,     L"°" }, // °
    { IDM_INSCHAR_BULLET,     L"•" }, // •
    { IDM_INSCHAR_MIDDLEDOT,  L"·" }, // ·
};

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

    // Display order, left to right: insert special char, spell check,
    // theme toggle, snippets panel, always on top.
    const UINT ids[] = { IDM_QUICK_INSERTCHAR, IDM_QUICK_SPELLCHECK,
                         IDM_QUICK_DARKMODE, IDM_QUICK_SNIPPETS, IDM_QUICK_ONTOP };
    const int count = static_cast<int>(sizeof(ids) / sizeof(ids[0]));

    bool present = FindMenuItemIndex(hMenu, IDM_QUICK_INSERTCHAR) != -1;

    if (g_state.quickAccessIcons)
    {
        if (present)
        {
            DrawMenuBar(g_hwndMain);
            return;
        }
        // Insert the owner-drawn items at the end of the menu bar. Only the
        // first one needs MFT_RIGHTJUSTIFY — every item after a right-
        // justified one is also pushed right, preserving the order above.
        for (int i = 0; i < count; ++i)
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
        for (int i = 0; i < count; ++i)
        {
            int idx;
            while ((idx = FindMenuItemIndex(hMenu, ids[i])) != -1)
                RemoveMenu(hMenu, idx, MF_BYPOSITION);
        }
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

static void DrawInsertCharIcon(HDC hdc, RECT rc, bool dark)
{
    int cx = (rc.left + rc.right) / 2;
    int cy = (rc.top + rc.bottom) / 2;

    // Not a toggle — always drawn in a plain foreground colour.
    COLORREF c = dark ? RGB(210, 210, 210) : RGB(70, 70, 70);

    // Omega is the conventional "insert symbol" glyph.
    HFONT hFont = CreateFontW(16, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                              DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                              CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    HGDIOBJ oldFont = SelectObject(hdc, hFont);
    SetTextColor(hdc, c);
    SetBkMode(hdc, TRANSPARENT);
    SIZE sz{};
    GetTextExtentPoint32W(hdc, L"Ω", 1, &sz);
    TextOutW(hdc, cx - sz.cx / 2, cy - sz.cy / 2, L"Ω", 1);
    SelectObject(hdc, oldFont);
    DeleteObject(hFont);
}

static void DrawSnippetsIcon(HDC hdc, RECT rc, bool active, bool dark)
{
    int cx = (rc.left + rc.right) / 2;
    int cy = (rc.top + rc.bottom) / 2;

    COLORREF c;
    if (active)
        c = dark ? RGB(120, 180, 255) : RGB(0, 100, 200);
    else
        c = dark ? RGB(170, 170, 170) : RGB(120, 120, 120);

    // Window outline with a right-hand side panel; the panel pane is
    // filled when the snippets panel is visible.
    HPEN pen = CreatePen(PS_SOLID, 1, c);
    HGDIOBJ oldPen = SelectObject(hdc, pen);
    HGDIOBJ oldBrush = SelectObject(hdc, GetStockObject(HOLLOW_BRUSH));
    Rectangle(hdc, cx - 8, cy - 6, cx + 8, cy + 7);
    MoveToEx(hdc, cx + 2, cy - 6, nullptr);
    LineTo(hdc, cx + 2, cy + 7);
    if (active)
    {
        HBRUSH fill = CreateSolidBrush(c);
        RECT pane = {cx + 3, cy - 5, cx + 7, cy + 6};
        FillRect(hdc, &pane, fill);
        DeleteObject(fill);
    }
    SelectObject(hdc, oldBrush);
    SelectObject(hdc, oldPen);
    DeleteObject(pen);
}

// ---- Item drawing ----------------------------------------------------------

void DrawQuickIconItem(const DRAWITEMSTRUCT *dis)
{
    if (!dis || dis->CtlType != ODT_MENU)
        return;

    bool dark = IsDarkMode();
    bool hot = (dis->itemState & (ODS_SELECTED | ODS_HOTLIGHT)) != 0;

    // Hover tint only in light mode — in dark mode the lighter shade
    // sticks on icons the cursor passed over because Win11 doesn't
    // reliably re-fire WM_DRAWITEM on hover-end. Light mode's hover
    // shade is handled natively by Windows so persists / clears
    // correctly.
    COLORREF bg = dark ? RGB(45, 45, 45) : GetSysColor(COLOR_MENU);
    if (hot && !dark)
        bg = RGB(220, 220, 220);

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
    case IDM_QUICK_INSERTCHAR:
        DrawInsertCharIcon(dis->hDC, dis->rcItem, dark);
        break;
    case IDM_QUICK_SNIPPETS:
        DrawSnippetsIcon(dis->hDC, dis->rcItem, state, dark);
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
    case IDM_QUICK_SNIPPETS:
        g_state.snippetsPanelVisible = !g_state.snippetsPanelVisible;
        UpdateSnippetsVisibility();
        break;
    case IDM_QUICK_INSERTCHAR:
    {
        // Pop up the special-character list right under the icon and insert
        // the chosen glyph at the caret. TPM_RETURNCMD hands the selected
        // command back here, so no extra WM_COMMAND cases are needed.
        HideQuickIconTooltip(); // don't leave a tip floating over the popup
        const LangStrings &s = GetLangStrings();
        const LStr *names[] = {
            &s.scEuro, &s.scPound, &s.scCopyright, &s.scRegistered, &s.scTrademark,
            &s.scSection, &s.scDegree, &s.scBullet, &s.scMiddleDot
        };
        const int n = static_cast<int>(sizeof(kSpecialChars) / sizeof(kSpecialChars[0]));
        HMENU pop = CreatePopupMenu();
        if (!pop)
            break;
        for (int i = 0; i < n; ++i)
        {
            // glyph on the left, name right-aligned via the tab column.
            std::wstring label = std::wstring(kSpecialChars[i].glyph) + L"\t" + *names[i];
            AppendMenuW(pop, MF_STRING, kSpecialChars[i].id, label.c_str());
        }
        RECT rc = {};
        int idx = hMenu ? FindMenuItemIndex(hMenu, IDM_QUICK_INSERTCHAR) : -1;
        if (idx >= 0)
            GetMenuItemRect(g_hwndMain, hMenu, static_cast<UINT>(idx), &rc);
        UINT cmd = static_cast<UINT>(TrackPopupMenu(pop, TPM_RETURNCMD | TPM_LEFTALIGN | TPM_TOPALIGN,
                                                    rc.left, rc.bottom, 0, g_hwndMain, nullptr));
        DestroyMenu(pop);
        if (cmd >= IDM_INSCHAR_FIRST && cmd <= IDM_INSCHAR_LAST && g_hwndEditor)
        {
            const wchar_t *glyph = kSpecialChars[cmd - IDM_INSCHAR_FIRST].glyph;
            SendMessageW(g_hwndEditor, EM_REPLACESEL, TRUE, reinterpret_cast<LPARAM>(glyph));
            SetFocus(g_hwndEditor);
        }
        break;
    }
    }
    DrawMenuBar(g_hwndMain);
}

// ---- Hover tooltips --------------------------------------------------------
//
// The icons are owner-drawn menu-bar items, not child windows, so a normal
// tool-rectangle tooltip can't be attached to them. Instead we run a single
// tracking tooltip (TTF_TRACK | TTF_ABSOLUTE) that the main window drives
// from WM_NCMOUSEMOVE: we hit-test the cursor against each icon's on-screen
// rect (GetMenuItemRect) and, when it lands on one, position the tip just
// below the icon and activate it. WM_NCMOUSELEAVE (and moving off the icons)
// deactivates it. g_tooltipIconId tracks which icon's tip is currently up so
// we update only on a change — no per-pixel flicker.

static HWND g_quickTooltip = nullptr;
static UINT g_tooltipIconId = 0; // 0 = nothing shown

static const wchar_t *QuickIconTooltipText(UINT id)
{
    const LangStrings &s = GetLangStrings();
    switch (id)
    {
    case IDM_QUICK_SPELLCHECK: return s.tipQuickSpell.c_str();
    case IDM_QUICK_ONTOP:      return s.tipQuickOnTop.c_str();
    case IDM_QUICK_DARKMODE:   return s.tipQuickTheme.c_str();
    case IDM_QUICK_INSERTCHAR: return s.tipQuickInsertChar.c_str();
    case IDM_QUICK_SNIPPETS:   return s.tipQuickSnippets.c_str();
    }
    return L"";
}

static void EnsureQuickTooltip()
{
    if (g_quickTooltip || !g_hwndMain)
        return;
    g_quickTooltip = CreateWindowExW(WS_EX_TOPMOST, TOOLTIPS_CLASSW, nullptr,
                                     WS_POPUP | TTS_ALWAYSTIP | TTS_NOPREFIX,
                                     CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
                                     g_hwndMain, nullptr, GetModuleHandleW(nullptr), nullptr);
    if (!g_quickTooltip)
        return;
    TOOLINFOW ti = {};
    ti.cbSize = sizeof(ti);
    ti.uFlags = TTF_TRACK | TTF_ABSOLUTE;
    ti.hwnd = g_hwndMain;
    ti.uId = 1;
    ti.lpszText = const_cast<LPWSTR>(L"");
    SendMessageW(g_quickTooltip, TTM_ADDTOOLW, 0, reinterpret_cast<LPARAM>(&ti));
}

void HideQuickIconTooltip()
{
    if (!g_quickTooltip || g_tooltipIconId == 0)
        return;
    TOOLINFOW ti = {};
    ti.cbSize = sizeof(ti);
    ti.hwnd = g_hwndMain;
    ti.uId = 1;
    SendMessageW(g_quickTooltip, TTM_TRACKACTIVATE, FALSE, reinterpret_cast<LPARAM>(&ti));
    g_tooltipIconId = 0;
}

void ShowQuickIconTooltip(POINT ptScreen)
{
    if (!g_state.quickAccessIcons)
    {
        HideQuickIconTooltip();
        return;
    }
    HMENU hMenu = GetMenu(g_hwndMain);
    if (!hMenu)
        return;
    EnsureQuickTooltip();
    if (!g_quickTooltip)
        return;

    const UINT ids[] = { IDM_QUICK_INSERTCHAR, IDM_QUICK_SPELLCHECK,
                         IDM_QUICK_DARKMODE, IDM_QUICK_SNIPPETS, IDM_QUICK_ONTOP };
    UINT hitId = 0;
    RECT hitRc = {};
    for (UINT id : ids)
    {
        int idx = FindMenuItemIndex(hMenu, id);
        if (idx < 0)
            continue;
        RECT rc;
        if (GetMenuItemRect(g_hwndMain, hMenu, static_cast<UINT>(idx), &rc) && PtInRect(&rc, ptScreen))
        {
            hitId = id;
            hitRc = rc;
            break;
        }
    }

    if (hitId == 0)
    {
        HideQuickIconTooltip();
        return;
    }
    if (hitId == g_tooltipIconId)
        return; // already showing this icon's tip — leave it be

    g_tooltipIconId = hitId;
    TOOLINFOW ti = {};
    ti.cbSize = sizeof(ti);
    ti.hwnd = g_hwndMain;
    ti.uId = 1;
    ti.lpszText = const_cast<LPWSTR>(QuickIconTooltipText(hitId));
    SendMessageW(g_quickTooltip, TTM_UPDATETIPTEXTW, 0, reinterpret_cast<LPARAM>(&ti));
    SendMessageW(g_quickTooltip, TTM_TRACKPOSITION, 0,
                 static_cast<LPARAM>(MAKELONG(hitRc.left, hitRc.bottom + 2)));
    SendMessageW(g_quickTooltip, TTM_TRACKACTIVATE, TRUE, reinterpret_cast<LPARAM>(&ti));
}
