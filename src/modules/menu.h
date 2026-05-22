#pragma once

#include <windows.h>

void UpdateMenuStrings();
void UpdateLanguageMenu();

// Owner-drawn top-level menu bar items, used only in dark mode.
// Windows 11 stopped firing WM_UAHDRAWMENUITEM for regular menu bar
// items, so the only way to control their colour is to flip them to
// MFT_OWNERDRAW and paint via WM_DRAWITEM (which is documented and
// always fires). In light mode we leave them alone — system paint is
// fine. EnableOwnerDrawMenuBar idempotently converts / reverts.
void EnableOwnerDrawMenuBar(HWND hwnd, bool enable);
bool IsTopLevelMenuItemId(UINT id);
void MeasureTopLevelMenuItem(MEASUREITEMSTRUCT *mis);
void DrawTopLevelMenuItem(const DRAWITEMSTRUCT *dis);

// Posted to the main window when the first WM_DRAWITEM has measured the
// hidden gutter Windows adds to owner-drawn menu items, so the bar can
// be rebuilt once with corrected widths.
#define WM_MENU_REMEASURE (WM_APP + 0x21)
