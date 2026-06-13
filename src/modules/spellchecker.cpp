/*
  Spell checking via Windows Spell Checking API (Win8+).
  Dictionary data is provided by the OS — no shipped dictionary files.
  Requires the relevant language pack installed (Settings → Language).
*/

#include "spellchecker.h"
#include "core/globals.h"
#include "editor.h"
#include <spellcheck.h>
#include <richedit.h>
#include <objbase.h>

static ISpellCheckerFactory *g_factory = nullptr;
static ISpellChecker *g_checker = nullptr;
static bool g_comInit = false;

bool InitSpellCheck()
{
    HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (SUCCEEDED(hr) || hr == S_FALSE)
        g_comInit = true;
    else if (hr != RPC_E_CHANGED_MODE)
        return false;

    hr = CoCreateInstance(__uuidof(SpellCheckerFactory), nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&g_factory));
    if (FAILED(hr))
        return false;

    return SetSpellCheckLanguage(g_state.spellCheckLanguage.c_str());
}

void ShutdownSpellCheck()
{
    if (g_checker)
    {
        g_checker->Release();
        g_checker = nullptr;
    }
    if (g_factory)
    {
        g_factory->Release();
        g_factory = nullptr;
    }
    if (g_comInit)
    {
        CoUninitialize();
        g_comInit = false;
    }
}

bool IsSpellCheckAvailable()
{
    return g_checker != nullptr;
}

bool SetSpellCheckLanguage(const wchar_t *langTag)
{
    if (!g_factory || !langTag || !langTag[0])
        return false;
    if (g_checker)
    {
        g_checker->Release();
        g_checker = nullptr;
    }
    BOOL supported = FALSE;
    if (FAILED(g_factory->IsSupported(langTag, &supported)) || !supported)
        return false;
    HRESULT hr = g_factory->CreateSpellChecker(langTag, &g_checker);
    return SUCCEEDED(hr);
}

std::vector<SpellError> CheckSpelling(const std::wstring &text)
{
    std::vector<SpellError> errors;
    if (!g_checker || text.empty())
        return errors;
    IEnumSpellingError *pEnum = nullptr;
    if (FAILED(g_checker->Check(text.c_str(), &pEnum)) || !pEnum)
        return errors;
    ISpellingError *pErr = nullptr;
    while (pEnum->Next(&pErr) == S_OK)
    {
        ULONG start = 0, len = 0;
        pErr->get_StartIndex(&start);
        pErr->get_Length(&len);
        CORRECTIVE_ACTION action = CORRECTIVE_ACTION_NONE;
        pErr->get_CorrectiveAction(&action);
        if (action == CORRECTIVE_ACTION_GET_SUGGESTIONS || action == CORRECTIVE_ACTION_REPLACE)
            errors.push_back({static_cast<int>(start), static_cast<int>(len)});
        pErr->Release();
    }
    pEnum->Release();
    return errors;
}

std::vector<std::wstring> GetSuggestions(const std::wstring &word)
{
    std::vector<std::wstring> result;
    if (!g_checker || word.empty())
        return result;
    IEnumString *pEnum = nullptr;
    if (FAILED(g_checker->Suggest(word.c_str(), &pEnum)) || !pEnum)
        return result;
    LPOLESTR sug = nullptr;
    while (pEnum->Next(1, &sug, nullptr) == S_OK && result.size() < 10)
    {
        result.push_back(sug);
        CoTaskMemFree(sug);
    }
    pEnum->Release();
    return result;
}

void AddWordToDictionary(const std::wstring &word)
{
    if (!g_checker)
        return;
    g_checker->Add(word.c_str());
}

// Normalize text from GetEditorText() (which uses CRLF) into RichEdit
// internal representation (CR only) so offsets line up with EM_*SETSEL.
static std::wstring NormalizeForRichEdit(const std::wstring &text)
{
    std::wstring out;
    out.reserve(text.size());
    for (size_t i = 0; i < text.size(); ++i)
    {
        if (text[i] == L'\r' && i + 1 < text.size() && text[i + 1] == L'\n')
        {
            out += L'\r';
            ++i;
        }
        else
        {
            out += text[i];
        }
    }
    return out;
}

// Spell errors are kept in this module-static vector and rendered by
// the editor's WM_PAINT as a GDI overlay (DrawSpellErrors in editor.cpp).
// Nothing about spell state is written into RichEdit's character format,
// so the document layout / scroll cache never knows it exists — that's
// the entire point of this design. The earlier "apply CFE_UNDERLINEWAVE
// to every error via EM_EXSETSEL + EM_SETCHARFORMAT" path mutated
// hundreds of ranges on a Lorem-ipsum-sized buffer, which corrupted
// RichEdit's internal scroll/layout cache and made the editor jump to
// the bottom (and paint blank until scrolled) after a cover/uncover.
static std::vector<SpellError> g_spellErrors;

const std::vector<SpellError> &GetSpellErrors()
{
    return g_spellErrors;
}

void ClearSpellingMarks()
{
    if (g_spellErrors.empty())
        return;
    g_spellErrors.clear();
    if (g_hwndEditor)
        InvalidateRect(g_hwndEditor, nullptr, FALSE);
}

void ApplySpellingMarks()
{
    if (!g_state.spellCheckEnabled || !g_checker || !g_hwndEditor)
    {
        ClearSpellingMarks();
        return;
    }

    // Don't re-run mid-drag — the selection-snapshot logic in the old
    // path needed this, but even with overlay rendering it's still
    // wasted work while the user is actively selecting.
    if (GetKeyState(VK_LBUTTON) & 0x8000)
        return;

    std::wstring text = NormalizeForRichEdit(GetEditorText());
    g_spellErrors = CheckSpelling(text);
    InvalidateRect(g_hwndEditor, nullptr, FALSE);
}

void ScheduleSpellCheck()
{
    if (!g_state.spellCheckEnabled || !g_checker || !g_hwndMain)
        return;
    SetTimer(g_hwndMain, SPELL_CHECK_TIMER_ID, SPELL_CHECK_DEBOUNCE_MS, nullptr);
}
