#include "menu.h"
#include "core/globals.h"
#include "resource.h"
#include "lang/lang.h"
#include "quickicons.h"
#include "theme.h"
#include <string>

// External cache of top-level menu item labels for dark-mode owner-draw.
// Setting MFT_OWNERDRAW on a menu item changes the semantics of dwTypeData
// (it no longer reliably holds the display string — GetMenuItemInfoW with
// MIIM_STRING returns empty on Win11 for such items), so we stash the
// labels here at the moment of conversion and refresh them when the
// language changes. Indexed by position in the menu bar.
static std::wstring g_topLevelText[32];

// Native menu bar metrics sampled from a still-MFT_STRING item before we
// flip the bar to owner-draw. Used by MeasureTopLevelMenuItem so dark-mode
// items end up the exact size Windows would have drawn them — otherwise
// a guessed padding makes the dark menu visibly "rozstrzelony" compared
// to light mode. Zero means "not sampled yet, use fallback".
static int g_menuBarPadding = 0;
static int g_menuBarHeight = 0;

// Windows silently adds a fixed gutter to whatever width WM_MEASUREITEM
// returns for an owner-drawn menu item (16px at 125% DPI in testing —
// not any nameable system metric). We can't predict it, so we measure
// it: first WM_MEASUREITEM round returns the full desired width, the
// first WM_DRAWITEM compares the rcItem it actually got against that,
// the difference IS the gutter, and a re-measure then returns
// (desired - gutter) so the displayed item lands on the desired width.
// -1 means "not measured yet".
static int g_measureExtra = -1;
// Desired (native-matching) total width per top-level item, recorded by
// MeasureTopLevelMenuItem so DrawTopLevelMenuItem can back-compute the
// gutter. Indexed by wID - IDM_TOPLEVEL_BASE.
static int g_topLevelDesired[32] = {};

// Probe the native sizing of the first still-MFT_STRING top-level popup.
// Item rect width minus the measured text width is exactly the horizontal
// padding Windows uses; rect height is the bar height. Must run BEFORE
// any item is flipped to MFT_OWNERDRAW (otherwise the rect comes from
// our own MEASUREITEM, not from Windows).
static void SampleNativeMenuBarMetrics(HWND hwnd)
{
    if (g_menuBarPadding != 0)
        return;
    HMENU hMenu = GetMenu(hwnd);
    if (!hMenu)
        return;
    // Force the menu bar to lay out its items right now. During WM_CREATE
    // GetMenuBarInfo can come back with an empty rect because the bar
    // hasn't been measured yet — Windows defers the work until first
    // paint. Detach-and-reattach the same menu triggers a synchronous
    // layout pass so rcBar is populated on the very first sample.
    SetMenu(hwnd, nullptr);
    SetMenu(hwnd, hMenu);
    NONCLIENTMETRICSW ncm = {};
    ncm.cbSize = sizeof(ncm);
    SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0);
    int count = GetMenuItemCount(hMenu);
    for (int i = 0; i < count; ++i)
    {
        UINT id = GetMenuItemID(hMenu, i);
        if (IsQuickIconId(id))
            continue;
        HMENU hSub = GetSubMenu(hMenu, i);
        if (!hSub)
            continue;
        MENUITEMINFOW miiCheck = {};
        miiCheck.cbSize = sizeof(miiCheck);
        miiCheck.fMask = MIIM_FTYPE;
        if (!GetMenuItemInfoW(hMenu, i, TRUE, &miiCheck))
            continue;
        if (miiCheck.fType & MFT_OWNERDRAW)
            continue;
        MENUBARINFO mbi = {};
        mbi.cbSize = sizeof(mbi);
        if (!GetMenuBarInfo(hwnd, OBJID_MENU, i + 1, &mbi))
            continue;
        int itemW = mbi.rcBar.right - mbi.rcBar.left;
        int itemH = mbi.rcBar.bottom - mbi.rcBar.top;
        if (itemW <= 0 || itemH <= 0)
            continue;
        wchar_t buf[128] = {};
        MENUITEMINFOW miiText = {};
        miiText.cbSize = sizeof(miiText);
        miiText.fMask = MIIM_STRING;
        miiText.dwTypeData = buf;
        miiText.cch = 127;
        GetMenuItemInfoW(hMenu, i, TRUE, &miiText);
        if (buf[0] == L'\0')
            continue;
        HDC hdc = GetDC(hwnd);
        HFONT hFont = CreateFontIndirectW(&ncm.lfMenuFont);
        HFONT hOldFont = reinterpret_cast<HFONT>(SelectObject(hdc, hFont));
        // Use DrawTextW(DT_CALCRECT) instead of GetTextExtentPoint32W so
        // the ampersand mnemonic prefix gets stripped from the measured
        // width — exactly what Windows does internally when laying out a
        // menu bar item. GetTextExtentPoint32W would count the "&" as a
        // literal character, which would offset our padding calculation.
        RECT rcCalc = {0, 0, 0, 0};
        DrawTextW(hdc, buf, -1, &rcCalc, DT_CALCRECT | DT_SINGLELINE);
        SelectObject(hdc, hOldFont);
        DeleteObject(hFont);
        ReleaseDC(hwnd, hdc);
        int textWidth = rcCalc.right - rcCalc.left;
        int padding = itemW - textWidth;
        if (padding > 0 && padding < 64 && itemH > 0 && itemH < 100)
        {
            g_menuBarPadding = padding;
            g_menuBarHeight = itemH;
            return;
        }
    }
}

