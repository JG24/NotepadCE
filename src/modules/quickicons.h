#pragma once

#include <windows.h>

// Right-justified owner-drawn menu bar items (spell check, always on top,
// dark mode) — present only when g_state.quickAccessIcons is true.
void UpdateQuickIconsVisibility();

bool IsQuickIconId(UINT id);
void MeasureQuickIconItem(MEASUREITEMSTRUCT *mis);
void DrawQuickIconItem(const DRAWITEMSTRUCT *dis);
void HandleQuickIconClick(UINT id);

// Refresh just the icons (call after state toggles from any path).
void RefreshQuickIcons();

// Hover tooltips for the quick-access icons. ShowQuickIconTooltip is driven
// from the main window's WM_NCMOUSEMOVE with the cursor in screen coords; it
// shows / hides a tracking tooltip depending on which icon (if any) is under
// the cursor. HideQuickIconTooltip is called from WM_NCMOUSELEAVE.
void ShowQuickIconTooltip(POINT ptScreen);
void HideQuickIconTooltip();
