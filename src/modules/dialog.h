/*
  Dialog box implementations for find, replace, goto, font selection, and more.
  Provides modeless and modal dialog creation with proper event handling.
*/

#pragma once
#include <windows.h>
#include <commdlg.h>

void DoFind(bool forward, bool matchCase = false);
void EditFind();
void EditFindNext();
void EditFindPrev();
void EditReplace();
void EditGoto();
void FormatFont();
void ViewTransparency();
void EditSettingsDateFormat();

// Small wizard for the Lorem ipsum generator (paragraph count, average
// paragraph length, optional "Lorem ipsum" opening, optional <p> wrapping).
void ToolsLoremIpsum();
void HelpAbout();
void HandleFindReplaceMessage(LPFINDREPLACEW pfr);