// Find a top-level submenu index by the ID of its first item. Used because
// the "Tools" top-level menu can be present or absent depending on the
// toolsEnabled toggle, which shifts the positions of Help.
static int FindTopLevel(HMENU hMenu, UINT firstItemId)
{
    int count = GetMenuItemCount(hMenu);
    for (int i = 0; i < count; i++)
    {
        HMENU sub = GetSubMenu(hMenu, i);
        if (!sub)
            continue;
        if (GetMenuItemID(sub, 0) == firstItemId)
            return i;
    }
    return -1;
}

// Find a nested popup inside `parent` by the ID of the first item of its
// inner submenu. Used to locate the "Language" popup that now lives under
// the "View" menu.
static int FindNestedPopup(HMENU parent, UINT firstInnerId)
{
    int count = GetMenuItemCount(parent);
    for (int i = 0; i < count; i++)
    {
        HMENU sub = GetSubMenu(parent, i);
        if (!sub)
            continue;
        if (GetMenuItemID(sub, 0) == firstInnerId)
            return i;
    }
    return -1;
}

// Set only the item's string, preserving every other attribute (check
// state, enabled/grayed, submenu handle, ID). ModifyMenuW would have
// replaced all flags and silently cleared MF_CHECKED — which is why
// every toggle in the menu bar lost its tick after a language change.
static void SetItemText(HMENU hMenu, UINT itemOrPos, BOOL byPosition, LPCWSTR text)
{
    MENUITEMINFOW mii = {};
    mii.cbSize = sizeof(mii);
    mii.fMask = MIIM_STRING;
    mii.dwTypeData = const_cast<LPWSTR>(text);
    SetMenuItemInfoW(hMenu, itemOrPos, byPosition, &mii);
}

