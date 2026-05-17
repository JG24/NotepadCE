#include "menu.h"
#include "core/globals.h"
#include "resource.h"
#include "lang/lang.h"

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
        SetItemText(hFileMenu, 0, TRUE, lang.menuNew.c_str());
        SetItemText(hFileMenu, 1, TRUE, lang.menuOpen.c_str());
        SetItemText(hFileMenu, 2, TRUE, lang.menuSave.c_str());
        SetItemText(hFileMenu, 3, TRUE, lang.menuSaveAs.c_str());
        SetItemText(hFileMenu, 5, TRUE, lang.menuPrint.c_str());
        SetItemText(hFileMenu, 6, TRUE, lang.menuPageSetup.c_str());
        SetItemText(hFileMenu, 8, TRUE, lang.menuExit.c_str());
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
        }
    }

    HMENU hFormatMenu = GetSubMenu(hMenu, 2);
    if (hFormatMenu)
    {
        SetItemText(hFormatMenu, 0, TRUE, lang.menuWordWrap.c_str());
        SetItemText(hFormatMenu, 1, TRUE, lang.menuFont.c_str());
    }

    HMENU hViewMenu = GetSubMenu(hMenu, 3);
    if (hViewMenu)
    {
        // View positions: 0..2 zoom, 3 sep, 4 status, 5 dark, 6 special,
        // 7 line numbers, 8 Language (popup), 9 sep, 10 transparency, 11 always on top.
        SetItemText(hViewMenu, 0, TRUE, lang.menuZoomIn.c_str());
        SetItemText(hViewMenu, 1, TRUE, lang.menuZoomOut.c_str());
        SetItemText(hViewMenu, 2, TRUE, lang.menuZoomDefault.c_str());
        SetItemText(hViewMenu, 4, TRUE, lang.menuStatusBar.c_str());
        SetItemText(hViewMenu, 5, TRUE, lang.menuDarkMode.c_str());
        SetItemText(hViewMenu, 6, TRUE, lang.menuShowSpecial.c_str());
        SetItemText(hViewMenu, 7, TRUE, lang.menuLineNumbers.c_str());

        HMENU hLangMenu = GetSubMenu(hViewMenu, 8);
        if (hLangMenu)
        {
            SetItemText(hViewMenu, 8, TRUE, lang.menuLanguage.c_str());
            // Order: EN, PL, JA (per user request, JA at end).
            SetItemText(hLangMenu, 0, TRUE, lang.menuLangEnglish.c_str());
            SetItemText(hLangMenu, 1, TRUE, lang.menuLangPolish.c_str());
            SetItemText(hLangMenu, 2, TRUE, lang.menuLangJapanese.c_str());
        }

        SetItemText(hViewMenu, 10, TRUE, lang.menuTransparency.c_str());
        SetItemText(hViewMenu, 11, TRUE, lang.menuAlwaysOnTop.c_str());
    }

    if (toolsIdx >= 0)
    {
        HMENU hToolsMenu = GetSubMenu(hMenu, toolsIdx);
        if (hToolsMenu)
        {
            SetItemText(hMenu, toolsIdx, TRUE, lang.menuTools.c_str());
            SetItemText(hToolsMenu, 0, TRUE, lang.menuToolsNormalize.c_str());
            SetItemText(hToolsMenu, 1, TRUE, lang.menuToolsBase64.c_str());
            SetItemText(hToolsMenu, 2, TRUE, lang.menuToolsSha1.c_str());
            SetItemText(hToolsMenu, 3, TRUE, lang.menuToolsMd5.c_str());
        }
    }

    if (helpIdx >= 0)
    {
        HMENU hHelpMenu = GetSubMenu(hMenu, helpIdx);
        if (hHelpMenu)
            SetItemText(hHelpMenu, 0, TRUE, lang.menuAbout.c_str());
    }

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

    LangID currentLang = GetCurrentLanguage();
    CheckMenuItem(hLangMenu, IDM_VIEW_LANG_EN, (currentLang == LangID::EN) ? MF_CHECKED : MF_UNCHECKED);
    CheckMenuItem(hLangMenu, IDM_VIEW_LANG_JA, (currentLang == LangID::JA) ? MF_CHECKED : MF_UNCHECKED);
    CheckMenuItem(hLangMenu, IDM_VIEW_LANG_PL, (currentLang == LangID::PL) ? MF_CHECKED : MF_UNCHECKED);
}
