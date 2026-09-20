#pragma once

#include <string>

void ToolsNormalizeText();
void ToolsBase64();
void ToolsBase64Decode();
void ToolsSha1();
void ToolsMd5();

// Text transforms. Each operates on the current selection, or — when
// there is no selection — on the whole document.
void ToolsUppercase();
void ToolsLowercase();
void ToolsTitleCase();
void ToolsTrimTrailing();
void ToolsTrimLines();

// Lorem ipsum generator. Paragraph lengths vary around avgWords; the result is
// inserted at the caret. Clamped internally to 1..500 paragraphs, 5..500 words.
std::wstring GenerateLoremIpsum(int paragraphs, int avgWords, bool startWithLorem, bool htmlTags);
void ToolsInsertLorem(int paragraphs, int avgWords, bool startWithLorem, bool htmlTags);
void ToolsTabsToSpaces();
void ToolsSpacesToTabs();
void ToolsReverseLines();
void ToolsJoinLines();
void ToolsRemoveEmptyLines();
void ToolsRemoveDuplicateLines();
void ToolsSortLinesAsc();
void ToolsSortLinesDesc();
void ToolsUrlEncode();
void ToolsUrlDecode();

// Adds/removes the top-level "Tools" menu from the menu bar based on
// g_state.toolsEnabled. Call after WM_CREATE and from the toggle handler.
void UpdateToolsMenuVisibility();
