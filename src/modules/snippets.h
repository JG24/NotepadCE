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

  Snippets side panel — a TreeView over the real directory <exe dir>\Snippets
  (.txt files in real sub-folders). Toggled from the quick-access icons;
  double-click opens a snippet for editing, the context menu can insert one
  at the caret, create / rename / delete entries.
*/

#pragma once

#include <windows.h>

// Posted to the main window when the tree must be rebuilt outside a
// TreeView notification handler (after a rename finishes, for example —
// deleting all items from inside TVN_ENDLABELEDIT is not safe).
#define WM_SNIPPETS_REFRESH (WM_APP + 20)

// Width of the draggable gap between the editor and the panel.
const int SNIPPETS_SPLITTER_W = 5;

void CreateSnippetsWindow(HWND parent);
// Show / hide per g_state.snippetsPanelVisible: refreshes the tree when
// turning on, then relayouts (ResizeControls does the actual show/hide).
void UpdateSnippetsVisibility();
void RefreshSnippetTree();
void ApplySnippetsTheme();

// Width the panel occupies in the main-window layout — 0 when hidden,
// otherwise the saved width clamped to [min, half the client area].
int SnippetsLayoutWidth();

// TreeView notifications routed from the main window's WM_NOTIFY.
// *handled is set when the notification was consumed.
LRESULT HandleSnippetsNotify(NMHDR *pnmh, bool *handled);

// Paints the 1px separator line in the splitter gap; called from the main
// window's WM_PAINT. No-op while the panel is hidden.
void DrawSnippetsSplitter(HDC hdc);

// Splitter drag, driven from the main window's mouse messages.
// Each returns true when the message was consumed.
bool SnippetsHandleSetCursor();
bool SnippetsHandleLButtonDown(LPARAM lParam);
bool SnippetsHandleMouseMove(LPARAM lParam);
bool SnippetsHandleLButtonUp();
