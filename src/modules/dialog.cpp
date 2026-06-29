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

  Dialog box implementations for find, replace, goto, font selection, and more.
  Provides modeless and modal dialog creation with proper event handling.
*/

#include "dialog.h"
#include "core/globals.h"
#include "editor.h"
#include "ui.h"
#include "theme.h"
#include "settings.h"
#include "resource.h"
#include "lang/lang.h"
#include "build_info.h"
#include <commdlg.h>
#include <commctrl.h>
#include <richedit.h>
#include <uxtheme.h>
#include <algorithm>
#include <cwctype>
#include <ctime>

static HWND g_transparencySlider = nullptr;
static HWND g_transparencyLabel = nullptr;
static BYTE g_transparencyOriginal = 255;

static HFONT GetDialogFont()
{
    static HFONT hFont = nullptr;
    if (!hFont)
    {
        NONCLIENTMETRICSW ncm = {};
        ncm.cbSize = sizeof(ncm);
        SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0);
        hFont = CreateFontIndirectW(&ncm.lfMessageFont);
    }
    return hFont;
}

static void CenterDialogOnParent(HWND hDlg, HWND hParent)
{
    if (!hParent || !IsWindow(hParent))
        hParent = GetDesktopWindow();
    RECT rcParent, rcDlg;
    GetWindowRect(hParent, &rcParent);
    GetWindowRect(hDlg, &rcDlg);
    int dw = rcDlg.right - rcDlg.left;
    int dh = rcDlg.bottom - rcDlg.top;
    int pw = rcParent.right - rcParent.left;
    int ph = rcParent.bottom - rcParent.top;
    int x = rcParent.left + (pw - dw) / 2;
    int y = rcParent.top + (ph - dh) / 2;

    HMONITOR hMon = MonitorFromWindow(hParent, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi = {sizeof(mi)};
    if (GetMonitorInfoW(hMon, &mi))
    {
        if (x < mi.rcWork.left) x = mi.rcWork.left;
        if (y < mi.rcWork.top) y = mi.rcWork.top;
        if (x + dw > mi.rcWork.right) x = mi.rcWork.right - dw;
        if (y + dh > mi.rcWork.bottom) y = mi.rcWork.bottom - dh;
    }
    SetWindowPos(hDlg, nullptr, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
}

static void ApplyDialogFont(HWND hDlg)
{
    HFONT hFont = GetDialogFont();
    SendMessageW(hDlg, WM_SETFONT, reinterpret_cast<WPARAM>(hFont), TRUE);
    for (HWND h = GetWindow(hDlg, GW_CHILD); h; h = GetWindow(h, GW_HWNDNEXT))
        SendMessageW(h, WM_SETFONT, reinterpret_cast<WPARAM>(hFont), TRUE);
}

static HBRUSH GetDialogBackgroundBrush();

// Default WM_ERASEBKGND for #32770 windows fills with COLOR_3DFACE,
// which appears white in light mode and — after minimize+restore — can
// also reappear under our dark theme because Windows redraws via the
// class brush, bypassing our WM_CTLCOLORDLG handler. This helper paints
// the dark background explicitly when dark mode is active; in light
// mode it falls through to the system default.
static LRESULT EraseDialogBg(HWND hDlg, WPARAM wParam)
{
    if (!IsDarkMode())
        return 0;
    HDC hdc = reinterpret_cast<HDC>(wParam);
    RECT rc;
    GetClientRect(hDlg, &rc);
    FillRect(hdc, &rc, GetDialogBackgroundBrush());
    return 1;
}

// Creates a dialog window whose CLIENT area is exactly clientW x clientH.
// All child-control coordinates can then be placed in client coordinates
// without worrying about caption/border height.
static HWND CreateAppDialog(LPCWSTR title, int clientW, int clientH)
{
    const DWORD style = WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_VISIBLE;
    const DWORD exStyle = WS_EX_DLGMODALFRAME;
    RECT rc = {0, 0, clientW, clientH};
    AdjustWindowRectEx(&rc, style, FALSE, exStyle);
    return CreateWindowExW(exStyle, L"#32770", title, style,
                           0, 0, rc.right - rc.left, rc.bottom - rc.top,
                           g_hwndMain, nullptr, GetModuleHandleW(nullptr), nullptr);
}

static void ApplyTransparencyPercent(int pct)
{
    if (pct < 25) pct = 25;
    if (pct > 100) pct = 100;
    BYTE alpha = static_cast<BYTE>(pct * 255 / 100);
    g_state.windowOpacity = alpha;
    SetWindowLongW(g_hwndMain, GWL_EXSTYLE, GetWindowLongW(g_hwndMain, GWL_EXSTYLE) | WS_EX_LAYERED);
    SetLayeredWindowAttributes(g_hwndMain, 0, alpha, LWA_ALPHA);
}

static HBRUSH GetDialogBackgroundBrush()
{
    static HBRUSH brush = CreateSolidBrush(RGB(32, 32, 32));
    return brush;
}

static HBRUSH GetDialogEditBrush()
{
    static HBRUSH brush = CreateSolidBrush(RGB(45, 45, 45));
    return brush;
}

static bool DrawDarkDialogButton(const DRAWITEMSTRUCT *dis)
{
    if (!IsDarkMode() || !dis || dis->CtlType != ODT_BUTTON)
        return false;

    COLORREF bgColor = (dis->itemState & ODS_SELECTED) ? RGB(70, 70, 70) : RGB(56, 56, 56);
    COLORREF borderColor = RGB(95, 95, 95);
    COLORREF textColor = (dis->itemState & ODS_DISABLED) ? RGB(140, 140, 140) : RGB(240, 240, 240);

    HBRUSH bgBrush = CreateSolidBrush(bgColor);
    FillRect(dis->hDC, &dis->rcItem, bgBrush);
    DeleteObject(bgBrush);

    HBRUSH borderBrush = CreateSolidBrush(borderColor);
    FrameRect(dis->hDC, &dis->rcItem, borderBrush);
    DeleteObject(borderBrush);

    wchar_t text[128] = {};
    GetWindowTextW(dis->hwndItem, text, 128);
    RECT textRect = dis->rcItem;
    SetBkMode(dis->hDC, TRANSPARENT);
    SetTextColor(dis->hDC, textColor);
    DrawTextW(dis->hDC, text, -1, &textRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

    if (dis->itemState & ODS_FOCUS)
    {
        RECT focusRect = dis->rcItem;
        InflateRect(&focusRect, -4, -4);
        DrawFocusRect(dis->hDC, &focusRect);
    }
    return true;
}

static void MakeDialogButtonsOwnerDraw(HWND hDlg)
{
    if (!IsDarkMode())
        return;
    wchar_t cls[16] = {};
    for (HWND h = GetWindow(hDlg, GW_CHILD); h; h = GetWindow(h, GW_HWNDNEXT))
    {
        if (GetClassNameW(h, cls, 16) > 0 && lstrcmpiW(cls, L"Button") == 0)
        {
            LONG_PTR style = GetWindowLongPtrW(h, GWL_STYLE);
            SetWindowLongPtrW(h, GWL_STYLE, (style & ~BS_TYPEMASK) | BS_OWNERDRAW);
            InvalidateRect(h, nullptr, TRUE);
        }
    }
}

static void ApplyDialogDarkMode(HWND hDlg)
{
    if (!IsDarkMode())
        return;
    SetTitleBarDark(hDlg, TRUE);
    SetWindowTheme(hDlg, L"DarkMode_Explorer", nullptr);
    for (HWND h = GetWindow(hDlg, GW_CHILD); h; h = GetWindow(h, GW_HWNDNEXT))
        SetWindowTheme(h, L"DarkMode_Explorer", nullptr);
    MakeDialogButtonsOwnerDraw(hDlg);
}

static INT_PTR HandleDialogDarkColors(UINT msg, WPARAM wParam)
{
    if (!IsDarkMode())
        return 0;
    HDC hdc = reinterpret_cast<HDC>(wParam);
    switch (msg)
    {
    case WM_CTLCOLOREDIT:
        SetTextColor(hdc, RGB(240, 240, 240));
        SetBkColor(hdc, RGB(45, 45, 45));
        return reinterpret_cast<INT_PTR>(GetDialogEditBrush());
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLORBTN:
    case WM_CTLCOLORDLG:
        SetTextColor(hdc, RGB(240, 240, 240));
        SetBkColor(hdc, RGB(32, 32, 32));
        return reinterpret_cast<INT_PTR>(GetDialogBackgroundBrush());
    }
    return 0;
}

// NOTE: the native Find / Replace dialogs (comdlg32) are deliberately left in
// their default (light) appearance, even under the Dark / Matrix themes. They
// can't be dark-themed reliably: their window procedure lives in comdlg32, so
// owner-drawing the buttons leaves them unpainted (invisible), while the
// WM_CTLCOLOR / visual-style route darkens only some controls — the dialog
// background stays light and the group box / radio labels render unreadably.
// A standard light dialog is fully visible and functional, which wins over a
// half-dark broken one. (A future fully-custom Find dialog could be themed.)

static LRESULT CALLBACK TransparencyDlgProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
    case WM_ERASEBKGND:
    {
        LRESULT r = EraseDialogBg(hDlg, wParam);
        if (r) return r;
        break;
    }
    case WM_HSCROLL:
        if (reinterpret_cast<HWND>(lParam) == g_transparencySlider)
        {
            int pos = static_cast<int>(SendMessageW(g_transparencySlider, TBM_GETPOS, 0, 0));
            ApplyTransparencyPercent(pos);
            wchar_t buf[16];
            wsprintfW(buf, L"%d%%", pos);
            SetWindowTextW(g_transparencyLabel, buf);
            return 0;
        }
        break;
    case WM_COMMAND:
        if (LOWORD(wParam) == IDOK)
        {
            SaveSettings();
            DestroyWindow(hDlg);
            return 0;
        }
        if (LOWORD(wParam) == IDCANCEL)
        {
            ApplyTransparencyPercent(g_transparencyOriginal * 100 / 255);
            g_state.windowOpacity = g_transparencyOriginal;
            DestroyWindow(hDlg);
            return 0;
        }
        break;
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLORBTN:
    case WM_CTLCOLORDLG:
    {
        INT_PTR colorResult = HandleDialogDarkColors(msg, wParam);
        if (colorResult)
            return static_cast<LRESULT>(colorResult);
        break;
    }
    case WM_DRAWITEM:
        if (DrawDarkDialogButton(reinterpret_cast<const DRAWITEMSTRUCT *>(lParam)))
            return TRUE;
        break;
    case WM_CLOSE:
        ApplyTransparencyPercent(g_transparencyOriginal * 100 / 255);
        g_state.windowOpacity = g_transparencyOriginal;
        DestroyWindow(hDlg);
        return 0;
    case WM_DESTROY:
        g_transparencySlider = nullptr;
        g_transparencyLabel = nullptr;
        g_hwndTransparencyDlg = nullptr;
        return 0;
    }
    return DefWindowProcW(hDlg, msg, wParam, lParam);
}

static FINDREPLACEW g_findReplace = {};
static wchar_t g_findBuffer[256] = {};
static wchar_t g_replaceBuffer[256] = {};

void DoFind(bool forward, bool matchCase)
{
    if (g_state.findText.empty())
        return;

    DWORD start = 0, end = 0;
    SendMessageW(g_hwndEditor, EM_GETSEL, reinterpret_cast<WPARAM>(&start), reinterpret_cast<LPARAM>(&end));

    FINDTEXTEXW ft = {};
    ft.lpstrText = const_cast<LPWSTR>(g_state.findText.c_str());
    WPARAM flags = matchCase ? FR_MATCHCASE : 0;
    if (forward)
        flags |= FR_DOWN;

    LRESULT pos = -1;
    if (forward)
    {
        ft.chrg.cpMin = static_cast<LONG>(end);
        ft.chrg.cpMax = -1;
        pos = SendMessageW(g_hwndEditor, EM_FINDTEXTEXW, flags, reinterpret_cast<LPARAM>(&ft));
        if (pos == -1)
        {
            ft.chrg.cpMin = 0;
            ft.chrg.cpMax = -1;
            pos = SendMessageW(g_hwndEditor, EM_FINDTEXTEXW, flags, reinterpret_cast<LPARAM>(&ft));
        }
    }
    else
    {
        ft.chrg.cpMin = static_cast<LONG>(start);
        ft.chrg.cpMax = 0;
        pos = SendMessageW(g_hwndEditor, EM_FINDTEXTEXW, flags, reinterpret_cast<LPARAM>(&ft));
        if (pos == -1)
        {
            LONG len = static_cast<LONG>(SendMessageW(g_hwndEditor, WM_GETTEXTLENGTH, 0, 0));
            ft.chrg.cpMin = len;
            ft.chrg.cpMax = 0;
            pos = SendMessageW(g_hwndEditor, EM_FINDTEXTEXW, flags, reinterpret_cast<LPARAM>(&ft));
        }
    }

    if (pos != -1)
    {
        SendMessageW(g_hwndEditor, EM_SETSEL, ft.chrgText.cpMin, ft.chrgText.cpMax);
        SendMessageW(g_hwndEditor, EM_SCROLLCARET, 0, 0);
    }
    else
    {
        const auto &lang = GetLangStrings();
        MessageBoxW(g_hwndMain, (lang.msgCannotFind + g_state.findText + L"\"").c_str(), lang.appName.c_str(), MB_ICONINFORMATION);
    }
}

static void InitFindReplaceStruct()
{
    wcsncpy_s(g_findBuffer, g_state.findText.c_str(), _TRUNCATE);
    wcsncpy_s(g_replaceBuffer, g_state.replaceText.c_str(), _TRUNCATE);
    g_findReplace = {};
    g_findReplace.lStructSize = sizeof(g_findReplace);
    g_findReplace.hwndOwner = g_hwndMain;
    g_findReplace.lpstrFindWhat = g_findBuffer;
    g_findReplace.wFindWhatLen = static_cast<WORD>(sizeof(g_findBuffer) / sizeof(wchar_t));
    g_findReplace.lpstrReplaceWith = g_replaceBuffer;
    g_findReplace.wReplaceWithLen = static_cast<WORD>(sizeof(g_replaceBuffer) / sizeof(wchar_t));
    g_findReplace.Flags = FR_DOWN;
}

// Find / Replace previously used FR_ENABLEHOOK + a hook proc to center the
// dialog at WM_INITDIALOG. On some Win11 builds this combination silently
// failed (CommDlgExtendedError -> CDERR_FINDRESFAILURE / similar) and the
// dialog never appeared. The hook is non-essential — we can position the
// dialog post-creation via SetWindowPos, which avoids the issue.
void EditFind()
{
    if (g_hwndFindDlg && IsWindow(g_hwndFindDlg))
    {
        SetFocus(g_hwndFindDlg);
        return;
    }
    g_hwndFindDlg = nullptr;
    InitFindReplaceStruct();
    g_hwndFindDlg = FindTextW(&g_findReplace);
    if (g_hwndFindDlg)
    {
        CenterDialogOnParent(g_hwndFindDlg, g_hwndMain);
    }
}

void EditReplace()
{
    if (g_hwndFindDlg && IsWindow(g_hwndFindDlg))
    {
        SetFocus(g_hwndFindDlg);
        return;
    }
    g_hwndFindDlg = nullptr;
    InitFindReplaceStruct();
    g_hwndFindDlg = ReplaceTextW(&g_findReplace);
    if (g_hwndFindDlg)
    {
        CenterDialogOnParent(g_hwndFindDlg, g_hwndMain);
    }
}

void EditFindNext()
{
    if (!g_state.findText.empty())
        DoFind(true, (g_findReplace.Flags & FR_MATCHCASE) != 0);
}

void EditFindPrev()
{
    if (!g_state.findText.empty())
        DoFind(false, (g_findReplace.Flags & FR_MATCHCASE) != 0);
}

void HandleFindReplaceMessage(LPFINDREPLACEW pfr)
{
    if (!pfr)
        return;
    if (pfr->Flags & FR_DIALOGTERM)
    {
        g_hwndFindDlg = nullptr;
        SetFocus(g_hwndEditor);
        return;
    }

    g_state.findText = pfr->lpstrFindWhat ? pfr->lpstrFindWhat : L"";
    if (pfr->lpstrReplaceWith)
        g_state.replaceText = pfr->lpstrReplaceWith;

    bool matchCase = (pfr->Flags & FR_MATCHCASE) != 0;
    bool forward = (pfr->Flags & FR_DOWN) != 0;

    if (pfr->Flags & FR_FINDNEXT)
    {
        DoFind(forward, matchCase);
    }
    else if (pfr->Flags & FR_REPLACE)
    {
        if (g_state.findText.empty())
            return;
        DWORD selStart = 0, selEnd = 0;
        SendMessageW(g_hwndEditor, EM_GETSEL, reinterpret_cast<WPARAM>(&selStart), reinterpret_cast<LPARAM>(&selEnd));
        if (selStart != selEnd)
        {
            std::wstring sel(selEnd - selStart + 1, L'\0');
            SendMessageW(g_hwndEditor, EM_GETSELTEXT, 0, reinterpret_cast<LPARAM>(sel.data()));
            sel.resize(wcslen(sel.c_str()));
            std::wstring findCmp = g_state.findText;
            if (!matchCase)
            {
                std::transform(sel.begin(), sel.end(), sel.begin(), towlower);
                std::transform(findCmp.begin(), findCmp.end(), findCmp.begin(), towlower);
            }
            if (sel == findCmp)
                SendMessageW(g_hwndEditor, EM_REPLACESEL, TRUE, reinterpret_cast<LPARAM>(g_state.replaceText.c_str()));
        }
        DoFind(forward, matchCase);
    }
    else if (pfr->Flags & FR_REPLACEALL)
    {
        if (g_state.findText.empty())
            return;
        WPARAM flags = (matchCase ? FR_MATCHCASE : 0) | FR_DOWN;
        SendMessageW(g_hwndEditor, EM_SETSEL, 0, 0);
        LONG start = 0;
        int replaced = 0;
        SendMessageW(g_hwndEditor, EM_HIDESELECTION, TRUE, 0);
        while (true)
        {
            FINDTEXTEXW ft = {};
            ft.chrg.cpMin = start;
            ft.chrg.cpMax = -1;
            ft.lpstrText = const_cast<LPWSTR>(g_state.findText.c_str());
            LRESULT pos = SendMessageW(g_hwndEditor, EM_FINDTEXTEXW, flags, reinterpret_cast<LPARAM>(&ft));
            if (pos == -1)
                break;
            SendMessageW(g_hwndEditor, EM_SETSEL, ft.chrgText.cpMin, ft.chrgText.cpMax);
            SendMessageW(g_hwndEditor, EM_REPLACESEL, TRUE, reinterpret_cast<LPARAM>(g_state.replaceText.c_str()));
            start = ft.chrgText.cpMin + static_cast<LONG>(g_state.replaceText.length());
            ++replaced;
        }
        SendMessageW(g_hwndEditor, EM_HIDESELECTION, FALSE, 0);
        if (replaced > 0)
        {
            g_state.modified = true;
            UpdateTitle();
        }
    }
}

INT_PTR CALLBACK GotoDlgProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
    case WM_INITDIALOG:
        ApplyDialogDarkMode(hDlg);
        return TRUE;
    case WM_ERASEBKGND:
    {
        LRESULT r = EraseDialogBg(hDlg, wParam);
        if (r) return static_cast<INT_PTR>(r);
        break;
    }
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLORBTN:
    case WM_CTLCOLORDLG:
    {
        INT_PTR colorResult = HandleDialogDarkColors(msg, wParam);
        if (colorResult)
            return colorResult;
        break;
    }
    case WM_DRAWITEM:
        if (DrawDarkDialogButton(reinterpret_cast<const DRAWITEMSTRUCT *>(lParam)))
            return TRUE;
        break;
    case WM_COMMAND:
        if (LOWORD(wParam) == IDOK)
        {
            wchar_t buf[32];
            GetWindowTextW(GetDlgItem(hDlg, 1001), buf, 32);
            int line = _wtoi(buf);
            if (line > 0)
            {
                LRESULT charIndex = SendMessageW(g_hwndEditor, EM_LINEINDEX, static_cast<WPARAM>(line) - 1, 0);
                if (charIndex != -1)
                {
                    SendMessageW(g_hwndEditor, EM_SETSEL, charIndex, charIndex);
                    SendMessageW(g_hwndEditor, EM_SCROLLCARET, 0, 0);
                    SetFocus(g_hwndEditor);
                    DestroyWindow(hDlg);
                }
                else
                {
                    const auto &lang = GetLangStrings();
                    MessageBoxW(hDlg, L"The line number is beyond the total number of lines.", (lang.appName + L" - " + lang.dialogGoTo).c_str(), MB_OK | MB_ICONWARNING);
                }
            }
            return TRUE;
        }
        else if (LOWORD(wParam) == IDCANCEL)
        {
            DestroyWindow(hDlg);
            return TRUE;
        }
        break;
    case WM_CLOSE:
        DestroyWindow(hDlg);
        return TRUE;
    }
    return DefDlgProcW(hDlg, msg, wParam, lParam);
}

