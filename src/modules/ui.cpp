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

  User interface functions for window title, status bar, and control layout.
  Manages window resizing, status bar parts, and UI state synchronization.
*/

#include "ui.h"
#include "core/globals.h"
#include "lang/lang.h"
#include "editor.h"
#include "file.h"
#include "gutter.h"
#include <commctrl.h>
#include <shlwapi.h>

void UpdateTitle()
{
    const auto &lang = GetLangStrings();
    std::wstring filename = g_state.filePath.empty() ? lang.untitled : PathFindFileNameW(g_state.filePath.c_str());
    std::wstring title = (g_state.modified ? L"*" : L"") + filename + L" - " + lang.appName;
    SetWindowTextW(g_hwndMain, title.c_str());
}

void UpdateStatus()
{
    if (!g_state.showStatusBar)
    {
        ShowWindow(g_hwndStatus, SW_HIDE);
        return;
    }
    ShowWindow(g_hwndStatus, SW_SHOW);
    auto [line, col] = GetCursorPos();
    const auto &lang = GetLangStrings();

    int chars = static_cast<int>(SendMessageW(g_hwndEditor, WM_GETTEXTLENGTH, 0, 0));
    int totalLines = static_cast<int>(SendMessageW(g_hwndEditor, EM_GETLINECOUNT, 0, 0));

    DWORD selStart = 0, selEnd = 0;
    SendMessageW(g_hwndEditor, EM_GETSEL, reinterpret_cast<WPARAM>(&selStart), reinterpret_cast<LPARAM>(&selEnd));
    int selCount = static_cast<int>(selEnd - selStart);

    wchar_t buf[64];
    if (selCount > 0)
        swprintf(buf, 64, L"  %ls%d  ", lang.statusSelected.c_str(), selCount);
    else
        swprintf(buf, 64, L"  %ls%d  ", lang.statusChars.c_str(), chars);
    g_statusTexts[0] = buf;
    swprintf(buf, 64, L"  %ls%d  ", lang.statusLines.c_str(), totalLines);
    g_statusTexts[1] = buf;
    swprintf(buf, 64, L"  %ls%d  ", lang.statusLine.c_str(), line);
    g_statusTexts[2] = buf;
    swprintf(buf, 64, L"  %ls%d  ", lang.statusColumn.c_str(), col);
    g_statusTexts[3] = buf;
    g_statusTexts[4] = std::wstring(L"  ") + GetEncodingName(g_state.encoding) + L"  ";
    g_statusTexts[5] = std::wstring(L"  ") + GetLineEndingName(g_state.lineEnding) + L"  ";

    for (int i = 0; i < 6; i++)
        SendMessageW(g_hwndStatus, SB_SETTEXTW, i | SBT_NOBORDERS, reinterpret_cast<LPARAM>(g_statusTexts[i].c_str()));
    InvalidateRect(g_hwndStatus, nullptr, TRUE);
}

void SetupStatusBarParts()
{
    RECT rc;
    GetClientRect(g_hwndMain, &rc);
    HDC hdc = GetDC(g_hwndStatus);
    HFONT hFont = reinterpret_cast<HFONT>(SendMessageW(g_hwndStatus, WM_GETFONT, 0, 0));
    HFONT old = reinterpret_cast<HFONT>(SelectObject(hdc, hFont ? hFont : reinterpret_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT))));
    auto textW = [&](const wchar_t *s)
    {
        SIZE sz{};
        GetTextExtentPoint32W(hdc, s, static_cast<int>(wcslen(s)), &sz);
        return sz.cx + 16;
    };
    const auto &lang = GetLangStrings();
    int wChars = textW((L"  " + lang.statusChars + L"9999999  ").c_str());
    int wLines = textW((L"  " + lang.statusLines + L"999999  ").c_str());
    int wLine = textW((L"  " + lang.statusLine + L"999999  ").c_str());
    int wCol = textW((L"  " + lang.statusColumn + L"9999  ").c_str());
    int wEnc = textW(L"  UTF-8 with BOM  ");
    int wLE = textW(L"  Windows (CRLF)  ");
    SelectObject(hdc, old);
    ReleaseDC(g_hwndStatus, hdc);
    (void)rc;
    int parts[6] = {
        wChars,
        wChars + wLines,
        wChars + wLines + wLine,
        wChars + wLines + wLine + wCol,
        wChars + wLines + wLine + wCol + wEnc,
        -1
    };
    (void)wLE;
    SendMessageW(g_hwndStatus, SB_SETPARTS, 6, reinterpret_cast<LPARAM>(parts));
}

void ResizeControls()
{
    RECT rc;
    GetClientRect(g_hwndMain, &rc);
    int statusH = 0;
    if (g_state.showStatusBar)
    {
        ShowWindow(g_hwndStatus, SW_SHOW);
        SendMessageW(g_hwndStatus, WM_SIZE, 0, 0);
        RECT rs;
        GetWindowRect(g_hwndStatus, &rs);
        statusH = rs.bottom - rs.top;
    }
    else
        ShowWindow(g_hwndStatus, SW_HIDE);

    const int pad = 2;

    UpdateGutterWidth();
    int gutterW = g_state.showLineNumbers ? GetGutterWidth() : 0;

    int areaH = rc.bottom - statusH - pad * 2;
    if (areaH < 0)
        areaH = 0;

    if (g_hwndGutter)
    {
        if (g_state.showLineNumbers && gutterW > 0)
        {
            ShowWindow(g_hwndGutter, SW_SHOW);
            MoveWindow(g_hwndGutter, pad, pad, gutterW, areaH, TRUE);
        }
        else
        {
            ShowWindow(g_hwndGutter, SW_HIDE);
        }
    }

    // Small visual gap between the gutter and the editor text — without
    // it the line number sits flush against the first character.
    const int gutterGap = (gutterW > 0) ? 2 : 0;
    int editorX = pad + gutterW + gutterGap;
    int editorW = rc.right - editorX - pad;
    if (editorW < 0)
        editorW = 0;
    MoveWindow(g_hwndEditor, editorX, pad, editorW, areaH, TRUE);
    if (g_hwndGutter && g_state.showLineNumbers)
        InvalidateRect(g_hwndGutter, nullptr, FALSE);
    SetupStatusBarParts();
}