void UpdateMenuStrings()
{
    HMENU hMenu = GetMenu(g_hwndMain);
    if (!hMenu)
        return;

    const auto &lang = GetLangStrings();

    // Top-level labels.
    SetItemText(hMenu, 0, TRUE, lang.menuFile.c_str());
    SetItemText(hMenu, 1, TRUE, lang.menuEdit.c_str());
    SetItemText(hMenu, 2, TRUE, lang.menuFormat.c_str());
    SetItemText(hMenu, 3, TRUE, lang.menuView.c_str());

    int toolsIdx = FindTopLevel(hMenu, IDM_TOOLS_NORMALIZE);
    int helpIdx  = FindTopLevel(hMenu, IDM_HELP_ABOUT);

    if (helpIdx >= 0)
        SetItemText(hMenu, helpIdx, TRUE, lang.menuHelp.c_str());

    HMENU hFileMenu = GetSubMenu(hMenu, 0);
    if (hFileMenu)
    {
        // Address File menu items by ID, not by position. The "Recent
        // Files" submenu is inserted dynamically at position 5, shifting
        // Print / Page Setup / Exit down one slot. Position-based updates
        // would then relabel the Recent Files popup itself (writing
        // "Print" over it) and mislabel everything below it.
        SetItemText(hFileMenu, IDM_FILE_NEW, FALSE, lang.menuNew.c_str());
        SetItemText(hFileMenu, IDM_FILE_OPEN, FALSE, lang.menuOpen.c_str());
        SetItemText(hFileMenu, IDM_FILE_SAVE, FALSE, lang.menuSave.c_str());
        SetItemText(hFileMenu, IDM_FILE_SAVEAS, FALSE, lang.menuSaveAs.c_str());
        SetItemText(hFileMenu, IDM_FILE_PRINT, FALSE, lang.menuPrint.c_str());
        SetItemText(hFileMenu, IDM_FILE_PAGESETUP, FALSE, lang.menuPageSetup.c_str());
        SetItemText(hFileMenu, IDM_FILE_OPENFOLDER, FALSE, lang.menuFileOpenFolder.c_str());
        SetItemText(hFileMenu, IDM_FILE_COPYPATH, FALSE, lang.menuFileCopyPath.c_str());
        SetItemText(hFileMenu, IDM_FILE_EXIT, FALSE, lang.menuExit.c_str());
    }

    HMENU hEditMenu = GetSubMenu(hMenu, 1);
    if (hEditMenu)
    {
        SetItemText(hEditMenu, 0, TRUE, lang.menuUndo.c_str());
        SetItemText(hEditMenu, 1, TRUE, lang.menuRedo.c_str());
        SetItemText(hEditMenu, 3, TRUE, lang.menuCut.c_str());
        SetItemText(hEditMenu, 4, TRUE, lang.menuCopy.c_str());
        SetItemText(hEditMenu, 5, TRUE, lang.menuPaste.c_str());
        SetItemText(hEditMenu, 6, TRUE, lang.menuDelete.c_str());
        SetItemText(hEditMenu, 8, TRUE, lang.menuFind.c_str());
        SetItemText(hEditMenu, 9, TRUE, lang.menuFindNext.c_str());
        SetItemText(hEditMenu, 10, TRUE, lang.menuFindPrev.c_str());
        SetItemText(hEditMenu, 11, TRUE, lang.menuReplace.c_str());
        SetItemText(hEditMenu, 12, TRUE, lang.menuGoTo.c_str());
        SetItemText(hEditMenu, 14, TRUE, lang.menuSelectAll.c_str());
        SetItemText(hEditMenu, 15, TRUE, lang.menuTimeDate.c_str());
        SetItemText(hEditMenu, 16, TRUE, lang.menuSettingsSpellCheck.c_str());

        HMENU hSettingsMenu = GetSubMenu(hEditMenu, 18);
        if (hSettingsMenu)
        {
            SetItemText(hEditMenu, 18, TRUE, lang.menuSettings.c_str());
            SetItemText(hSettingsMenu, 0, TRUE, lang.menuSettingsDateFormat.c_str());
            SetItemText(hSettingsMenu, 1, TRUE, lang.menuSettingsTools.c_str());
            SetItemText(hSettingsMenu, 2, TRUE, lang.menuSettingsQuickIcons.c_str());
            SetItemText(hSettingsMenu, 3, TRUE, lang.menuSettingsHighlightLine.c_str());
            SetItemText(hSettingsMenu, 4, TRUE, lang.menuSettingsHighlightWord.c_str());
        }
    }

    HMENU hFormatMenu = GetSubMenu(hMenu, 2);
    if (hFormatMenu)
    {
        SetItemText(hFormatMenu, 0, TRUE, lang.menuWordWrap.c_str());
        SetItemText(hFormatMenu, 1, TRUE, lang.menuFont.c_str());
        // Position 2 is a separator; 3 = Line Endings popup, 4 = Encoding popup.
        SetItemText(hFormatMenu, 3, TRUE, lang.menuFormatLineEndings.c_str());
        SetItemText(hFormatMenu, 4, TRUE, lang.menuFormatEncoding.c_str());
        HMENU hLE = GetSubMenu(hFormatMenu, 3);
        if (hLE)
        {
            SetItemText(hLE, IDM_FORMAT_LE_CRLF, FALSE, lang.lineEndingCRLF.c_str());
            SetItemText(hLE, IDM_FORMAT_LE_LF, FALSE, lang.lineEndingLF.c_str());
            SetItemText(hLE, IDM_FORMAT_LE_CR, FALSE, lang.lineEndingCR.c_str());
        }
        HMENU hEnc = GetSubMenu(hFormatMenu, 4);
        if (hEnc)
        {
            SetItemText(hEnc, IDM_FORMAT_ENC_UTF8, FALSE, lang.encodingUTF8.c_str());
            SetItemText(hEnc, IDM_FORMAT_ENC_UTF8BOM, FALSE, lang.encodingUTF8BOM.c_str());
            SetItemText(hEnc, IDM_FORMAT_ENC_UTF16LE, FALSE, lang.encodingUTF16LE.c_str());
            SetItemText(hEnc, IDM_FORMAT_ENC_UTF16BE, FALSE, lang.encodingUTF16BE.c_str());
            SetItemText(hEnc, IDM_FORMAT_ENC_ANSI, FALSE, lang.encodingANSI.c_str());
        }
    }

    HMENU hViewMenu = GetSubMenu(hMenu, 3);
    if (hViewMenu)
    {
        // Address View items by command ID — the theme separators and the
        // Light/Dark/Matrix block make position-based indexing fragile.
        SetItemText(hViewMenu, IDM_VIEW_ZOOMIN, FALSE, lang.menuZoomIn.c_str());
        SetItemText(hViewMenu, IDM_VIEW_ZOOMOUT, FALSE, lang.menuZoomOut.c_str());
        SetItemText(hViewMenu, IDM_VIEW_ZOOMDEFAULT, FALSE, lang.menuZoomDefault.c_str());
        SetItemText(hViewMenu, IDM_VIEW_STATUSBAR, FALSE, lang.menuStatusBar.c_str());
        SetItemText(hViewMenu, IDM_VIEW_THEME_LIGHT, FALSE, lang.menuThemeLight.c_str());
        SetItemText(hViewMenu, IDM_VIEW_THEME_DARK, FALSE, lang.menuThemeDark.c_str());
        SetItemText(hViewMenu, IDM_VIEW_THEME_MATRIX, FALSE, lang.menuThemeMatrix.c_str());
        SetItemText(hViewMenu, IDM_VIEW_SHOWSPECIAL, FALSE, lang.menuShowSpecial.c_str());
        SetItemText(hViewMenu, IDM_VIEW_LINENUMBERS, FALSE, lang.menuLineNumbers.c_str());
        SetItemText(hViewMenu, IDM_VIEW_TRANSPARENCY, FALSE, lang.menuTransparency.c_str());
        SetItemText(hViewMenu, IDM_VIEW_ALWAYSONTOP, FALSE, lang.menuAlwaysOnTop.c_str());

        // Language popup — no command ID, locate it by the ID of its
        // first child. Order in the .rc: EN, PL, JA, DE, CS, UK, LT, RU, ZH.
        int langIdx = FindNestedPopup(hViewMenu, IDM_VIEW_LANG_EN);
        if (langIdx >= 0)
        {
            SetItemText(hViewMenu, langIdx, TRUE, lang.menuLanguage.c_str());
            HMENU hLangMenu = GetSubMenu(hViewMenu, langIdx);
            if (hLangMenu)
            {
                // Autonyms — each language named in itself, never
                // translated. Otherwise, set the UI to e.g. Russian and
                // good luck spotting which entry is Polish.
                SetItemText(hLangMenu, IDM_VIEW_LANG_EN, FALSE, L"English");
                SetItemText(hLangMenu, IDM_VIEW_LANG_PL, FALSE, L"Polski");
                SetItemText(hLangMenu, IDM_VIEW_LANG_JA, FALSE, L"日本語");
                SetItemText(hLangMenu, IDM_VIEW_LANG_DE, FALSE, L"Deutsch");
                SetItemText(hLangMenu, IDM_VIEW_LANG_CS, FALSE, L"Čeština");
                SetItemText(hLangMenu, IDM_VIEW_LANG_UK, FALSE, L"Українська");
                SetItemText(hLangMenu, IDM_VIEW_LANG_LT, FALSE, L"Lietuvių");
                SetItemText(hLangMenu, IDM_VIEW_LANG_RU, FALSE, L"Русский");
                SetItemText(hLangMenu, IDM_VIEW_LANG_ZH, FALSE, L"简体中文");
            }
        }
    }

    if (toolsIdx >= 0)
    {
        HMENU hToolsMenu = GetSubMenu(hMenu, toolsIdx);
        if (hToolsMenu)
        {
            SetItemText(hMenu, toolsIdx, TRUE, lang.menuTools.c_str());
            SetItemText(hToolsMenu, IDM_TOOLS_NORMALIZE, FALSE, lang.menuToolsNormalize.c_str());
            SetItemText(hToolsMenu, IDM_TOOLS_BASE64, FALSE, lang.menuToolsBase64.c_str());
            SetItemText(hToolsMenu, IDM_TOOLS_BASE64_DECODE, FALSE, lang.menuToolsBase64Decode.c_str());
            SetItemText(hToolsMenu, IDM_TOOLS_SHA1, FALSE, lang.menuToolsSha1.c_str());
            SetItemText(hToolsMenu, IDM_TOOLS_MD5, FALSE, lang.menuToolsMd5.c_str());
            SetItemText(hToolsMenu, IDM_TOOLS_UPPERCASE, FALSE, lang.menuToolsUppercase.c_str());
            SetItemText(hToolsMenu, IDM_TOOLS_LOWERCASE, FALSE, lang.menuToolsLowercase.c_str());
            SetItemText(hToolsMenu, IDM_TOOLS_TITLECASE, FALSE, lang.menuToolsTitleCase.c_str());
            SetItemText(hToolsMenu, IDM_TOOLS_TRIMTRAILING, FALSE, lang.menuToolsTrimTrailing.c_str());
            SetItemText(hToolsMenu, IDM_TOOLS_TABS2SPACES, FALSE, lang.menuToolsTabsToSpaces.c_str());
            SetItemText(hToolsMenu, IDM_TOOLS_SPACES2TABS, FALSE, lang.menuToolsSpacesToTabs.c_str());
            SetItemText(hToolsMenu, IDM_TOOLS_URLENCODE, FALSE, lang.menuToolsUrlEncode.c_str());
            SetItemText(hToolsMenu, IDM_TOOLS_URLDECODE, FALSE, lang.menuToolsUrlDecode.c_str());
            SetItemText(hToolsMenu, IDM_TOOLS_SORTASC, FALSE, lang.menuToolsSortAsc.c_str());
            SetItemText(hToolsMenu, IDM_TOOLS_SORTDESC, FALSE, lang.menuToolsSortDesc.c_str());
            SetItemText(hToolsMenu, IDM_TOOLS_REVERSELINES, FALSE, lang.menuToolsReverseLines.c_str());
            SetItemText(hToolsMenu, IDM_TOOLS_JOINLINES, FALSE, lang.menuToolsJoinLines.c_str());
            SetItemText(hToolsMenu, IDM_TOOLS_REMOVEEMPTY, FALSE, lang.menuToolsRemoveEmpty.c_str());
            SetItemText(hToolsMenu, IDM_TOOLS_REMOVEDUPES, FALSE, lang.menuToolsRemoveDupes.c_str());
        }
    }

    if (helpIdx >= 0)
    {
        HMENU hHelpMenu = GetSubMenu(hMenu, helpIdx);
        if (hHelpMenu)
            SetItemText(hHelpMenu, 0, TRUE, lang.menuAbout.c_str());
    }

    // Refresh the dark-mode cache for top-level items. The SetItemText
    // calls above invoke SetMenuItemInfoW(MIIM_STRING) which doesn't
    // reach our owner-draw renderer when items are MFT_OWNERDRAW —
    // we'd be left with stale labels (or none at all) after a language
    // change in dark mode. Pull the labels straight from the current
    // lang strings here. Then re-run EnableOwnerDrawMenuBar so the
    // synthetic wIDs catch up to any positional shifts (Tools toggle
    // inserting / removing the Tools popup).
    for (int i = 0; i < 32; ++i)
        g_topLevelText[i].clear();
    g_topLevelText[0] = lang.menuFile;
    g_topLevelText[1] = lang.menuEdit;
    g_topLevelText[2] = lang.menuFormat;
    g_topLevelText[3] = lang.menuView;
    if (toolsIdx >= 0)
        g_topLevelText[toolsIdx] = lang.menuTools;
    if (helpIdx >= 0)
        g_topLevelText[helpIdx] = lang.menuHelp;
    EnableOwnerDrawMenuBar(g_hwndMain, IsDarkMode());

    DrawMenuBar(g_hwndMain);
}