void EditGoto()
{
    const auto &lang = GetLangStrings();
    const int W = 300, H = 150;
    const int PAD = 20;
    HWND hDlg = CreateAppDialog(lang.dialogGoTo.c_str(), W, H);
    if (!hDlg)
        return;

    CreateWindowExW(0, L"STATIC", lang.dialogLineNumber.c_str(),
                    WS_CHILD | WS_VISIBLE,
                    PAD, PAD, W - PAD * 2, 20, hDlg, nullptr, nullptr, nullptr);

    DWORD start = 0;
    SendMessageW(g_hwndEditor, EM_GETSEL, reinterpret_cast<WPARAM>(&start), 0);
    int curLine = (int)SendMessageW(g_hwndEditor, EM_EXLINEFROMCHAR, 0, start) + 1;
    wchar_t buf[32];
    wsprintfW(buf, L"%d", curLine);

    HWND hEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", buf,
                                 WS_CHILD | WS_VISIBLE | ES_NUMBER | ES_AUTOHSCROLL | WS_TABSTOP,
                                 PAD, PAD + 24, W - PAD * 2, 24,
                                 hDlg, reinterpret_cast<HMENU>(1001), nullptr, nullptr);
    SendMessageW(hEdit, EM_SETSEL, 0, -1);

    const int btnW = 88, btnH = 28;
    const int btnY = H - PAD - btnH;
    CreateWindowExW(0, L"BUTTON", lang.dialogOK.c_str(),
                    WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON | WS_TABSTOP,
                    W - PAD - btnW * 2 - 8, btnY, btnW, btnH,
                    hDlg, reinterpret_cast<HMENU>(IDOK), nullptr, nullptr);
    CreateWindowExW(0, L"BUTTON", lang.dialogCancel.c_str(),
                    WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                    W - PAD - btnW, btnY, btnW, btnH,
                    hDlg, reinterpret_cast<HMENU>(IDCANCEL), nullptr, nullptr);

    ApplyDialogFont(hDlg);

    SetWindowLongPtrW(hDlg, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(GotoDlgProc));
    ApplyDialogDarkMode(hDlg);
    CenterDialogOnParent(hDlg, g_hwndMain);
    SetFocus(hEdit);
}

