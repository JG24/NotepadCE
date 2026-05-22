/*
  Line-number gutter painted left of the editor. Mirrors the editor's
  visible-line range and reads line positions via EM_GETFIRSTVISIBLELINE
  + EM_POSFROMCHAR + EM_LINEINDEX. Repainted whenever the editor itself
  repaints (the editor subclass invalidates this window from WM_PAINT).
*/

#include "gutter.h"
#include "core/globals.h"
#include "core/types.h"
#include "theme.h"
#include <richedit.h>
#include <string>
#include <cwchar>

static int g_gutterWidth = 0;

static int CountDigits(int n)
{
    int d = 0;
    if (n <= 0)
        return 1;
    while (n > 0)
    {
        ++d;
        n /= 10;
    }
    return d;
}

bool UpdateGutterWidth()
{
    if (!g_state.showLineNumbers || !g_hwndEditor)
    {
        if (g_gutterWidth != 0)
        {
            g_gutterWidth = 0;
            return true;
        }
        return false;
    }

    int totalLines = static_cast<int>(SendMessageW(g_hwndEditor, EM_GETLINECOUNT, 0, 0));
    int digits = CountDigits(totalLines);
    if (digits < 3)
        digits = 3;

    HWND hMeasure = g_hwndGutter ? g_hwndGutter : g_hwndEditor;
    HDC hdc = GetDC(hMeasure);
    HFONT hFont = reinterpret_cast<HFONT>(SendMessageW(g_hwndEditor, WM_GETFONT, 0, 0));
    HFONT oldFont = hFont ? reinterpret_cast<HFONT>(SelectObject(hdc, hFont)) : nullptr;
    SIZE sz{};
    GetTextExtentPoint32W(hdc, L"0", 1, &sz);
    if (oldFont)
        SelectObject(hdc, oldFont);
    ReleaseDC(hMeasure, hdc);

    int newWidth = sz.cx * digits + 14;
    if (newWidth == g_gutterWidth)
        return false;
    g_gutterWidth = newWidth;
    return true;
}

int GetGutterWidth()
{
    return g_gutterWidth;
}

static LRESULT CALLBACK GutterWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        RECT rc;
        GetClientRect(hwnd, &rc);

        bool dark = IsDarkMode();
        COLORREF bg, fg;
        if (IsMatrixTheme())
        {
            bg = RGB(4, 14, 4);
            fg = RGB(50, 160, 65);
        }
        else if (dark)
        {
            bg = RGB(35, 35, 35);
            fg = RGB(140, 140, 140);
        }
        else
        {
            bg = RGB(240, 240, 240);
            fg = RGB(120, 120, 120);
        }
        HBRUSH hbr = CreateSolidBrush(bg);
        FillRect(hdc, &rc, hbr);
        DeleteObject(hbr);

        if (!g_hwndEditor)
        {
            EndPaint(hwnd, &ps);
            return 0;
        }

        HFONT hFont = reinterpret_cast<HFONT>(SendMessageW(g_hwndEditor, WM_GETFONT, 0, 0));
        HFONT oldFont = hFont ? reinterpret_cast<HFONT>(SelectObject(hdc, hFont)) : nullptr;
        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, fg);

        int firstLine  = static_cast<int>(SendMessageW(g_hwndEditor, EM_GETFIRSTVISIBLELINE, 0, 0));
        int totalLines = static_cast<int>(SendMessageW(g_hwndEditor, EM_GETLINECOUNT, 0, 0));

        RECT erc;
        GetClientRect(g_hwndEditor, &erc);

        // Logical-line numbering with word wrap: we want a number only on
        // the first visual line of each logical line. With wrap on,
        // EM_GETLINECOUNT counts visual lines. A new logical line starts
        // whenever there's a CR/LF gap between the previous visual line's
        // end and the current visual line's start. We iterate from 0 to
        // keep a running logical counter — invisible lines are cheap (just
        // EM_LINEINDEX / EM_LINELENGTH, no layout).
        int logicalLine = 0;
        int prevEnd = 0;

        for (int line = 0; line < totalLines; ++line)
        {
            int lineStart = static_cast<int>(SendMessageW(g_hwndEditor, EM_LINEINDEX, line, 0));
            if (lineStart < 0)
                break;
            int lineLen = static_cast<int>(SendMessageW(g_hwndEditor, EM_LINELENGTH, lineStart, 0));

            bool isLogicalStart = (line == 0) || (prevEnd < lineStart);
            if (isLogicalStart)
                ++logicalLine;

            prevEnd = lineStart + lineLen;

            if (line < firstLine)
                continue;

            POINTL pt{};
            SendMessageW(g_hwndEditor, EM_POSFROMCHAR, reinterpret_cast<WPARAM>(&pt), lineStart);
            if (pt.y > erc.bottom)
                break;

            if (!isLogicalStart)
                continue;

            wchar_t buf[16];
            int blen = swprintf(buf, 16, L"%d", logicalLine);
            if (blen < 0)
                continue;

            RECT rcText;
            rcText.left = 0;
            rcText.top = pt.y;
            rcText.right = rc.right - 5;
            rcText.bottom = pt.y + 40;
            DrawTextW(hdc, buf, blen, &rcText, DT_RIGHT | DT_TOP | DT_SINGLELINE | DT_NOPREFIX);
        }

        if (oldFont)
            SelectObject(hdc, oldFont);
        EndPaint(hwnd, &ps);
        return 0;
    }
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

void RegisterGutterClass()
{
    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = GutterWndProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = L"NotepadCEGutter";
    RegisterClassExW(&wc);
}

void CreateGutterWindow(HWND parent)
{
    g_hwndGutter = CreateWindowExW(0, L"NotepadCEGutter", nullptr,
                                   WS_CHILD,
                                   0, 0, 0, 0,
                                   parent, nullptr, GetModuleHandleW(nullptr), nullptr);
}
