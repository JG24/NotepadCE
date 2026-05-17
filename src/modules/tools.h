#pragma once

void ToolsNormalizeText();
void ToolsBase64();
void ToolsSha1();
void ToolsMd5();

// Adds/removes the top-level "Tools" menu from the menu bar based on
// g_state.toolsEnabled. Call after WM_CREATE and from the toggle handler.
void UpdateToolsMenuVisibility();