void FormatFont()
{
    LOGFONTW lf{};
    if (g_state.hFont)
        GetObjectW(g_state.hFont, sizeof(LOGFONTW), &lf);
    else
    {
        HDC hdc = GetDC(g_hwndMain);
        lf.lfHeight = -MulDiv(g_state.fontSize, GetDeviceCaps(hdc, LOGPIXELSY), 72);
        ReleaseDC(g_hwndMain, hdc);
        wcscpy_s(lf.lfFaceName, g_state.fontName.c_str());
        lf.lfWeight = g_state.fontWeight;
        lf.lfItalic = g_state.fontItalic ? TRUE : FALSE;
        lf.lfUnderline = g_state.fontUnderline ? TRUE : FALSE;
        lf.lfCharSet = DEFAULT_CHARSET;
        lf.lfOutPrecision = OUT_DEFAULT_PRECIS;
        lf.lfClipPrecision = CLIP_DEFAULT_PRECIS;
        lf.lfQuality = CLEARTYPE_QUALITY;
        lf.lfPitchAndFamily = DEFAULT_PITCH | FF_DONTCARE;
    }
    CHOOSEFONTW cf{};
    cf.lStructSize = sizeof(cf);
    cf.hwndOwner = g_hwndMain;
    cf.lpLogFont = &lf;
    cf.Flags = CF_SCREENFONTS | CF_INITTOLOGFONTSTRUCT | CF_FORCEFONTEXIST | CF_BOTH;
    if (ChooseFontW(&cf))
    {
        g_state.fontName = lf.lfFaceName;
        g_state.fontWeight = lf.lfWeight;
        g_state.fontItalic = (lf.lfItalic != 0);
        g_state.fontUnderline = (lf.lfUnderline != 0);
        HDC hdc2 = GetDC(g_hwndMain);
        g_state.fontSize = MulDiv(-lf.lfHeight, 72, GetDeviceCaps(hdc2, LOGPIXELSY));
        ReleaseDC(g_hwndMain, hdc2);
        ApplyFont();
        SaveFontSettings();
    }
}

