/*
  Paste normalization for rich-text sources.

  Earlier we parsed CF_RTF to extract structure AND character data — a
  fully general RTF reader. That broke on Polish characters because
  different Word builds emit codepage-dependent \'XX escapes mixed with
  \u<num> escapes, and the parser's codepage tracking didn't always
  match what Word intended. CF_UNICODETEXT is UTF-16 from Windows itself
  and never has those issues, so we let it provide the characters and
  treat CF_RTF only as a "this came from rich-text" indicator (set by
  the caller).

  What we do here:
    - Split the text into lines.
    - Detect list items by their leading marker (bullet glyph, number).
    - Strip the marker, mark the line as a list item, prepend "- ".
    - Rejoin: list↔list stays tight, anything else gets a blank line.

  Soft breaks (Word's Shift+Enter) come in as VT (U+000B); we normalize
  them to plain line breaks so the splitter treats them like paragraphs.
*/

#include "rtfpaste.h"
#include <vector>
#include <cwctype>

namespace {

bool IsBulletGlyph(wchar_t c)
{
    switch (c)
    {
    case L'•': // BULLET •
    case L'·': // MIDDLE DOT ·
    case L'◦': // WHITE BULLET ◦
    case L'●': // BLACK CIRCLE ●
    case L'○': // WHITE CIRCLE ○
    case L'■': // BLACK SQUARE ■
    case L'▪': // BLACK SMALL SQUARE ▪
    case L'‣': // TRIANGULAR BULLET ‣
    case L'⁃': // HYPHEN BULLET ⁃
    case L'▶': // BLACK RIGHT-POINTING TRIANGLE ▶
    case L'▸': // BLACK RIGHT-POINTING SMALL TRIANGLE ▸
    case L'*':
    case L'o':
    case L'O':
        return true;
    default:
        return false;
    }
}

struct LineRec
{
    std::wstring text;
    bool isList;
};

// Detect a list marker at the start of `text` and return the byte index
// where actual content begins. Sets isList = true on success.
size_t SkipListMarker(const std::wstring &text, bool &isList)
{
    isList = false;
    size_t i = 0;
    while (i < text.size() && (text[i] == L' ' || text[i] == L'\t'))
        ++i;
    if (i >= text.size())
        return 0;

    wchar_t c = text[i];

    // Glyph bullet: <bullet> + WS + content
    if (IsBulletGlyph(c))
    {
        if (i + 1 < text.size() && (text[i + 1] == L' ' || text[i + 1] == L'\t'))
        {
            size_t j = i + 2;
            while (j < text.size() && (text[j] == L' ' || text[j] == L'\t'))
                ++j;
            isList = true;
            return j;
        }
        return 0;
    }

    // Numbered: digits + ('.' | ')') + WS + content
    if (iswdigit(c))
    {
        size_t j = i;
        while (j < text.size() && iswdigit(text[j]))
            ++j;
        if (j < text.size() && (text[j] == L'.' || text[j] == L')'))
        {
            ++j;
            if (j < text.size() && (text[j] == L' ' || text[j] == L'\t'))
            {
                ++j;
                while (j < text.size() && (text[j] == L' ' || text[j] == L'\t'))
                    ++j;
                isList = true;
                return j;
            }
        }
        return 0;
    }

    // Lettered: letter + ('.' | ')') + WS + content
    if (iswalpha(c) && (i + 1 < text.size()) &&
        (text[i + 1] == L'.' || text[i + 1] == L')'))
    {
        size_t j = i + 2;
        if (j < text.size() && (text[j] == L' ' || text[j] == L'\t'))
        {
            ++j;
            while (j < text.size() && (text[j] == L' ' || text[j] == L'\t'))
                ++j;
            isList = true;
            return j;
        }
    }

    return 0;
}

} // namespace

std::wstring NormalizeRichPaste(const std::wstring &textIn)
{
    if (textIn.empty())
        return textIn;

    // Split into lines on \r\n, \r, \n, or VT (Word's soft break).
    std::vector<std::wstring> lines;
    std::wstring cur;
    cur.reserve(64);
    for (size_t i = 0; i < textIn.size(); ++i)
    {
        wchar_t ch = textIn[i];
        if (ch == L'\r')
        {
            lines.push_back(cur);
            cur.clear();
            if (i + 1 < textIn.size() && textIn[i + 1] == L'\n')
                ++i;
        }
        else if (ch == L'\n' || ch == L'\v')
        {
            lines.push_back(cur);
            cur.clear();
        }
        else
        {
            cur += ch;
        }
    }
    if (!cur.empty())
        lines.push_back(cur);

    // Classify lines, strip list markers.
    std::vector<LineRec> recs;
    recs.reserve(lines.size());
    for (auto &line : lines)
    {
        LineRec r;
        r.isList = false;
        size_t skip = SkipListMarker(line, r.isList);
        r.text = r.isList ? line.substr(skip) : line;
        recs.push_back(std::move(r));
    }

    // Assemble: list↔list single break, everything else doubled.
    std::wstring out;
    out.reserve(textIn.size() + 64);
    for (size_t i = 0; i < recs.size(); ++i)
    {
        if (recs[i].isList)
            out += L"- ";
        out += recs[i].text;
        if (i + 1 < recs.size())
        {
            bool a = recs[i].isList;
            bool b = recs[i + 1].isList;
            bool ae = recs[i].text.empty();
            bool be = recs[i + 1].text.empty();
            if ((a && b) || ae || be)
                out += L"\r\n";
            else
                out += L"\r\n\r\n";
        }
    }
    return out;
}
