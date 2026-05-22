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

void ClearSpellingMarks()
{
    if (!g_hwndEditor)
        return;
    int len = static_cast<int>(SendMessageW(g_hwndEditor, WM_GETTEXTLENGTH, 0, 0));
    if (len <= 0)
        return;

    CHARRANGE oldRange;
    SendMessageW(g_hwndEditor, EM_EXGETSEL, 0, reinterpret_cast<LPARAM>(&oldRange));
    LRESULT oldMask = SendMessageW(g_hwndEditor, EM_SETEVENTMASK, 0, 0);
    SendMessageW(g_hwndEditor, WM_SETREDRAW, FALSE, 0);

    CHARRANGE all = {0, len};
    SendMessageW(g_hwndEditor, EM_EXSETSEL, 0, reinterpret_cast<LPARAM>(&all));
    CHARFORMAT2W cf = {};
    cf.cbSize = sizeof(cf);
    cf.dwMask = CFM_UNDERLINE | CFM_UNDERLINETYPE;
    cf.dwEffects = 0;
    cf.bUnderlineType = CFU_UNDERLINENONE;
    SendMessageW(g_hwndEditor, EM_SETCHARFORMAT, SCF_SELECTION, reinterpret_cast<LPARAM>(&cf));

    SendMessageW(g_hwndEditor, EM_EXSETSEL, 0, reinterpret_cast<LPARAM>(&oldRange));
    SendMessageW(g_hwndEditor, WM_SETREDRAW, TRUE, 0);
    SendMessageW(g_hwndEditor, EM_SETEVENTMASK, 0, oldMask);
    InvalidateRect(g_hwndEditor, nullptr, TRUE);
}

void ApplySpellingMarks()
{
    static bool inProgress = false;
    if (inProgress)
        return;
    if (!g_state.spellCheckEnabled || !g_checker || !g_hwndEditor)
        return;

    // Don't run while the user is selecting/dragging — EM_EXSETSEL inside
    // would clobber the in-progress selection.
    if (GetKeyState(VK_LBUTTON) & 0x8000)
        return;

    inProgress = true;

    std::wstring text = NormalizeForRichEdit(GetEditorText());
    int textLen = static_cast<int>(SendMessageW(g_hwndEditor, WM_GETTEXTLENGTH, 0, 0));

    CHARRANGE oldRange;
    SendMessageW(g_hwndEditor, EM_EXGETSEL, 0, reinterpret_cast<LPARAM>(&oldRange));

    // EM_EXSETSEL on each error range nudges the viewport even with
    // WM_SETREDRAW disabled — for long documents this manifests as the
    // view sliding upward as the loop walks errors from top to bottom.
    // Snapshot the scroll position so we can restore it at the end.
    POINT scrollPos = {};
    SendMessageW(g_hwndEditor, EM_GETSCROLLPOS, 0, reinterpret_cast<LPARAM>(&scrollPos));

    // Suppress EN_SELCHANGE / EN_CHANGE notifications during the bulk
    // format updates. Otherwise every EM_EXSETSEL call below fires an
    // EN_SELCHANGE, each of which triggers UpdateStatus() in main.cpp —
    // for a document with many spelling errors that floods the UI thread
    // and makes the status bar fluctuate, selection misbehave, and the
    // editor feel locked up.
    LRESULT oldMask = SendMessageW(g_hwndEditor, EM_SETEVENTMASK, 0, 0);
    SendMessageW(g_hwndEditor, WM_SETREDRAW, FALSE, 0);

    // Clear previous wave underlines across the whole buffer.
    CHARRANGE all = {0, textLen};
    SendMessageW(g_hwndEditor, EM_EXSETSEL, 0, reinterpret_cast<LPARAM>(&all));
    CHARFORMAT2W clear = {};
    clear.cbSize = sizeof(clear);
    clear.dwMask = CFM_UNDERLINE | CFM_UNDERLINETYPE;
    clear.dwEffects = 0;
    clear.bUnderlineType = CFU_UNDERLINENONE;
    SendMessageW(g_hwndEditor, EM_SETCHARFORMAT, SCF_SELECTION, reinterpret_cast<LPARAM>(&clear));

    auto errors = CheckSpelling(text);
    for (const auto &e : errors)
    {
        CHARRANGE range = {e.start, e.start + e.length};
        SendMessageW(g_hwndEditor, EM_EXSETSEL, 0, reinterpret_cast<LPARAM>(&range));
        CHARFORMAT2W cf = {};
        cf.cbSize = sizeof(cf);
        cf.dwMask = CFM_UNDERLINE | CFM_UNDERLINETYPE;
        cf.dwEffects = CFE_UNDERLINE;
        cf.bUnderlineType = CFU_UNDERLINEWAVE;
        cf.bUnderlineColor = 0x06;
        SendMessageW(g_hwndEditor, EM_SETCHARFORMAT, SCF_SELECTION, reinterpret_cast<LPARAM>(&cf));
    }

    SendMessageW(g_hwndEditor, EM_EXSETSEL, 0, reinterpret_cast<LPARAM>(&oldRange));
    SendMessageW(g_hwndEditor, EM_SETSCROLLPOS, 0, reinterpret_cast<LPARAM>(&scrollPos));
    SendMessageW(g_hwndEditor, WM_SETREDRAW, TRUE, 0);
    SendMessageW(g_hwndEditor, EM_SETEVENTMASK, 0, oldMask);
    InvalidateRect(g_hwndEditor, nullptr, TRUE);

    inProgress = false;
}

void ScheduleSpellCheck()
{
    if (!g_state.spellCheckEnabled || !g_checker || !g_hwndMain)
        return;
    SetTimer(g_hwndMain, SPELL_CHECK_TIMER_ID, SPELL_CHECK_DEBOUNCE_MS, nullptr);
}