void ViewTransparency()
{
    const auto &lang = GetLangStrings();
    g_transparencyOriginal = g_state.windowOpacity;
    int pct = g_state.windowOpacity * 100 / 255;
    if (pct < 25) pct = 25;
    if (pct > 100) pct = 100;

    const int W = 360, H = 170;
    const int PAD = 20;
    HWND hDlg = CreateAppDialog(lang.dialogTransparency.c_str(), W, H);
    if (!hDlg)
        return;

    CreateWindowExW(0, L"STATIC", lang.dialogOpacityLabel.c_str(),
                    WS_CHILD | WS_VISIBLE,
                    PAD, PAD, 200, 20, hDlg, nullptr, nullptr, nullptr);

    wchar_t valBuf[16];
    wsprintfW(valBuf, L"%d%%", pct);
    g_transparencyLabel = CreateWindowExW(0, L"STATIC", valBuf,
                                          WS_CHILD | WS_VISIBLE | SS_RIGHT,
                                          W - PAD - 70, PAD, 70, 20,
                                          hDlg, nullptr, nullptr, nullptr);

    g_transparencySlider = CreateWindowExW(0, TRACKBAR_CLASSW, nullptr,
                                           WS_CHILD | WS_VISIBLE | TBS_HORZ | TBS_AUTOTICKS | WS_TABSTOP,
                                           PAD, PAD + 28, W - 2 * PAD, 34,
                                           hDlg, nullptr, nullptr, nullptr);
    SendMessageW(g_transparencySlider, TBM_SETRANGE, TRUE, MAKELPARAM(25, 100));
    SendMessageW(g_transparencySlider, TBM_SETTICFREQ, 5, 0);
    SendMessageW(g_transparencySlider, TBM_SETPAGESIZE, 0, 10);
    SendMessageW(g_transparencySlider, TBM_SETPOS, TRUE, pct);

    const int btnW = 88, btnH = 28;
    const int btnY = H - PAD - btnH;
    CreateWindowExW(0, L"BUTTON", lang.dialogOK.c_str(),
                    WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON | WS_TABSTOP,
                    W - PAD - btnW * 2 - 8, btnY, btnW, btnH,
                    hDlg, reinterpret_cast<HMENU>(IDOK), nullptr, nullptr);
    CreateWindowExW(0, L"BUTTON", lang.dialogCancel.c_str(),
                    WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                    W - PAD - btnW, btnY, btnW, btnH,
                    hDlg, reinterpret_cast<HMENU>(IDCANCEL), nullptr, nullptr);

    ApplyDialogFont(hDlg);

    g_hwndTransparencyDlg = hDlg;
    SetWindowLongPtrW(hDlg, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(TransparencyDlgProc));
    ApplyDialogDarkMode(hDlg);
    CenterDialogOnParent(hDlg, g_hwndMain);
    SetFocus(g_transparencySlider);
}

