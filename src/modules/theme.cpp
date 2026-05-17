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

  Theme management implementation with Windows dark mode API integration support.
  Controls visual appearance of title bar, menu bar, editor, and status controls.
*/

#include <windows.h>
#include <uxtheme.h>
#include <richedit.h>
#include "theme.h"
#include "core/types.h"
#include "core/globals.h"
#include "resource.h"
#include "settings.h"

bool SetTitleBarDark(HWND hwnd, BOOL dark)
{
    HMODULE hDwmapi = LoadLibraryW(L"dwmapi.dll");
    if (!hDwmapi)
        return false;

    typedef HRESULT(WINAPI * fnDwmSetWindowAttribute)(HWND, DWORD, LPCVOID, DWORD);
#ifdef __GNUC__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcast-function-type"
#endif
    auto dwmSetWindowAttribute = reinterpret_cast<fnDwmSetWindowAttribute>(GetProcAddress(hDwmapi, "DwmSetWindowAttribute"));
#ifdef __GNUC__
#pragma GCC diagnostic pop
#endif

    bool applied = false;
    if (dwmSetWindowAttribute)
    {
        const DWORD attrs[] = {DWMWA_USE_IMMERSIVE_DARK_MODE, 19};
        for (DWORD attr : attrs)
        {
            if (SUCCEEDED(dwmSetWindowAttribute(hwnd, attr, &dark, sizeof(dark))))
                applied = true;
        }
    }
    FreeLibrary(hDwmapi);
    return applied;
}

bool IsDarkMode()
{
    if (g_state.theme == Theme::Dark)
        return true;
    if (g_state.theme == Theme::Light)
        return false;
    HKEY hKey;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize", 0, KEY_READ, &hKey) == ERROR_SUCCESS)
    {
        DWORD value = 1, size = sizeof(value);
        if (RegQueryValueExW(hKey, L"AppsUseLightTheme", nullptr, nullptr, reinterpret_cast<LPBYTE>(&value), &size) == ERROR_SUCCESS)
        {
            RegCloseKey(hKey);
            return value == 0;
        }
        RegCloseKey(hKey);
    }
    return false;
}

LRESULT CALLBACK StatusSubclassProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (IsDarkMode())
    {
        if (msg == WM_ERASEBKGND)
        {
            RECT rc;
            GetClientRect(hwnd, &rc);
            HBRUSH hbr = g_hbrStatusDark ? g_hbrStatusDark : CreateSolidBrush(RGB(45, 45, 45));
            FillRect(reinterpret_cast<HDC>(wParam), &rc, hbr);
            if (!g_hbrStatusDark)
                DeleteObject(hbr);
            return 1;
        }
        if (msg == WM_PAINT)
        {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);
            RECT rc;
            GetClientRect(hwnd, &rc);
            HBRUSH hbr = g_hbrStatusDark ? g_hbrStatusDark : CreateSolidBrush(RGB(45, 45, 45));
            FillRect(hdc, &rc, hbr);
            if (!g_hbrStatusDark)
                DeleteObject(hbr);
            NONCLIENTMETRICSW ncm = {};
            ncm.cbSize = sizeof(ncm);
            SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0);
            HFONT hFont = CreateFontIndirectW(&ncm.lfStatusFont);
            HFONT hOldFont = reinterpret_cast<HFONT>(SelectObject(hdc, hFont));
            SetBkMode(hdc, TRANSPARENT);
            SetTextColor(hdc, RGB(255, 255, 255));
            int parts[6];
            int partCount = static_cast<int>(SendMessageW(hwnd, SB_GETPARTS, 6, reinterpret_cast<LPARAM>(parts)));
            int left = 4;
            for (int i = 0; i < partCount && i < 6; i++)
            {
                RECT rcPart = {left, rc.top + 2, (parts[i] == -1) ? rc.right : parts[i], rc.bottom - 2};
                wchar_t szText[256] = {};
                SendMessageW(hwnd, SB_GETTEXTW, i, reinterpret_cast<LPARAM>(szText));
                if (szText[0] != L'\0')
                    DrawTextW(hdc, szText, -1, &rcPart, DT_SINGLELINE | DT_VCENTER | DT_LEFT | DT_END_ELLIPSIS);
                left = rcPart.right + 2;
            }
            SelectObject(hdc, hOldFont);
            DeleteObject(hFont);
            EndPaint(hwnd, &ps);
            return 0;
        }
    }
    return CallWindowProcW(g_origStatusProc, hwnd, msg, wParam, lParam);
}