void UpdateLanguageMenu()
{
    HMENU hMenu = GetMenu(g_hwndMain);
    if (!hMenu)
        return;

    HMENU hViewMenu = GetSubMenu(hMenu, 3);
    if (!hViewMenu)
        return;
    int langIdx = FindNestedPopup(hViewMenu, IDM_VIEW_LANG_EN);
    if (langIdx < 0)
        return;
    HMENU hLangMenu = GetSubMenu(hViewMenu, langIdx);
    if (!hLangMenu)
        return;

    // The nine language command IDs are contiguous (IDM_VIEW_LANG_EN ..
    // IDM_VIEW_LANG_ZH) and laid out in LangID enum order, so the active
    // item's ID is simply base + enum value. CheckMenuRadioItem ticks it
    // and clears the rest of the group.
    UINT activeId = IDM_VIEW_LANG_EN + static_cast<UINT>(GetCurrentLanguage());
    CheckMenuRadioItem(hLangMenu, IDM_VIEW_LANG_EN, IDM_VIEW_LANG_ZH,
                       activeId, MF_BYCOMMAND);
}

bool IsTopLevelMenuItemId(UINT id)
{
    return id >= IDM_TOPLEVEL_BASE && id <= IDM_TOPLEVEL_MAX;
}

void EnableOwnerDrawMenuBar(HWND hwnd, bool enable)
{
    HMENU hMenu = GetMenu(hwnd);
    if (!hMenu)
        return;
    // Sample native menu bar item dimensions before any conversion so
    // dark-mode owner-draw items match the size light-mode items would
    // have had natively. No-op after the first successful sample.
    SampleNativeMenuBarMetrics(hwnd);
    int count = GetMenuItemCount(hMenu);
    for (int i = 0; i < count; ++i)
    {
        UINT id = GetMenuItemID(hMenu, i);
        if (IsQuickIconId(id))
            continue;
        HMENU hSub = GetSubMenu(hMenu, i);
        if (!hSub)
            continue; // top-level popups only

        MENUITEMINFOW miiCur = {};
        miiCur.cbSize = sizeof(miiCur);
        miiCur.fMask = MIIM_FTYPE;
        if (!GetMenuItemInfoW(hMenu, i, TRUE, &miiCur))
            continue;
        bool currentlyOwnerDraw = (miiCur.fType & MFT_OWNERDRAW) != 0;

        // Capture the label. While the item is still MFT_STRING the
        // system holds the string; once MFT_OWNERDRAW it doesn't, so we
        // keep our own cache.
        if (!currentlyOwnerDraw && i < 32)
        {
            wchar_t buf[128] = {};
            MENUITEMINFOW miiText = {};
            miiText.cbSize = sizeof(miiText);
            miiText.fMask = MIIM_STRING;
            miiText.dwTypeData = buf;
            miiText.cch = 127;
            GetMenuItemInfoW(hMenu, i, TRUE, &miiText);
            g_topLevelText[i] = buf;
        }
        std::wstring text = (i < 32) ? g_topLevelText[i] : std::wstring();

        // Rebuild the item: RemoveMenu + InsertMenuItemW. A plain
        // SetMenuItemInfo fType flip does NOT make Windows re-issue
        // WM_MEASUREITEM — it caches the very first measurement and
        // never asks again. Recreating the item is the only reliable
        // way to force a fresh measure, which we need once the gutter
        // size (g_measureExtra) has been learned. RemoveMenu leaves the
        // submenu handle alive so we can re-attach it.
        RemoveMenu(hMenu, i, MF_BYPOSITION);
        MENUITEMINFOW miiNew = {};
        miiNew.cbSize = sizeof(miiNew);
        miiNew.fMask = MIIM_FTYPE | MIIM_ID | MIIM_SUBMENU | MIIM_STRING;
        miiNew.fType = enable ? MFT_OWNERDRAW : MFT_STRING;
        miiNew.wID = enable ? static_cast<UINT>(IDM_TOPLEVEL_BASE + i) : 0;
        miiNew.hSubMenu = hSub;
        miiNew.dwTypeData = const_cast<LPWSTR>(text.c_str());
        InsertMenuItemW(hMenu, i, TRUE, &miiNew);
    }
    DrawMenuBar(hwnd);
}