static LRESULT CALLBACK AboutDlgProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
    case WM_ERASEBKGND:
    {
        LRESULT r = EraseDialogBg(hDlg, wParam);
        if (r) return r;
        break;
    }
    case WM_COMMAND:
        if (LOWORD(wParam) == IDOK || LOWORD(wParam) == IDCANCEL)
        {
            DestroyWindow(hDlg);
            return 0;
        }
        break;
    case WM_NOTIFY:
    {
        NMHDR *nm = reinterpret_cast<NMHDR *>(lParam);
        if (nm->code == NM_CLICK || nm->code == NM_RETURN)
        {
            // 1002 = this fork's repo (Marek / JG24), 1001 = the original.
            if (nm->idFrom == 1002)
            {
                ShellExecuteW(hDlg, L"open",
                              L"https://github.com/JG24/NotepadCE",
                              nullptr, nullptr, SW_SHOWNORMAL);
                return 0;
            }
            if (nm->idFrom == 1001)
            {
                ShellExecuteW(hDlg, L"open",
                              L"https://github.com/forloopcodes/legacy-notepad",
                              nullptr, nullptr, SW_SHOWNORMAL);
                return 0;
            }
        }
        // SysLink uses the system link color (~RGB(0,102,204)) which on the
        // dark dialog background blends in to the point of invisibility.
        // Override via custom-draw with a brighter blue.
        if (nm->code == NM_CUSTOMDRAW && (nm->idFrom == 1001 || nm->idFrom == 1002) && IsDarkMode())
        {
            NMCUSTOMDRAW *cd = reinterpret_cast<NMCUSTOMDRAW *>(lParam);
            if (cd->dwDrawStage == CDDS_PREPAINT)
            {
                SetTextColor(cd->hdc, RGB(102, 178, 255));
                SetBkMode(cd->hdc, TRANSPARENT);
                SetWindowLongPtrW(hDlg, DWLP_MSGRESULT, CDRF_NEWFONT);
                return TRUE;
            }
        }
        break;
    }
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLORBTN:
    case WM_CTLCOLORDLG:
    {
        INT_PTR r = HandleDialogDarkColors(msg, wParam);
        if (r) return static_cast<LRESULT>(r);
        break;
    }
    case WM_DRAWITEM:
        if (DrawDarkDialogButton(reinterpret_cast<const DRAWITEMSTRUCT *>(lParam)))
            return TRUE;
        break;
    case WM_CLOSE:
        DestroyWindow(hDlg);
        return 0;
    case WM_DESTROY:
        g_hwndAboutDlg = nullptr;
        return 0;
    }
    return DefWindowProcW(hDlg, msg, wParam, lParam);
}

