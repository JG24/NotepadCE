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

  Core type definitions including enums and structs for the notepad application.
  Provides encoding types, line endings, themes, background settings and app state.
*/

#pragma once

#include <windows.h>
#include <string>
#include <deque>
#include <vector>

#define APP_NAME L"Notepad"
#define ZOOM_MIN 25
#define ZOOM_MAX 500
#define ZOOM_DEFAULT 100
#define MAX_RECENT_FILES 10

#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif

#define WM_UAHDRAWMENU 0x0091
#define WM_UAHDRAWMENUITEM 0x0092

enum class Encoding
{
    UTF8,
    UTF8BOM,
    UTF16LE,
    UTF16BE,
    ANSI
};
enum class LineEnding
{
    CRLF,
    LF,
    CR
};
enum class Theme
{
    System,
    Light,
    Dark,
    Matrix
};
enum PreferredAppMode
{
    Default,
    AllowDark,
    ForceDark,
    ForceLight,
    Max
};
struct UAHMENUITEM
{
    int iPosition;
    UINT32 dwFlags;
    HMENU hMenu;
    RECT rcItem;
};
struct UAHMENU
{
    HMENU hMenu;
    HDC hdc;
    DWORD dwFlags;
};
struct UAHDRAWMENUITEM
{
    DRAWITEMSTRUCT dis;
    UAHMENU um;
    UAHMENUITEM umi;
};
struct AppState
{
    std::wstring filePath;
    bool modified = false;
    Encoding encoding = Encoding::UTF8;
    LineEnding lineEnding = LineEnding::CRLF;
    std::wstring findText;
    std::wstring replaceText;
    bool wordWrap = false;
    int zoomLevel = ZOOM_DEFAULT;
    bool showStatusBar = true;
    Theme theme = Theme::System;
    int fontSize = 11;
    std::wstring fontName = L"Consolas";
    int fontWeight = FW_NORMAL;
    bool fontItalic = false;
    bool fontUnderline = false;
    BYTE windowOpacity = 255;
    bool alwaysOnTop = false;
    bool closing = false;
    HFONT hFont = nullptr;
    std::deque<std::wstring> recentFiles;
    std::wstring dateTimeFormat = L"%Y-%m-%d %H:%M:%S";
    bool spellCheckEnabled = true;
    std::wstring spellCheckLanguage = L"pl-PL";
    bool showSpecialChars = false;
    bool showLineNumbers = false;
    bool toolsEnabled = false;
    bool quickAccessIcons = false;
    // Editor display extras — both OFF by default so a fresh start matches
    // classic Notepad. Toggled from Edit > Settings; drawn as a GDI overlay
    // in the editor's WM_PAINT (no RichEdit formatting touched).
    bool highlightCurrentLine = false;
    bool highlightOccurrences = false;
    // Snippets side panel (tree over <exe dir>\Snippets).
    bool snippetsPanelVisible = false;
    int snippetsPanelWidth = 200;
    // Expanded folders (paths relative to the Snippets root), persisted so
    // the tree reopens exactly as it was closed.
    std::vector<std::wstring> snippetsExpanded;
    int windowX = CW_USEDEFAULT;
    int windowY = CW_USEDEFAULT;
    int windowWidth = 640;
    int windowHeight = 480;
};

typedef BOOL(WINAPI *fnAllowDarkModeForWindow)(HWND hWnd, BOOL allow);
typedef void(WINAPI *fnAllowDarkModeForApp)(BOOL allow);
typedef PreferredAppMode(WINAPI *fnSetPreferredAppMode)(PreferredAppMode appMode);
typedef void(WINAPI *fnFlushMenuThemes)();
typedef void(WINAPI *fnRefreshImmersiveColorPolicyState)();