// Read the menu item's display string from our dark-mode cache. The
// regular GetMenuItemInfoW(... MIIM_STRING) returns empty for items that
// have MFT_OWNERDRAW set on current Win11 builds, which is exactly the
// situation we're in.
static const wchar_t *GetTopLevelCachedText(UINT itemID)
{
    int idx = static_cast<int>(itemID) - IDM_TOPLEVEL_BASE;
    if (idx < 0 || idx >= 32)
        return L"";
    return g_topLevelText[idx].c_str();
}

void MeasureTopLevelMenuItem(MEASUREITEMSTRUCT *mis)
{
    if (!mis || mis->CtlType != ODT_MENU)
        return;
    const wchar_t *szText = GetTopLevelCachedText(mis->itemID);

    NONCLIENTMETRICSW ncm = {};
    ncm.cbSize = sizeof(ncm);
    SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0);
    HDC hdc = GetDC(g_hwndMain);
    HFONT hFont = CreateFontIndirectW(&ncm.lfMenuFont);
    HFONT hOldFont = reinterpret_cast<HFONT>(SelectObject(hdc, hFont));
    // Same DT_CALCRECT measurement Windows would use natively — strips
    // the "&" mnemonic prefix from the width so the result matches what
    // the eventual DrawTextW paint will render.
    RECT rcCalc = {0, 0, 0, 0};
    DrawTextW(hdc, szText, -1, &rcCalc, DT_CALCRECT | DT_SINGLELINE);
    SelectObject(hdc, hOldFont);
    DeleteObject(hFont);
    ReleaseDC(g_hwndMain, hdc);
    int textWidth = rcCalc.right - rcCalc.left;

    // Use the padding & height we sampled from Windows' native rendering
    // (before any item was flipped to MFT_OWNERDRAW). Fallback to a
    // sensible default if sampling didn't yield a value — e.g. the menu
    // bar wasn't laid out yet when we tried.
    int padding = g_menuBarPadding > 0 ? g_menuBarPadding : 12;
    int height = g_menuBarHeight > 0 ? g_menuBarHeight : GetSystemMetrics(SM_CYMENU);

    // desired = the on-screen width we want, matching the native bar.
    int desired = textWidth + padding;
    int idx = static_cast<int>(mis->itemID) - IDM_TOPLEVEL_BASE;
    if (idx >= 0 && idx < 32)
        g_topLevelDesired[idx] = desired;
    // Windows pads whatever we return here by a fixed gutter. Until the
    // first WM_DRAWITEM has measured that gutter (g_measureExtra), we
    // return the full desired width — the item shows up too wide for a
    // single frame, then the re-measure corrects it.
    int itemWidth = desired;
    if (g_measureExtra >= 0)
        itemWidth = desired - g_measureExtra;
    if (itemWidth < 1)
        itemWidth = 1;
    mis->itemWidth = itemWidth;
    mis->itemHeight = height;
}

