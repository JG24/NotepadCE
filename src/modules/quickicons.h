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