void ApplyTheme()
{
    BOOL dark = IsDarkMode();
    HMODULE hUxtheme = GetModuleHandleW(L"uxtheme.dll");
    if (hUxtheme)
    {
#ifdef __GNUC__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcast-function-type"
#endif
        auto allowDarkModeForApp = reinterpret_cast<fnAllowDarkModeForApp>(GetProcAddress(hUxtheme, MAKEINTRESOURCEA(132)));
        if (allowDarkModeForApp)
            allowDarkModeForApp(dark);
        auto setPreferredAppMode = reinterpret_cast<fnSetPreferredAppMode>(GetProcAddress(hUxtheme, MAKEINTRESOURCEA(135)));
        if (setPreferredAppMode)
            setPreferredAppMode(dark ? ForceDark : ForceLight);
        auto refreshPolicy = reinterpret_cast<fnRefreshImmersiveColorPolicyState>(GetProcAddress(hUxtheme, MAKEINTRESOURCEA(104)));
        if (refreshPolicy)
            refreshPolicy();
        auto allowDarkModeForWindow = reinterpret_cast<fnAllowDarkModeForWindow>(GetProcAddress(hUxtheme, MAKEINTRESOURCEA(133)));
        if (allowDarkModeForWindow)
        {
            allowDarkModeForWindow(g_hwndMain, dark);
            allowDarkModeForWindow(g_hwndStatus, dark);
            allowDarkModeForWindow(g_hwndEditor, dark);
        }
        auto flushMenuThemes = reinterpret_cast<fnFlushMenuThemes>(GetProcAddress(hUxtheme, MAKEINTRESOURCEA(136)));
        if (flushMenuThemes)
            flushMenuThemes();
#ifdef __GNUC__
#pragma GCC diagnostic pop
#endif
    }
    if (dark)
    {
        if (!g_hbrStatusDark)
            g_hbrStatusDark = CreateSolidBrush(RGB(45, 45, 45));
        if (!g_hbrMenuDark)
            g_hbrMenuDark = CreateSolidBrush(RGB(45, 45, 45));
    }
    else
    {
        if (g_hbrStatusDark)
        {
            DeleteObject(g_hbrStatusDark);
            g_hbrStatusDark = nullptr;
        }
        if (g_hbrMenuDark)
        {
            DeleteObject(g_hbrMenuDark);
            g_hbrMenuDark = nullptr;
        }
    }
    SetTitleBarDark(g_hwndMain, dark);
    SetWindowTheme(g_hwndEditor, dark ? L"DarkMode_Explorer" : nullptr, nullptr);
    SetWindowTheme(g_hwndStatus, dark ? L"DarkMode_Explorer" : nullptr, nullptr);
    SetWindowTheme(g_hwndMain, dark ? L"DarkMode_Explorer" : nullptr, nullptr);
    COLORREF bgColor = dark ? RGB(30, 30, 30) : GetSysColor(COLOR_WINDOW);
    COLORREF textColor = dark ? RGB(255, 255, 255) : GetSysColor(COLOR_WINDOWTEXT);
    SendMessageW(g_hwndEditor, EM_SETBKGNDCOLOR, 0, bgColor);
    CHARFORMAT2W cf = {};
    cf.cbSize = sizeof(cf);
    // Also reset CFM_BACKCOLOR with CFE_AUTOBACKCOLOR — earlier paste paths
    // bake an explicit background colour into the char format. Without
    // clearing it here, dark-mode-pasted text retains its dark per-char
    // background after switching to light mode (resulting in dark squares
    // with black text).
    cf.dwMask = CFM_COLOR | CFM_BACKCOLOR;
    cf.dwEffects = CFE_AUTOBACKCOLOR;
    cf.crTextColor = textColor;
    // SCF_ALL fires EN_CHANGE because RichEdit treats per-char format
    // changes as edits. We don't want that: switching the theme isn't a
    // user edit, so suppress notifications around the call. Otherwise
    // toggling dark/light marks an unmodified file as modified and the
    // app prompts to save on exit.
    LRESULT oldMask = SendMessageW(g_hwndEditor, EM_SETEVENTMASK, 0, 0);
    SendMessageW(g_hwndEditor, EM_SETCHARFORMAT, SCF_ALL, reinterpret_cast<LPARAM>(&cf));
    SendMessageW(g_hwndEditor, EM_SETCHARFORMAT, SCF_DEFAULT, reinterpret_cast<LPARAM>(&cf));
    SendMessageW(g_hwndEditor, EM_SETEVENTMASK, 0, oldMask);
    SendMessageW(g_hwndStatus, SB_SETBKCOLOR, 0, dark ? RGB(45, 45, 45) : CLR_DEFAULT);
    CheckMenuItem(GetMenu(g_hwndMain), IDM_VIEW_DARKMODE, MF_BYCOMMAND | (dark ? MF_CHECKED : MF_UNCHECKED));
    InvalidateRect(g_hwndEditor, nullptr, TRUE);
    InvalidateRect(g_hwndStatus, nullptr, TRUE);
    InvalidateRect(g_hwndMain, nullptr, TRUE);
    if (g_hwndGutter)
        InvalidateRect(g_hwndGutter, nullptr, TRUE);
    DrawMenuBar(g_hwndMain);
}

void ToggleDarkMode()
{
    g_state.theme = IsDarkMode() ? Theme::Light : Theme::Dark;
    ApplyTheme();
    SaveSettings();
}
