/*
  Text tools: normalization, Base64, SHA1, MD5.
  Hashing uses BCrypt (bcrypt.dll). Base64 uses crypt32 (CryptBinaryToStringW).
  All operate in-place on the current selection; with no selection, on the
  whole document.
*/

#include "tools.h"
#include "core/globals.h"
#include "editor.h"
#include "lang/lang.h"
#include "resource.h"
#include <windows.h>
#include <richedit.h>
#include <wincrypt.h>
#include <bcrypt.h>
#include <string>
#include <vector>
#include <algorithm>

#ifndef BCRYPT_SHA1_ALGORITHM
#define BCRYPT_SHA1_ALGORITHM L"SHA1"
#endif
#ifndef BCRYPT_MD5_ALGORITHM
#define BCRYPT_MD5_ALGORITHM L"MD5"
#endif
#ifndef BCRYPT_HASH_LENGTH
#define BCRYPT_HASH_LENGTH L"HashDigestLength"
#endif

// ---------- Helpers --------------------------------------------------------

static std::wstring GetTargetText(bool *wasSelection)
{
    DWORD start = 0, end = 0;
    SendMessageW(g_hwndEditor, EM_GETSEL,
                 reinterpret_cast<WPARAM>(&start),
                 reinterpret_cast<LPARAM>(&end));
    if (start != end)
    {
        if (wasSelection)
            *wasSelection = true;
        std::wstring buf(end - start + 1, L'\0');
        SendMessageW(g_hwndEditor, EM_GETSELTEXT, 0, reinterpret_cast<LPARAM>(buf.data()));
        buf.resize(wcslen(buf.c_str()));
        return buf;
    }
    if (wasSelection)
        *wasSelection = false;
    int len = static_cast<int>(SendMessageW(g_hwndEditor, WM_GETTEXTLENGTH, 0, 0));
    if (len <= 0)
        return {};
    std::wstring buf(len + 1, L'\0');
    GetWindowTextW(g_hwndEditor, buf.data(), len + 1);
    buf.resize(wcslen(buf.c_str()));
    return buf;
}

static void ReplaceTargetText(const std::wstring &text, bool wasSelection)
{
    if (!wasSelection)
        SendMessageW(g_hwndEditor, EM_SETSEL, 0, -1);
    SendMessageW(g_hwndEditor, EM_REPLACESEL, TRUE, reinterpret_cast<LPARAM>(text.c_str()));
}

static std::vector<BYTE> WideToUtf8Bytes(const std::wstring &text)
{
    if (text.empty())
        return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()),
                                nullptr, 0, nullptr, nullptr);
    if (n <= 0)
        return {};
    std::vector<BYTE> out(static_cast<size_t>(n));
    WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()),
                        reinterpret_cast<LPSTR>(out.data()), n, nullptr, nullptr);
    return out;
}

// ---------- Normalization --------------------------------------------------

static const wchar_t *Normalize(wchar_t ch)
{
    switch (ch)
    {
    // Dashes & hyphens: U+2010..U+2015, U+2212
    case L'‐': case L'‑': case L'‒': case L'–':
    case L'—': case L'―': case L'−':
        return L"-";
    // Ellipsis
    case L'…':
        return L"...";
    // Double quotes: curly + low-9 + reversed
    case L'“': case L'”': case L'„': case L'‟':
        return L"\"";
    // Guillemets → double angle brackets
    case L'«':
        return L"<<";
    case L'»':
        return L">>";
    // Arrows (Word autocorrect: -->, <--, ==>)
    case L'→':
        return L"->";
    case L'←':
        return L"<-";
    case L'⇒':
        return L"=>";
    // Single quotes / apostrophes
    case L'‘': case L'’': case L'‚': case L'‛':
    case L'‹': case L'›': case L'´': case L'`':
        return L"'";
    // Whitespace variants → regular space
    case L' ': case L' ':
    case L' ': case L' ': case L' ': case L' ':
    case L' ': case L' ': case L' ': case L' ':
    case L' ': case L' ': case L' ':
    case L' ': case L' ': case L'　':
        return L" ";
    // Zero-width / invisible format characters — drop entirely.
    // These survive copy-paste from web/Word and break text pipelines
    // (search, word counts, space collapse) because they occupy zero
    // visual width but still count as characters.
    case 0x00AD: // SOFT HYPHEN
    case 0x034F: // COMBINING GRAPHEME JOINER
    case 0x061C: // ARABIC LETTER MARK
    case 0x180E: // MONGOLIAN VOWEL SEPARATOR
    case 0x200B: // ZERO WIDTH SPACE
    case 0x200C: // ZERO WIDTH NON-JOINER
    case 0x200D: // ZERO WIDTH JOINER
    case 0x200E: // LEFT-TO-RIGHT MARK
    case 0x200F: // RIGHT-TO-LEFT MARK
    case 0x202A: // LEFT-TO-RIGHT EMBEDDING
    case 0x202B: // RIGHT-TO-LEFT EMBEDDING
    case 0x202C: // POP DIRECTIONAL FORMATTING
    case 0x202D: // LEFT-TO-RIGHT OVERRIDE
    case 0x202E: // RIGHT-TO-LEFT OVERRIDE
    case 0x2060: // WORD JOINER
    case 0x2061: case 0x2062: case 0x2063: case 0x2064: // invisible math operators
    case 0x2066: case 0x2067: case 0x2068: case 0x2069: // isolate controls
    case 0xFEFF: // ZERO WIDTH NO-BREAK SPACE / BOM
        return L"";
    // Bullets / middle dots
    case L'•': case L'·': case L'‧': case L'∙':
        return L"*";
    // Multiplication sign
    case L'×':
        return L"x";
    default:
        return nullptr;
    }
}

