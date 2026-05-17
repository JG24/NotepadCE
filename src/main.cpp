/*
   ▄████████  ▄██████▄     ▄████████  ▄█        ▄██████▄   ▄██████▄     ▄███████▄
  ███    ███ ███    ███   ███    ███ ███       ███    ███ ███    ███   ███    ███
  ███    █▀  ███    ███   ███    ███ ███       ███    ███ ███    ███   ███    ███
 ▄███▄▄▄     ███    ███  ▄███▄▄▄▄██▀ ███       ███    ███ ███    ███   ███    ███
▀▀███▀▀▀     ███    ███ ▀▀███▀▀▀▀▀   ███       ███    ███ ███    ███ ▀█████████▀
  ███        ███    ███ ▀███████████ ███       ███    ███ ███    ███   ███
  ███        ███    ███   ███    ███ ███▌    ▄ ███    ███ ███    ███   ███
  ███         ▀██████▀    ███    ███ █████▄▄██  ▀██████▀   ▀██████▀   ▄████▀
                          ███    ███ ▀

  Main entry point and window procedure for Legacy Notepad text editor application.
  Coordinates all modules and handles Windows message loop and command dispatching.
*/

#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <commdlg.h>
#include <richedit.h>
#include <shlwapi.h>
#include <shellapi.h>
#include <dwmapi.h>
#include <uxtheme.h>

#include "resource.h"
#include "core/types.h"
#include "core/globals.h"
#include "modules/theme.h"
#include "modules/editor.h"
#include "modules/file.h"
#include "modules/ui.h"
#include "modules/dialog.h"
#include "modules/commands.h"
#include "modules/settings.h"
#include "modules/menu.h"
#include "modules/spellchecker.h"
#include "modules/tools.h"
#include "modules/gutter.h"
#include "modules/quickicons.h"
#include "lang/lang.h"

