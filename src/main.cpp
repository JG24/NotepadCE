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
#include "modules/snippets.h"
#include "lang/lang.h"

static bool g_ncMouseTracking = false;

// Switch UI language. Drops the modeless Find dialog first (its child
// controls were created with the old language's labels), then applies
// the new strings and rebuilds the menus, title and status bar.
static void ChangeLanguage(LangID lang)
{
    if (g_hwndFindDlg)
    {
        DestroyWindow(g_hwndFindDlg);
        g_hwndFindDlg = nullptr;
    }
    SetLanguage(lang);
    UpdateMenuStrings();
    UpdateRecentFilesMenu();
    UpdateLanguageMenu();
    UpdateTitle();
    UpdateStatus();
}

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
        CreateSnippetsWindow(hwnd);
        g_hwndStatus = CreateWindowExW(0, STATUSCLASSNAMEW, nullptr,
                                       WS_CHILD | WS_VISIBLE | SBARS_SIZEGRIP, 0, 0, 0, 0, hwnd, reinterpret_cast<HMENU>(IDC_STATUSBAR), GetModuleHandleW(nullptr), nullptr);
        g_origStatusProc = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(g_hwndStatus, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(StatusSubclassProc)));
        SendMessageW(g_hwndEditor, EM_EXLIMITTEXT, 0, static_cast<LPARAM>(-1));
        SendMessageW(g_hwndEditor, EM_SETEVENTMASK, 0, ENM_CHANGE | ENM_SELCHANGE);
        SetEditorPlainTextMode(g_hwndEditor);
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
        UpdateSnippetsVisibility();
        UpdateRecentFilesMenu();
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
            CheckMenuItem(hMenu, IDM_EDIT_SETTINGS_HIGHLIGHTLINE, g_state.highlightCurrentLine ? MF_CHECKED : MF_UNCHECKED);
            CheckMenuItem(hMenu, IDM_EDIT_SETTINGS_HIGHLIGHTWORD, g_state.highlightOccurrences ? MF_CHECKED : MF_UNCHECKED);
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
            // Defence in depth. WM_UAHDRAWMENUITEM doesn't actually fire
            // for menu bar items on current Win11 builds (Microsoft
            // changed that undocumented path), which is why this whole
            // case is effectively dead and the real painting happens via
            // owner-drawn items + WM_DRAWITEM (see menu.cpp). Left here
            // in case the message ever does come through on older builds
            // or a quirky configuration.
            COLORREF bgColor = RGB(45, 45, 45);
            COLORREF textColor = RGB(255, 255, 255);
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
                        // Owner-drawn items: dispatch by their wID range.
                        // After EnableOwnerDrawMenuBar(true) every top-level
                        // popup is MFT_OWNERDRAW with an IDM_TOPLEVEL_*
                        // synthetic ID, and the quick icons keep their
                        // IDM_QUICK_* IDs. WM_DRAWITEM fires for these later
                        // anyway, but we re-paint here so the menu bar isn't
                        // momentarily blank after our FillRect wiped it.
                        //
                        // GOTCHA: GetMenuItemID() returns (UINT)-1 for any
                        // item that opens a submenu, regardless of what we
                        // set via SetMenuItemInfoW(MIIM_ID). To recover the
                        // real wID we set on those popups, read it back
                        // through MENUITEMINFOW.wID with the MIIM_ID flag.
                        MENUITEMINFOW miiType = {};
                        miiType.cbSize = sizeof(miiType);
                        miiType.fMask = MIIM_FTYPE | MIIM_ID;
                        if (GetMenuItemInfoW(hMenu, i, TRUE, &miiType) &&
                            (miiType.fType & MFT_OWNERDRAW))
                        {
                            DRAWITEMSTRUCT dis = {};
                            dis.CtlType = ODT_MENU;
                            dis.itemID = miiType.wID;
                            dis.hDC = hdc;
                            dis.rcItem = mbi.rcBar;
                            OffsetRect(&dis.rcItem, -rcWindow.left, -rcWindow.top);
                            if (IsQuickIconId(dis.itemID))
                                DrawQuickIconItem(&dis);
                            else if (IsTopLevelMenuItemId(dis.itemID))
                                DrawTopLevelMenuItem(&dis);
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
    case WM_MENU_REMEASURE:
    {
        // The first WM_DRAWITEM has just learned the hidden gutter
        // Windows adds to owner-drawn menu item widths. Rebuild the bar
        // so every item gets re-measured with the correction applied.
        if (IsDarkMode())
            EnableOwnerDrawMenuBar(hwnd, true);
        return 0;
    }
    case WM_EXITMENULOOP:
    {
        // When a submenu was opened, Win11 puts the menu bar into a
        // "navigation mode" that paints other items via a system path
        // bypassing WM_UAHDRAWMENU* — leaving them in light-theme hover
        // colours that stick around after the submenu closes. Force a
        // full NC repaint here so our dark paint reasserts itself.
        if (IsDarkMode())
            RedrawWindow(hwnd, nullptr, nullptr, RDW_INVALIDATE | RDW_FRAME);
        break;
    }
    case WM_INITMENUPOPUP:
    {
        // Refresh the dynamic state of menu items right as a popup opens:
        // File > Open Folder / Copy Path need a saved path; Format's Line
        // Endings / Encoding submenus show the document's current values as
        // radio ticks.
        HMENU hMenu = GetMenu(g_hwndMain);
        if (hMenu)
        {
            UINT flag = g_state.filePath.empty() ? (MF_BYCOMMAND | MF_GRAYED)
                                                 : (MF_BYCOMMAND | MF_ENABLED);
            EnableMenuItem(hMenu, IDM_FILE_OPENFOLDER, flag);
            EnableMenuItem(hMenu, IDM_FILE_COPYPATH, flag);

            UINT leId = (g_state.lineEnding == LineEnding::CRLF) ? IDM_FORMAT_LE_CRLF
                      : (g_state.lineEnding == LineEnding::LF)   ? IDM_FORMAT_LE_LF
                                                                 : IDM_FORMAT_LE_CR;
            CheckMenuRadioItem(hMenu, IDM_FORMAT_LE_CRLF, IDM_FORMAT_LE_CR, leId, MF_BYCOMMAND);

            UINT encId;
            switch (g_state.encoding)
            {
            case Encoding::UTF8:    encId = IDM_FORMAT_ENC_UTF8; break;
            case Encoding::UTF8BOM: encId = IDM_FORMAT_ENC_UTF8BOM; break;
            case Encoding::UTF16LE: encId = IDM_FORMAT_ENC_UTF16LE; break;
            case Encoding::UTF16BE: encId = IDM_FORMAT_ENC_UTF16BE; break;
            default:                encId = IDM_FORMAT_ENC_ANSI; break;
            }
            CheckMenuRadioItem(hMenu, IDM_FORMAT_ENC_UTF8, IDM_FORMAT_ENC_ANSI, encId, MF_BYCOMMAND);
        }
        break;
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
        // lParam carries the cursor position in screen coordinates for NC
        // mouse messages — hand it to the quick-icon tooltip driver.
        {
            POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            ShowQuickIconTooltip(pt);
        }
        break;
    }
    case WM_NCMOUSELEAVE:
    {
        g_ncMouseTracking = false;
        HideQuickIconTooltip();
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
    case WM_SNIPPETS_REFRESH:
        RefreshSnippetTree();
        return 0;
    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        DrawSnippetsSplitter(hdc);
        EndPaint(hwnd, &ps);
        return 0;
    }
    // Snippets-panel splitter: the gap between the editor and the panel is
    // bare main-window client area, so its mouse traffic lands here.
    case WM_SETCURSOR:
        if (SnippetsHandleSetCursor())
            return TRUE;
        break;
    case WM_LBUTTONDOWN:
        if (SnippetsHandleLButtonDown(lParam))
            return 0;
        break;
    case WM_MOUSEMOVE:
        if (SnippetsHandleMouseMove(lParam))
            return 0;
        break;
    case WM_LBUTTONUP:
        if (SnippetsHandleLButtonUp())
            return 0;
        break;
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
        if (pMIS->CtlType == ODT_MENU)
        {
            UINT id = static_cast<UINT>(pMIS->itemID);
            if (IsQuickIconId(id))
            {
                MeasureQuickIconItem(pMIS);
                return TRUE;
            }
            if (IsTopLevelMenuItemId(id))
            {
                MeasureTopLevelMenuItem(pMIS);
                return TRUE;
            }
        }
        break;
    }
    case WM_DRAWITEM:
    {
        LPDRAWITEMSTRUCT pDIS = reinterpret_cast<LPDRAWITEMSTRUCT>(lParam);
        if (pDIS->CtlType == ODT_MENU)
        {
            UINT id = static_cast<UINT>(pDIS->itemID);
            if (IsQuickIconId(id))
            {
                DrawQuickIconItem(pDIS);
                return TRUE;
            }
            if (IsTopLevelMenuItemId(id))
            {
                DrawTopLevelMenuItem(pDIS);
                return TRUE;
            }
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
        case IDM_VIEW_THEME_LIGHT:
            SetThemeChoice(Theme::Light);
            break;
        case IDM_VIEW_THEME_DARK:
            SetThemeChoice(Theme::Dark);
            break;
        case IDM_VIEW_THEME_MATRIX:
            SetThemeChoice(Theme::Matrix);
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
        case IDM_EDIT_SETTINGS_HIGHLIGHTLINE:
            g_state.highlightCurrentLine = !g_state.highlightCurrentLine;
            CheckMenuItem(GetMenu(g_hwndMain), IDM_EDIT_SETTINGS_HIGHLIGHTLINE,
                          g_state.highlightCurrentLine ? MF_CHECKED : MF_UNCHECKED);
            InvalidateRect(g_hwndEditor, nullptr, TRUE);
            SaveSettings();
            break;
        case IDM_EDIT_SETTINGS_HIGHLIGHTWORD:
            g_state.highlightOccurrences = !g_state.highlightOccurrences;
            CheckMenuItem(GetMenu(g_hwndMain), IDM_EDIT_SETTINGS_HIGHLIGHTWORD,
                          g_state.highlightOccurrences ? MF_CHECKED : MF_UNCHECKED);
            InvalidateRect(g_hwndEditor, nullptr, TRUE);
            SaveSettings();
            break;
        case IDM_FORMAT_LE_CRLF:
        case IDM_FORMAT_LE_LF:
        case IDM_FORMAT_LE_CR:
        {
            LineEnding le = (cmd == IDM_FORMAT_LE_CRLF) ? LineEnding::CRLF
                          : (cmd == IDM_FORMAT_LE_LF)   ? LineEnding::LF
                                                        : LineEnding::CR;
            if (g_state.lineEnding != le)
            {
                g_state.lineEnding = le;
                g_state.modified = true; // affects what Save writes
                UpdateTitle();
                UpdateStatus();
            }
            break;
        }
        case IDM_FORMAT_ENC_UTF8:
        case IDM_FORMAT_ENC_UTF8BOM:
        case IDM_FORMAT_ENC_UTF16LE:
        case IDM_FORMAT_ENC_UTF16BE:
        case IDM_FORMAT_ENC_ANSI:
        {
            Encoding enc = (cmd == IDM_FORMAT_ENC_UTF8)    ? Encoding::UTF8
                         : (cmd == IDM_FORMAT_ENC_UTF8BOM) ? Encoding::UTF8BOM
                         : (cmd == IDM_FORMAT_ENC_UTF16LE) ? Encoding::UTF16LE
                         : (cmd == IDM_FORMAT_ENC_UTF16BE) ? Encoding::UTF16BE
                                                           : Encoding::ANSI;
            if (g_state.encoding != enc)
            {
                g_state.encoding = enc;
                g_state.modified = true;
                UpdateTitle();
                UpdateStatus();
            }
            break;
        }
        case IDM_FILE_OPENFOLDER:
            if (!g_state.filePath.empty())
            {
                // Open Explorer with the file selected.
                std::wstring args = L"/select,\"" + g_state.filePath + L"\"";
                ShellExecuteW(g_hwndMain, nullptr, L"explorer.exe", args.c_str(), nullptr, SW_SHOWNORMAL);
            }
            break;
        case IDM_FILE_COPYPATH:
            if (!g_state.filePath.empty() && OpenClipboard(g_hwndMain))
            {
                EmptyClipboard();
                size_t bytes = (g_state.filePath.size() + 1) * sizeof(wchar_t);
                HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, bytes);
                if (hMem)
                {
                    void *p = GlobalLock(hMem);
                    if (p)
                    {
                        CopyMemory(p, g_state.filePath.c_str(), bytes);
                        GlobalUnlock(hMem);
                        SetClipboardData(CF_UNICODETEXT, hMem);
                    }
                }
                CloseClipboard();
            }
            break;
        case IDM_QUICK_SPELLCHECK:
        case IDM_QUICK_ONTOP:
        case IDM_QUICK_DARKMODE:
        case IDM_QUICK_SNIPPETS:
            HandleQuickIconClick(cmd);
            SaveSettings();
            break;
        case IDM_QUICK_INSERTCHAR:
            // Opens its own popup and inserts the chosen glyph; no toggle
            // state to persist, so no SaveSettings.
            HandleQuickIconClick(cmd);
            break;
        case IDM_TOOLS_NORMALIZE:
            ToolsNormalizeText();
            break;
        case IDM_TOOLS_BASE64:
            ToolsBase64();
            break;
        case IDM_TOOLS_BASE64_DECODE:
            ToolsBase64Decode();
            break;
        case IDM_TOOLS_SHA1:
            ToolsSha1();
            break;
        case IDM_TOOLS_MD5:
            ToolsMd5();
            break;
        case IDM_TOOLS_UPPERCASE:
            ToolsUppercase();
            break;
        case IDM_TOOLS_LOWERCASE:
            ToolsLowercase();
            break;
        case IDM_TOOLS_TITLECASE:
            ToolsTitleCase();
            break;
        case IDM_TOOLS_TRIMTRAILING:
            ToolsTrimTrailing();
            break;
        case IDM_TOOLS_TABS2SPACES:
            ToolsTabsToSpaces();
            break;
        case IDM_TOOLS_SPACES2TABS:
            ToolsSpacesToTabs();
            break;
        case IDM_TOOLS_SORTASC:
            ToolsSortLinesAsc();
            break;
        case IDM_TOOLS_SORTDESC:
            ToolsSortLinesDesc();
            break;
        case IDM_TOOLS_URLENCODE:
            ToolsUrlEncode();
            break;
        case IDM_TOOLS_URLDECODE:
            ToolsUrlDecode();
            break;
        case IDM_TOOLS_REVERSELINES:
            ToolsReverseLines();
            break;
        case IDM_TOOLS_JOINLINES:
            ToolsJoinLines();
            break;
        case IDM_TOOLS_REMOVEEMPTY:
            ToolsRemoveEmptyLines();
            break;
        case IDM_TOOLS_REMOVEDUPES:
            ToolsRemoveDuplicateLines();
            break;
        case IDM_VIEW_LANG_EN:
            ChangeLanguage(LangID::EN);
            break;
        case IDM_VIEW_LANG_JA:
            ChangeLanguage(LangID::JA);
            break;
        case IDM_VIEW_LANG_PL:
            ChangeLanguage(LangID::PL);
            break;
        case IDM_VIEW_LANG_DE:
            ChangeLanguage(LangID::DE);
            break;
        case IDM_VIEW_LANG_CS:
            ChangeLanguage(LangID::CS);
            break;
        case IDM_VIEW_LANG_UK:
            ChangeLanguage(LangID::UK);
            break;
        case IDM_VIEW_LANG_LT:
            ChangeLanguage(LangID::LT);
            break;
        case IDM_VIEW_LANG_RU:
            ChangeLanguage(LangID::RU);
            break;
        case IDM_VIEW_LANG_ZH:
            ChangeLanguage(LangID::ZH);
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
        if (pnmh->hwndFrom == g_hwndSnippets)
        {
            bool handled = false;
            LRESULT r = HandleSnippetsNotify(pnmh, &handled);
            if (handled)
                return r;
        }
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
            OnEditorSelChangeHighlights();
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
    // Class registration or WM_CREATE failed (e.g. missing RichEdit DLL —
    // the error box was already shown). Without this, GetMessage below would
    // wait forever with no window and no WM_QUIT: an invisible hung process.
    if (!g_hwndMain)
        return 1;
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
        // While the snippets tree (or its in-place label editor) has focus,
        // accelerators must not fire — Ctrl+A/Z/Del belong to the edit box
        // and Del/F2/Enter to the tree's own key handling.
        HWND focus = GetFocus();
        bool inSnippets = focus && g_hwndSnippets &&
                          (focus == g_hwndSnippets || IsChild(g_hwndSnippets, focus));
        if (inSnippets || !TranslateAcceleratorW(g_hwndMain, g_hAccel, &msg))
        {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }

    ShutdownSpellCheck();
    return static_cast<int>(msg.wParam);
}