void ToolsNormalizeText()
{
    bool wasSelection = false;
    std::wstring text = GetTargetText(&wasSelection);
    if (text.empty())
        return;

    // Pass 1: per-char substitution (dashes, quotes, ellipsis, etc.).
    std::wstring step1;
    step1.reserve(text.size());
    for (wchar_t ch : text)
    {
        const wchar_t *replacement = Normalize(ch);
        if (replacement)
            step1.append(replacement);
        else
            step1.push_back(ch);
    }

    // Pass 2: collapse runs of ASCII space to a single space.
    // Tabs are left alone (intentional indentation).
    std::wstring step2;
    step2.reserve(step1.size());
    bool inSpaceRun = false;
    for (wchar_t c : step1)
    {
        if (c == L' ')
        {
            if (!inSpaceRun)
                step2.push_back(c);
            inSpaceRun = true;
        }
        else
        {
            step2.push_back(c);
            inSpaceRun = false;
        }
    }

    // Pass 3: cap consecutive line breaks at 2 (i.e. at most one blank
    // line between content). A "line break" is a CRLF pair, or a lone
    // CR / LF — counted once each.
    std::wstring step3;
    step3.reserve(step2.size());
    int newlineCount = 0;
    for (size_t i = 0; i < step2.size();)
    {
        wchar_t c = step2[i];
        int nlLen = 0;
        if (c == L'\r')
            nlLen = (i + 1 < step2.size() && step2[i + 1] == L'\n') ? 2 : 1;
        else if (c == L'\n')
            nlLen = 1;
        if (nlLen > 0)
        {
            ++newlineCount;
            if (newlineCount <= 2)
                step3.append(step2, i, nlLen);
            i += nlLen;
        }
        else
        {
            newlineCount = 0;
            step3.push_back(c);
            ++i;
        }
    }

    ReplaceTargetText(step3, wasSelection);
}

// ---------- Base64 ---------------------------------------------------------

void ToolsBase64()
{
    bool wasSelection = false;
    std::wstring text = GetTargetText(&wasSelection);
    if (text.empty())
        return;

    auto bytes = WideToUtf8Bytes(text);
    if (bytes.empty())
        return;

    DWORD outLen = 0;
    if (!CryptBinaryToStringW(bytes.data(), static_cast<DWORD>(bytes.size()),
                              CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF,
                              nullptr, &outLen))
        return;

    std::wstring out(outLen, L'\0');
    if (!CryptBinaryToStringW(bytes.data(), static_cast<DWORD>(bytes.size()),
                              CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF,
                              out.data(), &outLen))
        return;
    out.resize(wcslen(out.c_str()));
    ReplaceTargetText(out, wasSelection);
}

// ---------- Hashing via BCrypt --------------------------------------------

static std::wstring HashHex(LPCWSTR algId, const std::vector<BYTE> &data)
{
    BCRYPT_ALG_HANDLE hAlg = nullptr;
    if (BCryptOpenAlgorithmProvider(&hAlg, algId, nullptr, 0) != 0)
        return {};

    DWORD hashLen = 0, cb = 0;
    if (BCryptGetProperty(hAlg, BCRYPT_HASH_LENGTH,
                          reinterpret_cast<PBYTE>(&hashLen), sizeof(hashLen),
                          &cb, 0) != 0)
    {
        BCryptCloseAlgorithmProvider(hAlg, 0);
        return {};
    }

    BCRYPT_HASH_HANDLE hHash = nullptr;
    if (BCryptCreateHash(hAlg, &hHash, nullptr, 0, nullptr, 0, 0) != 0)
    {
        BCryptCloseAlgorithmProvider(hAlg, 0);
        return {};
    }

    BCryptHashData(hHash, const_cast<PBYTE>(data.data()),
                   static_cast<ULONG>(data.size()), 0);

    std::vector<BYTE> digest(hashLen);
    BCryptFinishHash(hHash, digest.data(), hashLen, 0);

    BCryptDestroyHash(hHash);
    BCryptCloseAlgorithmProvider(hAlg, 0);

    std::wstring hex;
    hex.reserve(static_cast<size_t>(hashLen) * 2);
    for (BYTE b : digest)
    {
        wchar_t buf[3];
        swprintf(buf, 3, L"%02x", b);
        hex += buf;
    }
    return hex;
}