static bool g_ncMouseTracking = false;

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (g_msgFindReplace && msg == g_msgFindReplace)
    {
        HandleFindReplaceMessage(reinterpret_cast<LPFINDREPLACEW>(lParam));
        return 0;
    }
    switch (msg)
    {
    case WM_CREATE:
    {
        g_hwndMain = hwnd;
        DragAcceptFiles(hwnd, TRUE);
        const wchar_t *richEditClass = nullptr;
        HMODULE hRichEdit = nullptr;
        hRichEdit = LoadLibraryW(L"Msftedit.dll");
        if (hRichEdit)
        {
            richEditClass = MSFTEDIT_CLASS;
        }
        else
        {
            hRichEdit = LoadLibraryW(L"Riched20.dll");
            if (hRichEdit)
            {
                richEditClass = RICHEDIT_CLASSW;
            }
            else
            {
                MessageBoxW(hwnd,
                            L"Cannot load RichEdit control.\n",
                            L"Error", MB_ICONERROR | MB_OK);
                return -1;
            }
        }
        g_hwndEditor = CreateWindowExW(0, richEditClass, nullptr,
                                       WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_HSCROLL | ES_MULTILINE | ES_AUTOVSCROLL | ES_AUTOHSCROLL | ES_WANTRETURN | ES_NOHIDESEL,
                                       0, 0, 100, 100, hwnd, reinterpret_cast<HMENU>(IDC_EDITOR), GetModuleHandleW(nullptr), nullptr);
        g_origEditorProc = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(g_hwndEditor, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(EditorSubclassProc)));
        CreateGutterWindow(hwnd);
        g_hwndStatus = CreateWindowExW(0, STATUSCLASSNAMEW, nullptr,
                                       WS_CHILD | WS_VISIBLE | SBARS_SIZEGRIP, 0, 0, 0, 0, hwnd, reinterpret_cast<HMENU>(IDC_STATUSBAR), GetModuleHandleW(nullptr), nullptr);
        g_origStatusProc = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(g_hwndStatus, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(StatusSubclassProc)));
        SendMessageW(g_hwndEditor, EM_EXLIMITTEXT, 0, static_cast<LPARAM>(-1));
        SendMessageW(g_hwndEditor, EM_SETEVENTMASK, 0, ENM_CHANGE | ENM_SELCHANGE);
        ApplyFont();
        if (g_state.wordWrap)
        {
            ApplyWordWrap();
            g_state.modified = false;
        }
        SetupStatusBarParts();
        UpdateMenuStrings();
        UpdateLanguageMenu();
        UpdateToolsMenuVisibility();
        UpdateQuickIconsVisibility();
        if (g_state.alwaysOnTop)
            SetWindowPos(g_hwndMain, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
        if (g_state.windowOpacity != 255)
        {
            SetWindowLongW(g_hwndMain, GWL_EXSTYLE, GetWindowLongW(g_hwndMain, GWL_EXSTYLE) | WS_EX_LAYERED);
            SetLayeredWindowAttributes(g_hwndMain, 0, g_state.windowOpacity, LWA_ALPHA);
        }
        UpdateTitle();
        UpdateStatus();
        ApplyTheme();
        // CheckMenuItem MUST run after ApplyTheme: theme application
        // calls flushMenuThemes / setPreferredAppMode which can clear
        // check marks on toggleable items in the menu.
        {
            HMENU hMenu = GetMenu(g_hwndMain);
            CheckMenuItem(hMenu, IDM_VIEW_ALWAYSONTOP, g_state.alwaysOnTop ? MF_CHECKED : MF_UNCHECKED);
            CheckMenuItem(hMenu, IDM_VIEW_STATUSBAR, g_state.showStatusBar ? MF_CHECKED : MF_UNCHECKED);
            CheckMenuItem(hMenu, IDM_FORMAT_WORDWRAP, g_state.wordWrap ? MF_CHECKED : MF_UNCHECKED);
            CheckMenuItem(hMenu, IDM_EDIT_SETTINGS_SPELLCHECK, g_state.spellCheckEnabled ? MF_CHECKED : MF_UNCHECKED);
            if (!IsSpellCheckAvailable())
                EnableMenuItem(hMenu, IDM_EDIT_SETTINGS_SPELLCHECK, MF_BYCOMMAND | MF_GRAYED);
            CheckMenuItem(hMenu, IDM_VIEW_SHOWSPECIAL, g_state.showSpecialChars ? MF_CHECKED : MF_UNCHECKED);
            CheckMenuItem(hMenu, IDM_VIEW_LINENUMBERS, g_state.showLineNumbers ? MF_CHECKED : MF_UNCHECKED);
            CheckMenuItem(hMenu, IDM_EDIT_SETTINGS_TOOLS, g_state.toolsEnabled ? MF_CHECKED : MF_UNCHECKED);
            CheckMenuItem(hMenu, IDM_EDIT_SETTINGS_QUICKICONS, g_state.quickAccessIcons ? MF_CHECKED : MF_UNCHECKED);
        }
        SetFocus(g_hwndEditor);
        return 0;
    }
    case WM_UAHDRAWMENU:
    {
        if (IsDarkMode())
        {
            UAHMENU *pUDM = reinterpret_cast<UAHMENU *>(lParam);
            MENUBARINFO mbi = {};
            mbi.cbSize = sizeof(mbi);
            if (GetMenuBarInfo(hwnd, OBJID_MENU, 0, &mbi))
            {
                RECT rcWindow;
                GetWindowRect(hwnd, &rcWindow);
                RECT rcMenuBar = mbi.rcBar;
                OffsetRect(&rcMenuBar, -rcWindow.left, -rcWindow.top);
                FillRect(pUDM->hdc, &rcMenuBar, g_hbrMenuDark ? g_hbrMenuDark : reinterpret_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
            }
            return TRUE;
        }
        break;
    }
    case WM_UAHDRAWMENUITEM:
    {
        if (IsDarkMode())
        {
            UAHDRAWMENUITEM *pUDMI = reinterpret_cast<UAHDRAWMENUITEM *>(lParam);
            wchar_t szText[256] = {};
            MENUITEMINFOW mii = {};
            mii.cbSize = sizeof(mii);
            mii.fMask = MIIM_STRING;
            mii.dwTypeData = szText;
            mii.cch = 255;
            GetMenuItemInfoW(pUDMI->um.hMenu, pUDMI->umi.iPosition, TRUE, &mii);
            COLORREF bgColor = RGB(45, 45, 45);
            COLORREF textColor = RGB(255, 255, 255);
            if ((pUDMI->dis.itemState & ODS_HOTLIGHT) || (pUDMI->dis.itemState & ODS_SELECTED))
                bgColor = RGB(65, 65, 65);
            HBRUSH hbr = CreateSolidBrush(bgColor);
            FillRect(pUDMI->um.hdc, &pUDMI->dis.rcItem, hbr);
            DeleteObject(hbr);
            NONCLIENTMETRICSW ncm = {};
            ncm.cbSize = sizeof(ncm);
            SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0);
            HFONT hFont = CreateFontIndirectW(&ncm.lfMenuFont);
            HFONT hOldFont = reinterpret_cast<HFONT>(SelectObject(pUDMI->um.hdc, hFont));
            SetBkMode(pUDMI->um.hdc, TRANSPARENT);
            SetTextColor(pUDMI->um.hdc, textColor);
            RECT rcText = pUDMI->dis.rcItem;
            DrawTextW(pUDMI->um.hdc, szText, -1, &rcText, DT_CENTER | DT_SINGLELINE | DT_VCENTER);
            SelectObject(pUDMI->um.hdc, hOldFont);
            DeleteObject(hFont);
            return TRUE;
        }
        break;
    }
    case WM_NCPAINT:
    case WM_NCACTIVATE:
    {
        LRESULT result = DefWindowProcW(hwnd, msg, wParam, lParam);
        if (IsDarkMode())
        {
            HDC hdc = GetWindowDC(hwnd);
            MENUBARINFO mbi = {};
            mbi.cbSize = sizeof(mbi);
            if (GetMenuBarInfo(hwnd, OBJID_MENU, 0, &mbi))
            {
                RECT rcWindow;
                GetWindowRect(hwnd, &rcWindow);
                RECT rcMenuBar = mbi.rcBar;
                OffsetRect(&rcMenuBar, -rcWindow.left, -rcWindow.top);
                rcMenuBar.bottom += 2;
                FillRect(hdc, &rcMenuBar, g_hbrMenuDark ? g_hbrMenuDark : reinterpret_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
                HMENU hMenu = GetMenu(hwnd);
                int itemCount = GetMenuItemCount(hMenu);
                NONCLIENTMETRICSW ncm = {};
                ncm.cbSize = sizeof(ncm);
                SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0);
                HFONT hFont = CreateFontIndirectW(&ncm.lfMenuFont);
                HFONT hOldFont = reinterpret_cast<HFONT>(SelectObject(hdc, hFont));
                SetBkMode(hdc, TRANSPARENT);
                SetTextColor(hdc, RGB(255, 255, 255));
                for (int i = 0; i < itemCount; i++)
                {
                    RECT rcItem;
                    if (GetMenuBarInfo(hwnd, OBJID_MENU, i + 1, &mbi))
                    {
                        // Skip owner-drawn items (the quick-access icons) —
                        // they get WM_DRAWITEM instead and we must not paint
                        // text over them here.
                        MENUITEMINFOW miiType = {};
                        miiType.cbSize = sizeof(miiType);
                        miiType.fMask = MIIM_FTYPE;
                        if (GetMenuItemInfoW(hMenu, i, TRUE, &miiType) &&
                            (miiType.fType & MFT_OWNERDRAW))
                        {
                            DRAWITEMSTRUCT dis = {};
                            dis.CtlType = ODT_MENU;
                            dis.itemID = GetMenuItemID(hMenu, i);
                            dis.hDC = hdc;
                            dis.rcItem = mbi.rcBar;
                            OffsetRect(&dis.rcItem, -rcWindow.left, -rcWindow.top);
                            DrawQuickIconItem(&dis);
                            continue;
                        }
                        rcItem = mbi.rcBar;
                        OffsetRect(&rcItem, -rcWindow.left, -rcWindow.top);
                        wchar_t szText[256] = {};
                        MENUITEMINFOW mii = {};
                        mii.cbSize = sizeof(mii);
                        mii.fMask = MIIM_STRING;
                        mii.dwTypeData = szText;
                        mii.cch = 255;
                        GetMenuItemInfoW(hMenu, i, TRUE, &mii);
                        DrawTextW(hdc, szText, -1, &rcItem, DT_CENTER | DT_SINGLELINE | DT_VCENTER);
                    }
                }
                SelectObject(hdc, hOldFont);
                DeleteObject(hFont);
            }
            ReleaseDC(hwnd, hdc);
        }
        return result;
    }
    case WM_SETTINGCHANGE:
    {
        if (lParam && wcscmp(reinterpret_cast<LPCWSTR>(lParam), L"ImmersiveColorSet") == 0)
            ApplyTheme();
        return 0;
    }
    case WM_NCMOUSEMOVE:
    {
        // Subscribe to WM_NCMOUSELEAVE so we can clear stale menu-bar
        // hover state when the cursor exits the non-client area. On Win11
        // the previously-hovered menu item can retain a light hover
        // background even after the mouse moves elsewhere — DrawMenuBar
        // on leave forces a clean redraw via our WM_UAHDRAWMENU* path.
        if (!g_ncMouseTracking)
        {
            TRACKMOUSEEVENT tme = {};
            tme.cbSize = sizeof(tme);
            tme.dwFlags = TME_NONCLIENT | TME_LEAVE;
            tme.hwndTrack = hwnd;
            TrackMouseEvent(&tme);
            g_ncMouseTracking = true;
        }
        break;
    }
    case WM_NCMOUSELEAVE:
    {
        g_ncMouseTracking = false;
        DrawMenuBar(hwnd);
        return 0;
    }
    case WM_ERASEBKGND:
    {
        if (IsDarkMode())
        {
            HDC hdc = reinterpret_cast<HDC>(wParam);
            RECT rc;
            GetClientRect(hwnd, &rc);
            HBRUSH hbr = CreateSolidBrush(RGB(30, 30, 30));
            FillRect(hdc, &rc, hbr);
            DeleteObject(hbr);
            return 1;
        }
        break;
    }
    case WM_DROPFILES:
    {
        HDROP hDrop = reinterpret_cast<HDROP>(wParam);
        wchar_t path[MAX_PATH];
        if (DragQueryFileW(hDrop, 0, path, MAX_PATH))
        {
            if (ConfirmDiscard())
                LoadFile(path);
        }
        DragFinish(hDrop);
        return 0;
    }
    case WM_SIZE:
        ResizeControls();
        UpdateStatus();
        return 0;
    case WM_TIMER:
        if (wParam == SPELL_CHECK_TIMER_ID)
        {
            KillTimer(hwnd, SPELL_CHECK_TIMER_ID);
            // Don't apply spell marks while the user is drag-selecting:
            // EM_EXSETSEL inside ApplySpellingMarks would clobber the
            // in-progress selection. Reschedule so it runs once the mouse
            // is released.
            if (GetKeyState(VK_LBUTTON) & 0x8000)
                SetTimer(hwnd, SPELL_CHECK_TIMER_ID, SPELL_CHECK_DEBOUNCE_MS, nullptr);
            else
                ApplySpellingMarks();
            return 0;
        }
        break;
    case WM_SETFOCUS:
        SetFocus(g_hwndEditor);
        return 0;
    case WM_CTLCOLORSTATIC:
        if (reinterpret_cast<HWND>(lParam) == g_hwndStatus && IsDarkMode())
        {
            HDC hdc = reinterpret_cast<HDC>(wParam);
            SetTextColor(hdc, RGB(255, 255, 255));
            SetBkColor(hdc, RGB(45, 45, 45));
            return reinterpret_cast<LRESULT>(g_hbrStatusDark ? g_hbrStatusDark : GetStockObject(BLACK_BRUSH));
        }
        break;
    case WM_MEASUREITEM:
    {
        LPMEASUREITEMSTRUCT pMIS = reinterpret_cast<LPMEASUREITEMSTRUCT>(lParam);
        if (pMIS->CtlType == ODT_MENU && IsQuickIconId(static_cast<UINT>(pMIS->itemID)))
        {
            MeasureQuickIconItem(pMIS);
            return TRUE;
        }
        break;
    }
    case WM_DRAWITEM:
    {
        LPDRAWITEMSTRUCT pDIS = reinterpret_cast<LPDRAWITEMSTRUCT>(lParam);
        if (pDIS->CtlType == ODT_MENU && IsQuickIconId(static_cast<UINT>(pDIS->itemID)))
        {
            DrawQuickIconItem(pDIS);
            return TRUE;
        }
        if (pDIS->hwndItem == g_hwndStatus && IsDarkMode())
        {
            HBRUSH hbr = g_hbrStatusDark ? g_hbrStatusDark : CreateSolidBrush(RGB(45, 45, 45));
            FillRect(pDIS->hDC, &pDIS->rcItem, hbr);
            if (!g_hbrStatusDark)
                DeleteObject(hbr);
            SetBkMode(pDIS->hDC, TRANSPARENT);
            SetTextColor(pDIS->hDC, RGB(255, 255, 255));
            int part = static_cast<int>(pDIS->itemID);
            if (part >= 0 && part < 6)
            {
                RECT rc = pDIS->rcItem;
                rc.left += 4;
                DrawTextW(pDIS->hDC, g_statusTexts[part].c_str(), -1, &rc, DT_SINGLELINE | DT_VCENTER | DT_LEFT | DT_END_ELLIPSIS);
            }
            return TRUE;
        }
        break;
    }
    case WM_COMMAND:
    {
        WORD cmd = LOWORD(wParam);
        if (cmd == IDC_EDITOR && HIWORD(wParam) == EN_CHANGE)
        {
            if (!g_state.modified)
            {
                g_state.modified = true;
                UpdateTitle();
            }
            UpdateStatus();
            if (g_state.showLineNumbers)
            {
                if (UpdateGutterWidth())
                    ResizeControls();
                if (g_hwndGutter)
                    InvalidateRect(g_hwndGutter, nullptr, FALSE);
            }
            ScheduleSpellCheck();
            return 0;
        }

        if (cmd >= IDM_FILE_RECENT_BASE && cmd < IDM_FILE_RECENT_BASE + MAX_RECENT_FILES)
        {
            int idx = cmd - IDM_FILE_RECENT_BASE;
            if (idx < static_cast<int>(g_state.recentFiles.size()))
            {
                if (ConfirmDiscard())
                    LoadFile(g_state.recentFiles[idx]);
            }
            return 0;
        }
        switch (cmd)
        {
        case IDM_FILE_NEW:
            FileNew();
            break;
        case IDM_FILE_OPEN:
            FileOpen();
            break;
        case IDM_FILE_SAVE:
            FileSave();
            break;
        case IDM_FILE_SAVEAS:
            FileSaveAs();
            break;
        case IDM_FILE_PRINT:
            FilePrint();
            break;
        case IDM_FILE_PAGESETUP:
            FilePageSetup();
            break;
        case IDM_FILE_EXIT:
            if (ConfirmDiscard())
                DestroyWindow(hwnd);
            break;
        case IDM_EDIT_UNDO:
            EditUndo();
            break;
        case IDM_EDIT_REDO:
            EditRedo();
            break;
        case IDM_EDIT_CUT:
            EditCut();
            break;
        case IDM_EDIT_COPY:
            EditCopy();
            break;
        case IDM_EDIT_PASTE:
            EditPaste();
            break;
        case IDM_EDIT_DELETE:
            EditDelete();
            break;
        case IDM_EDIT_FIND:
            EditFind();
            break;
        case IDM_EDIT_FINDNEXT:
            EditFindNext();
            break;
        case IDM_EDIT_FINDPREV:
            EditFindPrev();
            break;
        case IDM_EDIT_REPLACE:
            EditReplace();
            break;
        case IDM_EDIT_GOTO:
            EditGoto();
            break;
        case IDM_EDIT_SELECTALL:
            EditSelectAll();
            break;
        case IDM_EDIT_TIMEDATE:
            EditTimeDate();
            break;
        case IDM_EDIT_DUPLICATELINE:
            DuplicateLine();
            break;
        case IDM_EDIT_DELETELINE:
            DeleteLine();
            break;
        case IDM_FORMAT_WORDWRAP:
            FormatWordWrap();
            break;
        case IDM_FORMAT_FONT:
            FormatFont();
            break;
        case IDM_VIEW_ZOOMIN:
            ViewZoomIn();
            break;
        case IDM_VIEW_ZOOMOUT:
            ViewZoomOut();
            break;
        case IDM_VIEW_ZOOMDEFAULT:
            ViewZoomDefault();
            break;
        case IDM_VIEW_STATUSBAR:
            ViewStatusBar();
            break;
        case IDM_VIEW_DARKMODE:
            ToggleDarkMode();
            break;
        case IDM_VIEW_TRANSPARENCY:
            ViewTransparency();
            break;
        case IDM_VIEW_ALWAYSONTOP:
            ViewAlwaysOnTop();
            break;
        case IDM_VIEW_SHOWSPECIAL:
            g_state.showSpecialChars = !g_state.showSpecialChars;
            CheckMenuItem(GetMenu(g_hwndMain), IDM_VIEW_SHOWSPECIAL,
                          g_state.showSpecialChars ? MF_CHECKED : MF_UNCHECKED);
            InvalidateRect(g_hwndEditor, nullptr, TRUE);
            SaveSettings();
            break;
        case IDM_VIEW_LINENUMBERS:
            g_state.showLineNumbers = !g_state.showLineNumbers;
            CheckMenuItem(GetMenu(g_hwndMain), IDM_VIEW_LINENUMBERS,
                          g_state.showLineNumbers ? MF_CHECKED : MF_UNCHECKED);
            ResizeControls();
            InvalidateRect(g_hwndEditor, nullptr, TRUE);
            SaveSettings();
            break;
        case IDM_EDIT_SETTINGS_DATEFORMAT:
            EditSettingsDateFormat();
            break;
        case IDM_EDIT_SETTINGS_SPELLCHECK:
            g_state.spellCheckEnabled = !g_state.spellCheckEnabled;
            CheckMenuItem(GetMenu(g_hwndMain), IDM_EDIT_SETTINGS_SPELLCHECK,
                          g_state.spellCheckEnabled ? MF_CHECKED : MF_UNCHECKED);
            if (g_state.spellCheckEnabled)
                ApplySpellingMarks();
            else
                ClearSpellingMarks();
            RefreshQuickIcons();
            SaveSettings();
            break;
        case IDM_EDIT_SETTINGS_TOOLS:
            g_state.toolsEnabled = !g_state.toolsEnabled;
            CheckMenuItem(GetMenu(g_hwndMain), IDM_EDIT_SETTINGS_TOOLS,
                          g_state.toolsEnabled ? MF_CHECKED : MF_UNCHECKED);
            UpdateToolsMenuVisibility();
            UpdateMenuStrings();
            SaveSettings();
            break;
        case IDM_EDIT_SETTINGS_QUICKICONS:
            g_state.quickAccessIcons = !g_state.quickAccessIcons;
            CheckMenuItem(GetMenu(g_hwndMain), IDM_EDIT_SETTINGS_QUICKICONS,
                          g_state.quickAccessIcons ? MF_CHECKED : MF_UNCHECKED);
            UpdateQuickIconsVisibility();
            UpdateMenuStrings();
            SaveSettings();
            break;
        case IDM_QUICK_SPELLCHECK:
        case IDM_QUICK_ONTOP:
        case IDM_QUICK_DARKMODE:
            HandleQuickIconClick(cmd);
            SaveSettings();
            break;
        case IDM_TOOLS_NORMALIZE:
            ToolsNormalizeText();
            break;
        case IDM_TOOLS_BASE64:
            ToolsBase64();
            break;
        case IDM_TOOLS_SHA1:
            ToolsSha1();
            break;
        case IDM_TOOLS_MD5:
            ToolsMd5();
            break;
        case IDM_VIEW_LANG_EN:
            if (g_hwndFindDlg)
            {
                DestroyWindow(g_hwndFindDlg);
                g_hwndFindDlg = nullptr;
            }
            SetLanguage(LangID::EN);
            UpdateMenuStrings();
            UpdateLanguageMenu();
            UpdateTitle();
            UpdateStatus();
            break;
        case IDM_VIEW_LANG_JA:
            if (g_hwndFindDlg)
            {
                DestroyWindow(g_hwndFindDlg);
                g_hwndFindDlg = nullptr;
            }
            SetLanguage(LangID::JA);
            UpdateMenuStrings();
            UpdateLanguageMenu();
            UpdateTitle();
            UpdateStatus();
            break;
        case IDM_VIEW_LANG_PL:
            if (g_hwndFindDlg)
            {
                DestroyWindow(g_hwndFindDlg);
                g_hwndFindDlg = nullptr;
            }
            SetLanguage(LangID::PL);
            UpdateMenuStrings();
            UpdateLanguageMenu();
            UpdateTitle();
            UpdateStatus();
            break;
        case IDM_HELP_ABOUT:
            HelpAbout();
            break;
        }
        return 0;
    }
    case WM_NOTIFY:
    {
        NMHDR *pnmh = reinterpret_cast<NMHDR *>(lParam);
        if (pnmh->hwndFrom == g_hwndStatus && pnmh->code == NM_CUSTOMDRAW)
        {
            if (IsDarkMode())
            {
                LPNMCUSTOMDRAW lpnmcd = reinterpret_cast<LPNMCUSTOMDRAW>(lParam);
                if (lpnmcd->dwDrawStage == CDDS_PREPAINT)
                    return CDRF_NOTIFYITEMDRAW;
                if (lpnmcd->dwDrawStage == CDDS_ITEMPREPAINT)
                {
                    HBRUSH hbr = g_hbrStatusDark ? g_hbrStatusDark : CreateSolidBrush(RGB(45, 45, 45));
                    FillRect(lpnmcd->hdc, &lpnmcd->rc, hbr);
                    if (!g_hbrStatusDark && hbr)
                        DeleteObject(hbr);
                    SetBkMode(lpnmcd->hdc, TRANSPARENT);
                    SetBkColor(lpnmcd->hdc, RGB(45, 45, 45));
                    SetTextColor(lpnmcd->hdc, RGB(255, 255, 255));
                    wchar_t buf[256] = {};
                    int part = static_cast<int>(lpnmcd->dwItemSpec);
                    SendMessageW(g_hwndStatus, SB_GETTEXTW, part, reinterpret_cast<LPARAM>(buf));
                    RECT rc = lpnmcd->rc;
                    rc.left += 6;
                    DrawTextW(lpnmcd->hdc, buf, -1, &rc, DT_SINGLELINE | DT_VCENTER | DT_LEFT | DT_END_ELLIPSIS);
                    return CDRF_SKIPDEFAULT;
                }
            }
        }
        if (pnmh->hwndFrom == g_hwndEditor && pnmh->code == EN_SELCHANGE)
        {
            UpdateStatus();
        }
        return 0;
    }
    case WM_CLOSE:
        if (g_state.closing)
            return 0;
        g_state.closing = true;
        if (ConfirmDiscard())
            DestroyWindow(hwnd);
        else
            g_state.closing = false;
        return 0;
    case WM_DESTROY:
        {
            WINDOWPLACEMENT wp = {};
            wp.length = sizeof(WINDOWPLACEMENT);
            if (GetWindowPlacement(hwnd, &wp))
            {
                RECT rect = wp.rcNormalPosition;
                g_state.windowX = rect.left;
                g_state.windowY = rect.top;
                g_state.windowWidth = rect.right - rect.left;
                g_state.windowHeight = rect.bottom - rect.top;
            }
            SaveWindowSettings();
        }
        if (g_state.hFont)
        {
            DeleteObject(g_state.hFont);
            g_state.hFont = nullptr;
        }
        PostQuitMessage(0);
        return 0;
    case WM_MOUSEWHEEL:
        if (GetKeyState(VK_CONTROL) & 0x8000)
        {
            int delta = GET_WHEEL_DELTA_WPARAM(wParam);
            if (delta > 0)
                ViewZoomIn();
            else
                ViewZoomOut();
            return 0;
        }
        break;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, LPWSTR lpCmdLine, int nCmdShow)
{
    LoadFontSettings();
    LoadWindowSettings();
    InitLanguage();
    InitSpellCheck();
    typedef BOOL(WINAPI * fnSetProcessDpiAwarenessContext)(DPI_AWARENESS_CONTEXT);
    HMODULE hUser32 = GetModuleHandleW(L"user32.dll");
    if (hUser32)
    {
#ifdef __GNUC__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcast-function-type"
#endif
        auto setProcDPI = reinterpret_cast<fnSetProcessDpiAwarenessContext>(GetProcAddress(hUser32, "SetProcessDpiAwarenessContext"));
#ifdef __GNUC__
#pragma GCC diagnostic pop
#endif
        if (setProcDPI)
            setProcDPI(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    }

    g_msgFindReplace = RegisterWindowMessageW(FINDMSGSTRING);

    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.hIcon = LoadIconW(hInstance, MAKEINTRESOURCEW(IDI_NOTEPAD));
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    wc.lpszMenuName = MAKEINTRESOURCEW(IDR_MAINMENU);
    wc.lpszClassName = L"NotepadClass";
    wc.hIconSm = LoadIconW(hInstance, MAKEINTRESOURCEW(IDI_NOTEPAD));
    RegisterClassExW(&wc);
    RegisterGutterClass();

    INITCOMMONCONTROLSEX icc = {sizeof(icc), ICC_BAR_CLASSES | ICC_LINK_CLASS};
    InitCommonControlsEx(&icc);

    const auto &lang = GetLangStrings();
    std::wstring initialTitle = lang.untitled + L" - " + lang.appName;
    g_hwndMain = CreateWindowExW(0, L"NotepadClass", initialTitle.c_str(),
                                 WS_OVERLAPPEDWINDOW | WS_MAXIMIZEBOX, g_state.windowX, g_state.windowY, g_state.windowWidth, g_state.windowHeight,
                                 nullptr, nullptr, hInstance, nullptr);
    g_hAccel = LoadAcceleratorsW(hInstance, MAKEINTRESOURCEW(IDR_ACCEL));
    ShowWindow(g_hwndMain, nCmdShow);
    UpdateWindow(g_hwndMain);

    if (lpCmdLine && lpCmdLine[0])
    {
        std::wstring path = lpCmdLine;
        if (path.front() == L'"' && path.back() == L'"')
            path = path.substr(1, path.size() - 2);
        LoadFile(path);
    }
    ScheduleSpellCheck();

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0))
    {
        if (g_hwndFindDlg && IsDialogMessageW(g_hwndFindDlg, &msg))
            continue;
        if (g_hwndTransparencyDlg && IsDialogMessageW(g_hwndTransparencyDlg, &msg))
            continue;
        if (g_hwndDateFormatDlg && IsDialogMessageW(g_hwndDateFormatDlg, &msg))
            continue;
        if (g_hwndAboutDlg && IsDialogMessageW(g_hwndAboutDlg, &msg))
            continue;
        if (!TranslateAcceleratorW(g_hwndMain, g_hAccel, &msg))
        {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }

    ShutdownSpellCheck();
    return static_cast<int>(msg.wParam);
}
