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

  Theme management module providing dark mode support and visual style functions.
  Handles Windows immersive dark mode APIs and applies theme to all UI elements.
*/

#pragma once

#include <windows.h>
#include "core/types.h"

// IsDarkMode() returns true for both Dark and Matrix themes — the window
// chrome (title bar, menu bar, scrollbars) is dark in both. IsMatrixTheme()
// distinguishes the two so the editor area can be tinted green for Matrix.
bool IsDarkMode();
bool IsMatrixTheme();
COLORREF GetEditorBgColor();
COLORREF GetEditorTextColor();
bool SetTitleBarDark(HWND hwnd, BOOL dark);
void ApplyTheme();
void ToggleDarkMode();
// Sets an explicit theme (Light / Dark / Matrix), applies it and saves.
void SetThemeChoice(Theme t);
LRESULT CALLBACK StatusSubclassProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