void ToolsSha1()
{
    bool wasSelection = false;
    std::wstring text = GetTargetText(&wasSelection);
    if (text.empty())
        return;
    auto bytes = WideToUtf8Bytes(text);
    std::wstring hash = HashHex(BCRYPT_SHA1_ALGORITHM, bytes);
    if (!hash.empty())
        ReplaceTargetText(hash, wasSelection);
}

void ToolsMd5()
{
    bool wasSelection = false;
    std::wstring text = GetTargetText(&wasSelection);
    if (text.empty())
        return;
    auto bytes = WideToUtf8Bytes(text);
    std::wstring hash = HashHex(BCRYPT_MD5_ALGORITHM, bytes);
    if (!hash.empty())
        ReplaceTargetText(hash, wasSelection);
}

// ---------- Text transforms ------------------------------------------------
//
// All of these route their result through ReplaceTargetText, which feeds
// EM_REPLACESEL. RichEdit treats a lone CR as a paragraph break, so a
// stray "\r\n" pair would produce a blank line — every transform below
// emits CR-only line endings (NormalizeCR / JoinWithCR) to avoid that.

static const int kTabWidth = 4;

// Replace CRLF and lone LF with a single CR.
static std::wstring NormalizeCR(const std::wstring &text)
{
    std::wstring out;
    out.reserve(text.size());
    for (size_t i = 0; i < text.size(); ++i)
    {
        wchar_t c = text[i];
        if (c == L'\r')
        {
            out.push_back(L'\r');
            if (i + 1 < text.size() && text[i + 1] == L'\n')
                ++i;
        }
        else if (c == L'\n')
            out.push_back(L'\r');
        else
            out.push_back(c);
    }
    return out;
}

// Split on CRLF / CR / LF. Always yields at least one element.
static std::vector<std::wstring> SplitLines(const std::wstring &text)
{
    std::vector<std::wstring> lines;
    std::wstring cur;
    for (size_t i = 0; i < text.size(); ++i)
    {
        wchar_t c = text[i];
        if (c == L'\r')
        {
            lines.push_back(cur);
            cur.clear();
            if (i + 1 < text.size() && text[i + 1] == L'\n')
                ++i;
        }
        else if (c == L'\n')
        {
            lines.push_back(cur);
            cur.clear();
        }
        else
            cur.push_back(c);
    }
    lines.push_back(cur);
    return lines;
}

static std::wstring JoinWithCR(const std::vector<std::wstring> &lines)
{
    std::wstring out;
    for (size_t i = 0; i < lines.size(); ++i)
    {
        if (i)
            out.push_back(L'\r');
        out += lines[i];
    }
    return out;
}

void ToolsUppercase()
{
    bool wasSelection = false;
    std::wstring text = NormalizeCR(GetTargetText(&wasSelection));
    if (text.empty())
        return;
    CharUpperBuffW(text.data(), static_cast<DWORD>(text.size()));
    ReplaceTargetText(text, wasSelection);
}

void ToolsLowercase()
{
    bool wasSelection = false;
    std::wstring text = NormalizeCR(GetTargetText(&wasSelection));
    if (text.empty())
        return;
    CharLowerBuffW(text.data(), static_cast<DWORD>(text.size()));
    ReplaceTargetText(text, wasSelection);
}

void ToolsTitleCase()
{
    bool wasSelection = false;
    std::wstring text = NormalizeCR(GetTargetText(&wasSelection));
    if (text.empty())
        return;
    bool atWordStart = true;
    for (wchar_t &ch : text)
    {
        if (IsCharAlphaW(ch))
        {
            wchar_t c = ch;
            if (atWordStart)
                CharUpperBuffW(&c, 1);
            else
                CharLowerBuffW(&c, 1);
            ch = c;
            atWordStart = false;
        }
        else if (ch == L'\'' || ch == L'’')
        {
            // Apostrophe inside a word ("don't") — not a word boundary.
        }
        else
            atWordStart = true;
    }
    ReplaceTargetText(text, wasSelection);
}

