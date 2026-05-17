#pragma once
#include <windows.h>
#include <string>
#include <vector>

struct SpellError
{
    int start;
    int length;
};

bool InitSpellCheck();
void ShutdownSpellCheck();

// Returns true if a language-specific checker is loaded and ready.
bool IsSpellCheckAvailable();

// Returns true if the runtime supports the given BCP-47 language tag
// (e.g. "pl-PL") and a checker was created. False = no system dictionary
// for that language; user needs to install the language pack.
bool SetSpellCheckLanguage(const wchar_t *langTag);

std::vector<SpellError> CheckSpelling(const std::wstring &text);
std::vector<std::wstring> GetSuggestions(const std::wstring &word);
void AddWordToDictionary(const std::wstring &word);

// Editor integration. ApplySpellingMarks scans the whole editor buffer
// and updates wave underlines. ClearSpellingMarks removes all of them.
// ScheduleSpellCheck debounces the actual scan via a Windows timer on
// g_hwndMain — call it from EN_CHANGE; the timer ID lives in this header.
void ApplySpellingMarks();
void ClearSpellingMarks();
void ScheduleSpellCheck();

#define SPELL_CHECK_TIMER_ID 0xDEAD0001
#define SPELL_CHECK_DEBOUNCE_MS 400