void DrawTopLevelMenuItem(const DRAWITEMSTRUCT *dis)
{
    if (!dis || dis->CtlType != ODT_MENU)
        return;
    const wchar_t *szText = GetTopLevelCachedText(dis->itemID);

    // Learn the hidden gutter Windows adds to MEASUREITEM widths. On the
    // very first draw g_measureExtra is still -1, MEASUREITEM returned
    // the full desired width, so the rcItem we got = desired + gutter.
    // Capture the difference once, then ask the bar to rebuild so every
    // item is re-measured with the correction applied.
    if (g_measureExtra < 0)
    {
        int idx = static_cast<int>(dis->itemID) - IDM_TOPLEVEL_BASE;
        if (idx >= 0 && idx < 32 && g_topLevelDesired[idx] > 0)
        {
            int actualWidth = dis->rcItem.right - dis->rcItem.left;
            int extra = actualWidth - g_topLevelDesired[idx];
            if (extra >= 0 && extra <= 64)
            {
                g_measureExtra = extra;
                PostMessageW(g_hwndMain, WM_MENU_REMEASURE, 0, 0);
            }
        }
    }

    bool hot = (dis->itemState & ODS_HOTLIGHT) != 0;
    bool selected = (dis->itemState & ODS_SELECTED) != 0;

    // Dark-mode palette only — we only flip items to MFT_OWNERDRAW when
    // dark mode is active. Light mode leaves items system-painted and
    // this function isn't reached.
    COLORREF bg = RGB(45, 45, 45);
    if (hot || selected)
        bg = RGB(65, 65, 65);
    COLORREF textColor = RGB(255, 255, 255);

    HBRUSH hbr = CreateSolidBrush(bg);
    FillRect(dis->hDC, &dis->rcItem, hbr);
    DeleteObject(hbr);

    NONCLIENTMETRICSW ncm = {};
    ncm.cbSize = sizeof(ncm);
    SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0);
    HFONT hFont = CreateFontIndirectW(&ncm.lfMenuFont);
    HFONT hOldFont = reinterpret_cast<HFONT>(SelectObject(dis->hDC, hFont));
    SetBkMode(dis->hDC, TRANSPARENT);
    SetTextColor(dis->hDC, textColor);
    RECT rcText = dis->rcItem;
    DrawTextW(dis->hDC, szText, -1, &rcText, DT_CENTER | DT_SINGLELINE | DT_VCENTER);
    SelectObject(dis->hDC, hOldFont);
    DeleteObject(hFont);
}