void ToolsTrimTrailing()
{
    bool wasSelection = false;
    std::wstring text = GetTargetText(&wasSelection);
    if (text.empty())
        return;
    auto lines = SplitLines(text);
    for (auto &ln : lines)
    {
        size_t end = ln.find_last_not_of(L" \t");
        if (end == std::wstring::npos)
            ln.clear();
        else
            ln.erase(end + 1);
    }
    ReplaceTargetText(JoinWithCR(lines), wasSelection);
}

void ToolsTabsToSpaces()
{
    bool wasSelection = false;
    std::wstring text = GetTargetText(&wasSelection);
    if (text.empty())
        return;
    auto lines = SplitLines(text);
    for (auto &ln : lines)
    {
        std::wstring out;
        out.reserve(ln.size());
        int col = 0;
        for (wchar_t c : ln)
        {
            if (c == L'\t')
            {
                int n = kTabWidth - (col % kTabWidth);
                out.append(static_cast<size_t>(n), L' ');
                col += n;
            }
            else
            {
                out.push_back(c);
                ++col;
            }
        }
        ln = out;
    }
    ReplaceTargetText(JoinWithCR(lines), wasSelection);
}

void ToolsSpacesToTabs()
{
    bool wasSelection = false;
    std::wstring text = GetTargetText(&wasSelection);
    if (text.empty())
        return;
    auto lines = SplitLines(text);
    for (auto &ln : lines)
    {
        size_t sp = 0;
        while (sp < ln.size() && ln[sp] == L' ')
            ++sp;
        if (sp >= static_cast<size_t>(kTabWidth))
        {
            std::wstring lead(sp / kTabWidth, L'\t');
            lead.append(sp % kTabWidth, L' ');
            ln = lead + ln.substr(sp);
        }
    }
    ReplaceTargetText(JoinWithCR(lines), wasSelection);
}

void ToolsReverseLines()
{
    bool wasSelection = false;
    std::wstring text = GetTargetText(&wasSelection);
    if (text.empty())
        return;
    auto lines = SplitLines(text);
    std::reverse(lines.begin(), lines.end());
    ReplaceTargetText(JoinWithCR(lines), wasSelection);
}

void ToolsJoinLines()
{
    bool wasSelection = false;
    std::wstring text = GetTargetText(&wasSelection);
    if (text.empty())
        return;
    auto lines = SplitLines(text);
    if (lines.size() < 2)
        return; // nothing to join
    std::wstring out;
    for (auto &ln : lines)
    {
        size_t first = ln.find_first_not_of(L" \t");
        if (first == std::wstring::npos)
            continue; // skip blank lines
        size_t last = ln.find_last_not_of(L" \t");
        if (!out.empty())
            out.push_back(L' ');
        out.append(ln, first, last - first + 1);
    }
    ReplaceTargetText(out, wasSelection);
}

// ---------- Top-level "Tools" menu visibility -----------------------------

static HMENU g_hToolsMenuDetached = nullptr;

static int FindTopLevelByFirstItemId(HMENU hMenu, UINT firstItemId)
{
    int count = GetMenuItemCount(hMenu);
    for (int i = 0; i < count; i++)
    {
        HMENU sub = GetSubMenu(hMenu, i);
        if (!sub)
            continue;
        if (GetMenuItemID(sub, 0) == firstItemId)
            return i;
    }
    return -1;
}

void UpdateToolsMenuVisibility()
{
    HMENU hMenu = GetMenu(g_hwndMain);
    if (!hMenu)
        return;

    int toolsIdx = FindTopLevelByFirstItemId(hMenu, IDM_TOOLS_NORMALIZE);

    if (g_state.toolsEnabled)
    {
        if (toolsIdx != -1)
            return;
        if (!g_hToolsMenuDetached)
            return;
        // Anchor before Help. (Originally we anchored before Language, but
        // Language was moved into View, so it's no longer top-level.)
        int helpIdx = FindTopLevelByFirstItemId(hMenu, IDM_HELP_ABOUT);
        if (helpIdx == -1)
            helpIdx = GetMenuItemCount(hMenu);
        const auto &lang = GetLangStrings();
        InsertMenuW(hMenu, helpIdx, MF_BYPOSITION | MF_POPUP,
                    reinterpret_cast<UINT_PTR>(g_hToolsMenuDetached),
                    lang.menuTools.c_str());
        g_hToolsMenuDetached = nullptr;
    }
    else
    {
        if (toolsIdx == -1)
            return;
        g_hToolsMenuDetached = GetSubMenu(hMenu, toolsIdx);
        RemoveMenu(hMenu, toolsIdx, MF_BYPOSITION);
    }

    DrawMenuBar(g_hwndMain);
}
