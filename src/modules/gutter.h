#pragma once

#include <windows.h>

void RegisterGutterClass();
void CreateGutterWindow(HWND parent);

// Recompute gutter width based on total line count + font metrics.
// Returns true if the width changed (caller should re-layout).
bool UpdateGutterWidth();

int GetGutterWidth();
