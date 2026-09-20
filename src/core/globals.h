/*
  Global variable declarations shared across all application modules for window handles.
  Contains handles for main window, editor, status bar, dialogs, and GDI resources.
*/

#pragma once

#include <windows.h>
#include <commdlg.h>
#include "types.h"

extern HWND g_hwndMain;
extern HWND g_hwndEditor;
extern HWND g_hwndGutter;
extern HWND g_hwndSnippets;
extern HWND g_hwndStatus;
extern HWND g_hwndFindDlg;
extern HWND g_hwndTransparencyDlg;
extern HWND g_hwndDateFormatDlg;
extern HWND g_hwndAboutDlg;
extern HWND g_hwndGotoDlg;
extern HWND g_hwndLoremDlg;
// RichEdit window class chosen in WM_CREATE (msftedit, or the riched20
// fallback) — ApplyWordWrap recreates the editor with the same class.
extern const wchar_t *g_editorClass;
extern HACCEL g_hAccel;
extern AppState g_state;
extern WNDPROC g_origEditorProc;
extern WNDPROC g_origStatusProc;
extern HBRUSH g_hbrStatusDark;
extern HBRUSH g_hbrMenuDark;
extern PAGESETUPDLGW g_pageSetup;
extern std::wstring g_statusTexts[6];
extern UINT g_msgFindReplace;