void HelpAbout()
{
    if (g_hwndAboutDlg)
    {
        SetFocus(g_hwndAboutDlg);
        return;
    }
    const auto &lang = GetLangStrings();

    const int W = 500, H = 320;
    const int PAD = 24;
    HWND hDlg = CreateAppDialog(lang.aboutTitle.c_str(), W, H);
    if (!hDlg)
        return;

    const int LINE_H = 20;
    const int SEP_GAP = 14;
    int y = PAD;

    // Section 1: tagline + author + this fork's repository link
    CreateWindowExW(0, L"STATIC", lang.aboutTagline.c_str(),
                    WS_CHILD | WS_VISIBLE,
                    PAD, y, W - PAD * 2, LINE_H, hDlg, nullptr, nullptr, nullptr);
    y += LINE_H + 2;
    CreateWindowExW(0, L"STATIC", lang.aboutAuthor.c_str(),
                    WS_CHILD | WS_VISIBLE,
                    PAD, y, W - PAD * 2, LINE_H, hDlg, nullptr, nullptr, nullptr);
    y += LINE_H + 2;
    // LWS_USECUSTOMTEXT (0x0008) tells SysLink to honour WM_SETFONT.
    // Control id 1002 = this fork's repository (handled in AboutDlgProc).
    CreateWindowExW(0, L"SysLink",
                    L"<A HREF=\"https://github.com/JG24/NotepadCE\">https://github.com/JG24/NotepadCE</A>",
                    WS_CHILD | WS_VISIBLE | WS_TABSTOP | 0x0008,
                    PAD, y, W - PAD * 2, LINE_H,
                    hDlg, reinterpret_cast<HMENU>(1002), nullptr, nullptr);
    y += LINE_H + SEP_GAP;

    // Separator
    CreateWindowExW(0, L"STATIC", nullptr,
                    WS_CHILD | WS_VISIBLE | SS_ETCHEDHORZ,
                    PAD, y, W - PAD * 2, 2, hDlg, nullptr, nullptr, nullptr);
    y += SEP_GAP;

    // Section 2: provenance + tech + original author + link
    CreateWindowExW(0, L"STATIC", lang.aboutBuiltOn.c_str(),
                    WS_CHILD | WS_VISIBLE,
                    PAD, y, W - PAD * 2, LINE_H, hDlg, nullptr, nullptr, nullptr);
    y += LINE_H + 2;
    CreateWindowExW(0, L"STATIC", lang.aboutTech.c_str(),
                    WS_CHILD | WS_VISIBLE,
                    PAD, y, W - PAD * 2, LINE_H, hDlg, nullptr, nullptr, nullptr);
    y += LINE_H + 2;
    CreateWindowExW(0, L"STATIC", lang.aboutOriginalAuthor.c_str(),
                    WS_CHILD | WS_VISIBLE,
                    PAD, y, W - PAD * 2, LINE_H, hDlg, nullptr, nullptr, nullptr);
    y += LINE_H + 2;
    CreateWindowExW(0, L"SysLink",
                    L"<A HREF=\"https://github.com/forloopcodes/legacy-notepad\">https://github.com/forloopcodes/legacy-notepad</A>",
                    WS_CHILD | WS_VISIBLE | WS_TABSTOP | 0x0008,
                    PAD, y, W - PAD * 2, LINE_H,
                    hDlg, reinterpret_cast<HMENU>(1001), nullptr, nullptr);
    y += LINE_H + SEP_GAP;

    // Separator
    CreateWindowExW(0, L"STATIC", nullptr,
                    WS_CHILD | WS_VISIBLE | SS_ETCHEDHORZ,
                    PAD, y, W - PAD * 2, 2, hDlg, nullptr, nullptr, nullptr);
    y += SEP_GAP;

    // Build number, left-aligned, just above the OK button:
    // "<label> YYYYMMDDHHMM".
    std::wstring buildLine = L"NotepadCE " APP_VERSION L"   " + lang.aboutBuild + L" " + BUILD_NUMBER;
    CreateWindowExW(0, L"STATIC", buildLine.c_str(),
                    WS_CHILD | WS_VISIBLE,
                    PAD, y, W - PAD * 2, LINE_H, hDlg, nullptr, nullptr, nullptr);
    y += LINE_H + SEP_GAP;

    // OK button centered
    const int btnW = 96, btnH = 28;
    CreateWindowExW(0, L"BUTTON", lang.dialogOK.c_str(),
                    WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON | WS_TABSTOP,
                    (W - btnW) / 2, y, btnW, btnH,
                    hDlg, reinterpret_cast<HMENU>(IDOK), nullptr, nullptr);

    ApplyDialogFont(hDlg);

    g_hwndAboutDlg = hDlg;
    SetWindowLongPtrW(hDlg, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(AboutDlgProc));
    ApplyDialogDarkMode(hDlg);
    CenterDialogOnParent(hDlg, g_hwndMain);
    SetFocus(GetDlgItem(hDlg, IDOK));
}

