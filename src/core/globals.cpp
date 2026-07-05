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

  Global variable definitions for the notepad application storing runtime state.
  Initializes all shared resources like window handles, GDI objects, and app state.
*/

#include "globals.h"

HWND g_hwndMain = nullptr;
HWND g_hwndEditor = nullptr;
HWND g_hwndGutter = nullptr;
HWND g_hwndSnippets = nullptr;
HWND g_hwndStatus = nullptr;
HWND g_hwndFindDlg = nullptr;
HWND g_hwndTransparencyDlg = nullptr;
HWND g_hwndDateFormatDlg = nullptr;
HWND g_hwndAboutDlg = nullptr;
HACCEL g_hAccel = nullptr;
AppState g_state;
WNDPROC g_origEditorProc = nullptr;
WNDPROC g_origStatusProc = nullptr;
HBRUSH g_hbrStatusDark = nullptr;
HBRUSH g_hbrMenuDark = nullptr;
PAGESETUPDLGW g_pageSetup = {};
std::wstring g_statusTexts[6];
UINT g_msgFindReplace = 0;
