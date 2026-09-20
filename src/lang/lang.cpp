#include "lang.h"
#include "en.h"
#include "ja.h"
#include "pl.h"
#include "de.h"
#include "cs.h"
#include "uk.h"
#include "lt.h"
#include "ru.h"
#include "zh.h"
#include "../modules/settings.h"
#include <windows.h>

static LangID g_currentLang = LangID::EN;
static const LangStrings *g_currentStrings = &g_langEN;

LangID LoadLanguageSetting()
{
    return g_currentLang;
}

void SaveLanguageSetting()
{
    if (IsSettingsLoaded())
        SaveSettings();
}

void InitLanguage()
{
    // Language is loaded from NotepadCE.json by LoadFontSettings()
    // which calls SetLanguage() directly. This function is a safety
    // backstop that applies the current default if nothing else did.
    SetLanguage(g_currentLang);
}

void SetLanguage(LangID lang)
{
    g_currentLang = lang;

    switch (lang)
    {
    case LangID::JA:
        g_currentStrings = &g_langJA;
        break;
    case LangID::PL:
        g_currentStrings = &g_langPL;
        break;
    case LangID::DE:
        g_currentStrings = &g_langDE;
        break;
    case LangID::CS:
        g_currentStrings = &g_langCS;
        break;
    case LangID::UK:
        g_currentStrings = &g_langUK;
        break;
    case LangID::LT:
        g_currentStrings = &g_langLT;
        break;
    case LangID::RU:
        g_currentStrings = &g_langRU;
        break;
    case LangID::ZH:
        g_currentStrings = &g_langZH;
        break;
    case LangID::EN:
    default:
        g_currentStrings = &g_langEN;
        break;
    }

    if (IsSettingsLoaded())
        SaveSettings();
}

LangID GetCurrentLanguage()
{
    return g_currentLang;
}

const LangStrings &GetLangStrings()
{
    return *g_currentStrings;
}

const std::wstring &GetString([[maybe_unused]] const std::wstring &key)
{
    static std::wstring empty;
    return empty;
}