static HWND g_dateFormatEdit = nullptr;
static HWND g_dateFormatPreview = nullptr;

static void UpdateDateFormatPreview()
{
    if (!g_dateFormatEdit || !g_dateFormatPreview)
        return;
    wchar_t fmt[256] = {};
    GetWindowTextW(g_dateFormatEdit, fmt, 256);
    time_t t = time(nullptr);
    struct tm tmLocal;
    localtime_s(&tmLocal, &t);
    wchar_t buf[256] = {};
    if (fmt[0] == L'\0' || wcsftime(buf, 256, fmt, &tmLocal) == 0)
        wcscpy_s(buf, L"—");
    SetWindowTextW(g_dateFormatPreview, buf);
}

static LRESULT CALLBACK DateFormatDlgProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
    case WM_ERASEBKGND:
    {
        LRESULT r = EraseDialogBg(hDlg, wParam);
        if (r) return r;
        break;
    }
    case WM_COMMAND:
        if (HIWORD(wParam) == EN_CHANGE && reinterpret_cast<HWND>(lParam) == g_dateFormatEdit)
        {
            UpdateDateFormatPreview();
            return 0;
        }
        if (LOWORD(wParam) == IDOK)
        {
            wchar_t buf[256] = {};
            GetWindowTextW(g_dateFormatEdit, buf, 256);
            if (buf[0] != L'\0')
            {
                g_state.dateTimeFormat = buf;
                SaveSettings();
            }
            DestroyWindow(hDlg);
            return 0;
        }
        if (LOWORD(wParam) == IDCANCEL)
        {
            DestroyWindow(hDlg);
            return 0;
        }
        if (LOWORD(wParam) == 1003)
        {
            SetWindowTextW(g_dateFormatEdit, L"%H:%M %d.%m.%Y");
            return 0;
        }
        break;
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLORBTN:
    case WM_CTLCOLORDLG:
    {
        INT_PTR r = HandleDialogDarkColors(msg, wParam);
        if (r) return static_cast<LRESULT>(r);
        break;
    }
    case WM_DRAWITEM:
        if (DrawDarkDialogButton(reinterpret_cast<const DRAWITEMSTRUCT *>(lParam)))
            return TRUE;
        break;
    case WM_CLOSE:
        DestroyWindow(hDlg);
        return 0;
    case WM_DESTROY:
        g_dateFormatEdit = nullptr;
        g_dateFormatPreview = nullptr;
        g_hwndDateFormatDlg = nullptr;
        return 0;
    }
    return DefWindowProcW(hDlg, msg, wParam, lParam);
}

