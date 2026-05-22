#pragma once

void ToolsNormalizeText();
void ToolsBase64();
void ToolsSha1();
void ToolsMd5();

// Text transforms. Each operates on the current selection, or — when
// there is no selection — on the whole document.
void ToolsUppercase();
void ToolsLowercase();
void ToolsTitleCase();
void ToolsTrimTrailing();
void ToolsTabsToSpaces();
void ToolsSpacesToTabs();
void ToolsReverseLines();
void ToolsJoinLines();

// Adds/removes the top-level "Tools" menu from the menu bar based on
// g_state.toolsEnabled. Call after WM_CREATE and from the toggle handler.
void UpdateToolsMenuVisibility();