void EditSettingsDateFormat()
{
    const auto &lang = GetLangStrings();

    const int W = 520, H = 270;
    const int PAD = 20;
    const int LBL_W = 90;
    const int ROW_H = 24;
    HWND hDlg = CreateAppDialog(lang.dialogDateFormatTitle.c_str(), W, H);
    if (!hDlg)
        return;

    int y = PAD;
    int editX = PAD + LBL_W;
    int editW = W - PAD * 2 - LBL_W;

    CreateWindowExW(0, L"STATIC", lang.dialogDateFormatLabel.c_str(),
                    WS_CHILD | WS_VISIBLE | SS_CENTERIMAGE,
                    PAD, y, LBL_W, ROW_H, hDlg, nullptr, nullptr, nullptr);
    g_dateFormatEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", g_state.dateTimeFormat.c_str(),
                                       WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | WS_TABSTOP,
                                       editX, y, editW, ROW_H,
                                       hDlg, reinterpret_cast<HMENU>(1001), nullptr, nullptr);

    y += ROW_H + 12;
    CreateWindowExW(0, L"STATIC", lang.dialogDateFormatPreview.c_str(),
                    WS_CHILD | WS_VISIBLE | SS_CENTERIMAGE,
                    PAD, y, LBL_W, ROW_H, hDlg, nullptr, nullptr, nullptr);
    g_dateFormatPreview = CreateWindowExW(0, L"STATIC", L"",
                                          WS_CHILD | WS_VISIBLE | SS_LEFTNOWORDWRAP | SS_CENTERIMAGE,
                                          editX, y, editW, ROW_H,
                                          hDlg, nullptr, nullptr, nullptr);

    y += ROW_H + 16;
    CreateWindowExW(0, L"STATIC", lang.dialogDateFormatHelp.c_str(),
                    WS_CHILD | WS_VISIBLE,
                    PAD, y, W - PAD * 2, 90,
                    hDlg, nullptr, nullptr, nullptr);

    const int btnW = 140, btnH = 28;
    const int smBtnW = 88;
    const int btnY = H - PAD - btnH;
    CreateWindowExW(0, L"BUTTON", lang.dialogDateFormatRestore.c_str(),
                    WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                    PAD, btnY, btnW, btnH, hDlg, reinterpret_cast<HMENU>(1003), nullptr, nullptr);
    CreateWindowExW(0, L"BUTTON", lang.dialogOK.c_str(),
                    WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON | WS_TABSTOP,
                    W - PAD - smBtnW * 2 - 8, btnY, smBtnW, btnH,
                    hDlg, reinterpret_cast<HMENU>(IDOK), nullptr, nullptr);
    CreateWindowExW(0, L"BUTTON", lang.dialogCancel.c_str(),
                    WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                    W - PAD - smBtnW, btnY, smBtnW, btnH,
                    hDlg, reinterpret_cast<HMENU>(IDCANCEL), nullptr, nullptr);

    ApplyDialogFont(hDlg);

    g_hwndDateFormatDlg = hDlg;
    SetWindowLongPtrW(hDlg, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(DateFormatDlgProc));
    ApplyDialogDarkMode(hDlg);
    CenterDialogOnParent(hDlg, g_hwndMain);
    UpdateDateFormatPreview();
    SetFocus(g_dateFormatEdit);
}
