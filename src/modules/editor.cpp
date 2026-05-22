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

  Editor control functions for text manipulation, font rendering, and zoom control.
  Handles RichEdit control subclassing, word wrap, and cursor position tracking.
*/

#include "editor.h"
#include "core/types.h"
#include "core/globals.h"
#include "theme.h"
#include "ui.h"
#include "gutter.h"
#include "rtfpaste.h"
#include "resource.h"
#include "lang/lang.h"
#include <richedit.h>
#include <richole.h>
#include <tom.h>
#include <windowsx.h>
#include <algorithm>
#include <vector>

// tom.h declares IID_ITextDocument but its symbol lives in uuid.lib /
// libuuid.a — not pulled in by this static build. Inline the GUID by
// value so QueryInterface works without an extra link dependency.
static const IID kIID_ITextDocument =
    {0x8CC497C0, 0xA1DF, 0x11CE, {0x80, 0x98, 0x00, 0xAA, 0x00, 0x47, 0xBE, 0x5D}};

struct StreamCookie
{
    const std::wstring *text;
    size_t pos;
};

static DWORD CALLBACK StreamInCallback(DWORD_PTR dwCookie, LPBYTE pbBuff, LONG cb, LONG *pcb)
{
    StreamCookie *pCookie = reinterpret_cast<StreamCookie *>(dwCookie);
    size_t remaining = (pCookie->text->length() * sizeof(wchar_t)) - pCookie->pos;
    if (remaining <= 0)
    {
        *pcb = 0;
        return 0;
    }
    size_t toCopy = (static_cast<size_t>(cb) < remaining) ? static_cast<size_t>(cb) : remaining;
    memcpy(pbBuff, reinterpret_cast<const BYTE *>(pCookie->text->c_str()) + pCookie->pos, toCopy);
    pCookie->pos += toCopy;
    *pcb = static_cast<LONG>(toCopy);
    return 0;
}

struct StreamOutCookie
{
    std::wstring *text;
};

static DWORD CALLBACK StreamOutCallback(DWORD_PTR dwCookie, LPBYTE pbBuff, LONG cb, LONG *pcb)
{
    StreamOutCookie *pCookie = reinterpret_cast<StreamOutCookie *>(dwCookie);
    pCookie->text->append(reinterpret_cast<const wchar_t *>(pbBuff), cb / sizeof(wchar_t));
    *pcb = cb;
    return 0;
}

std::wstring GetEditorText()
{
    std::wstring text;
    StreamOutCookie cookie = {&text};
    EDITSTREAM es = {reinterpret_cast<DWORD_PTR>(&cookie), 0, StreamOutCallback};
    SendMessageW(g_hwndEditor, EM_STREAMOUT, SF_TEXT | SF_UNICODE, reinterpret_cast<LPARAM>(&es));
    return text;
}

void SetEditorText(const std::wstring &text)
{
    StreamCookie cookie = {&text, 0};
    EDITSTREAM es = {reinterpret_cast<DWORD_PTR>(&cookie), 0, StreamInCallback};
    SendMessageW(g_hwndEditor, EM_STREAMIN, SF_TEXT | SF_UNICODE, reinterpret_cast<LPARAM>(&es));
}

std::pair<int, int> GetCursorPos()
{
    DWORD start = 0, end = 0;
    SendMessageW(g_hwndEditor, EM_GETSEL, reinterpret_cast<WPARAM>(&start), reinterpret_cast<LPARAM>(&end));
    int line = static_cast<int>(SendMessageW(g_hwndEditor, EM_EXLINEFROMCHAR, 0, start));
    int lineIndex = static_cast<int>(SendMessageW(g_hwndEditor, EM_LINEINDEX, static_cast<WPARAM>(line), 0));
    int col = static_cast<int>(start) - lineIndex;
    return {line + 1, col + 1};
}

void ApplyFont()
{
    // Snapshot modified — zoom / font dialog / theme toggle aren't user
    // edits, but RichEdit fires EN_CHANGE on both WM_SETFONT and on
    // SCF_ALL EM_SETCHARFORMAT, which would set the flag and add a "*"
    // to the title bar. ApplyTheme() only suppresses around the latter;
    // WM_SETFONT slips through. Belt-and-braces: suppress the event mask
    // around the whole editor-side block AND restore the flag at the
    // end, in case anything else cascades into an EN_CHANGE.
    bool wasModified = g_state.modified;
    if (g_state.hFont)
    {
        DeleteObject(g_state.hFont);
        g_state.hFont = nullptr;
    }
    int size = g_state.fontSize * g_state.zoomLevel / 100;
    size = (size < 8) ? 8 : (size > 500) ? 500
                                         : size;
    HDC hdc = GetDC(g_hwndMain);
    int height = -MulDiv(size, GetDeviceCaps(hdc, LOGPIXELSY), 72);
    ReleaseDC(g_hwndMain, hdc);
    g_state.hFont = CreateFontW(height, 0, 0, 0, g_state.fontWeight, g_state.fontItalic, g_state.fontUnderline, FALSE,
                                DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                                DEFAULT_PITCH | FF_DONTCARE, g_state.fontName.c_str());
    LRESULT oldMask = SendMessageW(g_hwndEditor, EM_SETEVENTMASK, 0, 0);
    SendMessageW(g_hwndEditor, WM_SETFONT, reinterpret_cast<WPARAM>(g_state.hFont), TRUE);
    COLORREF textColor = GetEditorTextColor();
    CHARFORMAT2W cf = {};
    cf.cbSize = sizeof(cf);
    cf.dwMask = CFM_COLOR;
    cf.crTextColor = textColor;
    SendMessageW(g_hwndEditor, EM_SETCHARFORMAT, SCF_ALL, reinterpret_cast<LPARAM>(&cf));
    SendMessageW(g_hwndEditor, EM_SETCHARFORMAT, SCF_DEFAULT, reinterpret_cast<LPARAM>(&cf));
    SendMessageW(g_hwndEditor, EM_SETEVENTMASK, 0, oldMask);
    if (g_state.showLineNumbers && g_hwndGutter && UpdateGutterWidth())
        ResizeControls();
    if (g_state.modified != wasModified)
    {
        g_state.modified = wasModified;
        UpdateTitle();
    }
}

void ApplyZoom()
{
    ApplyFont();
}

void ApplyWordWrap()
{
    // Toggling word-wrap rebuilds the editor and replays the existing text
    // into it via EM_STREAMIN, which fires a real EN_CHANGE — RichEdit
    // genuinely sees text being inserted. From the user's perspective
    // that's not an edit, so snapshot the modified flag and restore it
    // afterwards. UpdateTitle resyncs the title bar in case an EN_CHANGE
    // already drew a stale "*" indicator.
    bool wasModified = g_state.modified;
    std::wstring text = GetEditorText();
    DWORD start = 0, end = 0;
    SendMessageW(g_hwndEditor, EM_GETSEL, reinterpret_cast<WPARAM>(&start), reinterpret_cast<LPARAM>(&end));
    DestroyWindow(g_hwndEditor);
    DWORD style = WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_MULTILINE | ES_WANTRETURN | ES_NOHIDESEL;
    if (!g_state.wordWrap)
        style |= WS_HSCROLL | ES_AUTOHSCROLL;
    g_hwndEditor = CreateWindowExW(0, MSFTEDIT_CLASS, nullptr, style,
                                   0, 0, 100, 100, g_hwndMain, reinterpret_cast<HMENU>(IDC_EDITOR), GetModuleHandleW(nullptr), nullptr);
    g_origEditorProc = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(g_hwndEditor, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(EditorSubclassProc)));
    SendMessageW(g_hwndEditor, EM_EXLIMITTEXT, 0, static_cast<LPARAM>(-1));
    SendMessageW(g_hwndEditor, EM_SETEVENTMASK, 0, ENM_CHANGE | ENM_SELCHANGE);
    ApplyFont();
    ApplyTheme();
    SetEditorText(text);
    SendMessageW(g_hwndEditor, EM_SETSEL, start, end);
    ResizeControls();
    SetFocus(g_hwndEditor);
    g_state.modified = wasModified;
    UpdateTitle();
}

static void IndentDedentLines(HWND hwnd, bool dedent)
{
    DWORD selStart = 0, selEnd = 0;
    SendMessageW(hwnd, EM_GETSEL, reinterpret_cast<WPARAM>(&selStart), reinterpret_cast<LPARAM>(&selEnd));
    bool hasSelection = selStart != selEnd;

    int firstLine = static_cast<int>(SendMessageW(hwnd, EM_EXLINEFROMCHAR, 0, selStart));
    int lastLine = firstLine;
    if (hasSelection)
    {
        int endLine = static_cast<int>(SendMessageW(hwnd, EM_EXLINEFROMCHAR, 0, selEnd));
        int endLineStart = static_cast<int>(SendMessageW(hwnd, EM_LINEINDEX, endLine, 0));
        // A selection that ends exactly at column 0 of the line after the
        // intended one (the natural result of shift-down or triple-click)
        // shouldn't pull that extra empty line into the indent block.
        if (endLine > firstLine && static_cast<int>(selEnd) == endLineStart)
            lastLine = endLine - 1;
        else
            lastLine = endLine;
    }

    int blockStart = static_cast<int>(SendMessageW(hwnd, EM_LINEINDEX, firstLine, 0));
    int lastLineStart = static_cast<int>(SendMessageW(hwnd, EM_LINEINDEX, lastLine, 0));
    int lastLineLen = static_cast<int>(SendMessageW(hwnd, EM_LINELENGTH, lastLineStart, 0));
    int blockEnd = lastLineStart + lastLineLen;
    int blockLen = blockEnd - blockStart;
    if (blockLen < 0)
        blockLen = 0;

    std::vector<wchar_t> buf(static_cast<size_t>(blockLen) + 1, L'\0');
    if (blockLen > 0)
    {
        TEXTRANGEW tr;
        tr.chrg.cpMin = blockStart;
        tr.chrg.cpMax = blockEnd;
        tr.lpstrText = buf.data();
        SendMessageW(hwnd, EM_GETTEXTRANGE, 0, reinterpret_cast<LPARAM>(&tr));
    }
    std::wstring block(buf.data());

    std::wstring out;
    out.reserve(block.size() + static_cast<size_t>(lastLine - firstLine + 1));

    // Walk each line in the block, applying indent or dedent. RichEdit
    // 2.0+ stores paragraph breaks as a single \r, so split on \r only.
    size_t pos = 0;
    while (true)
    {
        size_t eol = block.find(L'\r', pos);
        size_t lineEnd = (eol == std::wstring::npos) ? block.size() : eol;

        if (dedent)
        {
            // One leading tab, otherwise up to 4 leading spaces. Mirrors
            // Notepad++ / VS Code Shift+Tab semantics.
            size_t skip = 0;
            if (pos < lineEnd && block[pos] == L'\t')
                skip = 1;
            else
                while (skip < 4 && pos + skip < lineEnd && block[pos + skip] == L' ')
                    ++skip;
            out.append(block, pos + skip, lineEnd - pos - skip);
        }
        else
        {
            out.push_back(L'\t');
            out.append(block, pos, lineEnd - pos);
        }

        if (eol == std::wstring::npos)
            break;
        out.push_back(L'\r');
        pos = eol + 1;
    }

    SendMessageW(hwnd, EM_SETSEL, blockStart, blockEnd);
    SendMessageW(hwnd, EM_REPLACESEL, TRUE, reinterpret_cast<LPARAM>(out.c_str()));

    int newBlockEnd = blockStart + static_cast<int>(out.size());
    if (hasSelection)
    {
        // Re-select the modified block so the user can repeat Tab / Shift+Tab.
        SendMessageW(hwnd, EM_SETSEL, blockStart, newBlockEnd);
    }
    else
    {
        // No-selection dedent: slide caret left by however many characters
        // were stripped before it, but never past the line start.
        int removed = blockLen - static_cast<int>(out.size());
        int origCol = static_cast<int>(selStart) - blockStart;
        int newCol = origCol - removed;
        if (newCol < 0)
            newCol = 0;
        int newCaret = blockStart + newCol;
        SendMessageW(hwnd, EM_SETSEL, newCaret, newCaret);
    }
}

// Fetch the text in [cpMin, cpMax). RichEdit 2.0+ uses CR-only paragraph
// breaks, so the returned string contains '\r' (never "\r\n").
static std::wstring GetEditorTextRange(HWND hwnd, int cpMin, int cpMax)
{
    if (cpMax <= cpMin)
        return std::wstring();
    std::vector<wchar_t> buf(static_cast<size_t>(cpMax - cpMin) + 1, L'\0');
    TEXTRANGEW tr;
    tr.chrg.cpMin = cpMin;
    tr.chrg.cpMax = cpMax;
    tr.lpstrText = buf.data();
    SendMessageW(hwnd, EM_GETTEXTRANGE, 0, reinterpret_cast<LPARAM>(&tr));
    return std::wstring(buf.data());
}

// Move the line(s) spanned by the selection up or down by one, swapping
// with the neighbouring line. Bound to Alt+Up / Alt+Down.
static void MoveLines(HWND hwnd, bool down)
{
    DWORD selStart = 0, selEnd = 0;
    SendMessageW(hwnd, EM_GETSEL, reinterpret_cast<WPARAM>(&selStart), reinterpret_cast<LPARAM>(&selEnd));
    bool hasSelection = selStart != selEnd;

    int firstLine = static_cast<int>(SendMessageW(hwnd, EM_EXLINEFROMCHAR, 0, selStart));
    int lastLine = firstLine;
    if (hasSelection)
    {
        int endLine = static_cast<int>(SendMessageW(hwnd, EM_EXLINEFROMCHAR, 0, selEnd));
        int endLineStart = static_cast<int>(SendMessageW(hwnd, EM_LINEINDEX, endLine, 0));
        // A selection ending exactly at column 0 of the following line
        // (shift-down's natural result) shouldn't drag that line along.
        if (endLine > firstLine && static_cast<int>(selEnd) == endLineStart)
            lastLine = endLine - 1;
        else
            lastLine = endLine;
    }

    int totalLines = static_cast<int>(SendMessageW(hwnd, EM_GETLINECOUNT, 0, 0));
    if (down ? (lastLine >= totalLines - 1) : (firstLine <= 0))
        return; // already at the edge

    int blockStart = static_cast<int>(SendMessageW(hwnd, EM_LINEINDEX, firstLine, 0));
    int lastLineStart = static_cast<int>(SendMessageW(hwnd, EM_LINEINDEX, lastLine, 0));
    int lastLineLen = static_cast<int>(SendMessageW(hwnd, EM_LINELENGTH, lastLineStart, 0));
    int blockEnd = lastLineStart + lastLineLen;

    std::wstring blockText = GetEditorTextRange(hwnd, blockStart, blockEnd);
    int blockLen = static_cast<int>(blockText.size());

    int repStart, repEnd, newBlockStart;
    std::wstring combined;
    if (down)
    {
        int nextStart = static_cast<int>(SendMessageW(hwnd, EM_LINEINDEX, lastLine + 1, 0));
        int nextLen = static_cast<int>(SendMessageW(hwnd, EM_LINELENGTH, nextStart, 0));
        int nextEnd = nextStart + nextLen;
        std::wstring nextText = GetEditorTextRange(hwnd, nextStart, nextEnd);
        combined = nextText + L"\r" + blockText;
        repStart = blockStart;
        repEnd = nextEnd;
        newBlockStart = blockStart + static_cast<int>(nextText.size()) + 1;
    }
    else
    {
        int prevStart = static_cast<int>(SendMessageW(hwnd, EM_LINEINDEX, firstLine - 1, 0));
        // The previous line's content ends one char before blockStart (the
        // char at blockStart-1 is its terminating '\r').
        std::wstring prevText = GetEditorTextRange(hwnd, prevStart, blockStart - 1);
        combined = blockText + L"\r" + prevText;
        repStart = prevStart;
        repEnd = blockEnd;
        newBlockStart = prevStart;
    }

    SendMessageW(hwnd, EM_SETSEL, repStart, repEnd);
    SendMessageW(hwnd, EM_REPLACESEL, TRUE, reinterpret_cast<LPARAM>(combined.c_str()));

    if (hasSelection)
    {
        // Keep the block selected so the move can be repeated.
        SendMessageW(hwnd, EM_SETSEL, newBlockStart, newBlockStart + blockLen);
    }
    else
    {
        int caretOff = static_cast<int>(selStart) - blockStart;
        if (caretOff < 0)
            caretOff = 0;
        if (caretOff > blockLen)
            caretOff = blockLen;
        SendMessageW(hwnd, EM_SETSEL, newBlockStart + caretOff, newBlockStart + caretOff);
    }
    SendMessageW(hwnd, EM_SCROLLCARET, 0, 0);
}

// Insert a newline that inherits the leading whitespace of the current
// line (auto-indent). The copied indent is clamped to the caret column,
// so pressing Enter at column 0 still produces a plain newline.
static void InsertNewlineWithIndent(HWND hwnd)
{
    DWORD selStart = 0, selEnd = 0;
    SendMessageW(hwnd, EM_GETSEL, reinterpret_cast<WPARAM>(&selStart), reinterpret_cast<LPARAM>(&selEnd));

    int line = static_cast<int>(SendMessageW(hwnd, EM_EXLINEFROMCHAR, 0, selStart));
    int lineStart = static_cast<int>(SendMessageW(hwnd, EM_LINEINDEX, line, 0));
    int caretCol = static_cast<int>(selStart) - lineStart;

    std::wstring head = GetEditorTextRange(hwnd, lineStart, lineStart + caretCol);
    size_t indentLen = 0;
    while (indentLen < head.size() &&
           (head[indentLen] == L' ' || head[indentLen] == L'\t'))
        ++indentLen;

    std::wstring insert = L"\r";
    insert.append(head, 0, indentLen);
    SendMessageW(hwnd, EM_REPLACESEL, TRUE, reinterpret_cast<LPARAM>(insert.c_str()));
    SendMessageW(hwnd, EM_SCROLLCARET, 0, 0);
}

void DeleteWordBackward()
{
    DWORD start = 0, end = 0;
    SendMessageW(g_hwndEditor, EM_GETSEL, reinterpret_cast<WPARAM>(&start), reinterpret_cast<LPARAM>(&end));
    if (start != end)
    {
        SendMessageW(g_hwndEditor, EM_REPLACESEL, TRUE, reinterpret_cast<LPARAM>(L""));
        return;
    }
    if (start == 0)
        return;
    std::wstring text = GetEditorText();
    size_t pos = start;
    while (pos > 0 && iswspace(text[pos - 1]))
        --pos;
    while (pos > 0 && !iswspace(text[pos - 1]))
        --pos;
    SendMessageW(g_hwndEditor, EM_SETSEL, pos, start);
    SendMessageW(g_hwndEditor, EM_REPLACESEL, TRUE, reinterpret_cast<LPARAM>(L""));
}

void DeleteWordForward()
{
    DWORD start = 0, end = 0;
    SendMessageW(g_hwndEditor, EM_GETSEL, reinterpret_cast<WPARAM>(&start), reinterpret_cast<LPARAM>(&end));
    if (start != end)
    {
        SendMessageW(g_hwndEditor, EM_REPLACESEL, TRUE, reinterpret_cast<LPARAM>(L""));
        return;
    }
    std::wstring text = GetEditorText();
    size_t len = text.size();
    size_t pos = start;
    while (pos < len && !iswspace(text[pos]))
        ++pos;
    while (pos < len && iswspace(text[pos]))
        ++pos;
    SendMessageW(g_hwndEditor, EM_SETSEL, start, pos);
    SendMessageW(g_hwndEditor, EM_REPLACESEL, TRUE, reinterpret_cast<LPARAM>(L""));
}

void DuplicateLine()
{
    HWND ed = g_hwndEditor;
    DWORD selStart = 0, selEnd = 0;
    SendMessageW(ed, EM_GETSEL, reinterpret_cast<WPARAM>(&selStart), reinterpret_cast<LPARAM>(&selEnd));

    int line = static_cast<int>(SendMessageW(ed, EM_EXLINEFROMCHAR, 0, selStart));
    int lineStart = static_cast<int>(SendMessageW(ed, EM_LINEINDEX, line, 0));
    int lineLen = static_cast<int>(SendMessageW(ed, EM_LINELENGTH, lineStart, 0));

    // EM_GETLINE wants the first WCHAR of the buffer to hold its capacity
    // (in TCHARs). Allocate space for that plus the line content.
    // Msftedit (modern RichEdit) sometimes copies the trailing \r into
    // the buffer even though MSDN says otherwise — clamp to lineLen so
    // we never carry a stray paragraph terminator into the duplicate.
    std::vector<wchar_t> buf(static_cast<size_t>(lineLen) + 2, L'\0');
    *reinterpret_cast<WORD *>(buf.data()) = static_cast<WORD>(buf.size());
    int copied = static_cast<int>(SendMessageW(ed, EM_GETLINE, line, reinterpret_cast<LPARAM>(buf.data())));
    if (copied > lineLen)
        copied = lineLen;
    std::wstring lineText(buf.data(), copied);

    // RichEdit 2.0+ uses CR-only ('\r') as the paragraph break internally.
    // Inserting "\r\n" produces two breaks (one from \r, one from \n) and
    // a stray blank line. A single \r is the right separator here.
    int lineEnd = lineStart + lineLen;
    LRESULT oldMask = SendMessageW(ed, EM_SETEVENTMASK, 0, 0);
    SendMessageW(ed, EM_SETSEL, lineEnd, lineEnd);
    std::wstring insert = L"\r" + lineText;
    SendMessageW(ed, EM_REPLACESEL, TRUE, reinterpret_cast<LPARAM>(insert.c_str()));
    SendMessageW(ed, EM_SETEVENTMASK, 0, oldMask);

    // Place caret at the same column on the new (duplicated) line.
    int caretCol = static_cast<int>(selStart) - lineStart;
    int newCaret = lineEnd + 1 + caretCol;
    SendMessageW(ed, EM_SETSEL, newCaret, newCaret);
    SendMessageW(ed, EM_SCROLLCARET, 0, 0);
}

void DeleteLine()
{
    HWND ed = g_hwndEditor;
    DWORD selStart = 0, selEnd = 0;
    SendMessageW(ed, EM_GETSEL, reinterpret_cast<WPARAM>(&selStart), reinterpret_cast<LPARAM>(&selEnd));

    int line = static_cast<int>(SendMessageW(ed, EM_EXLINEFROMCHAR, 0, selStart));
    int lineStart = static_cast<int>(SendMessageW(ed, EM_LINEINDEX, line, 0));
    int totalLines = static_cast<int>(SendMessageW(ed, EM_GETLINECOUNT, 0, 0));
    int delStart = lineStart;
    int delEnd;
    int newCaret = lineStart;

    if (line + 1 < totalLines)
    {
        // Not the last line — also swallow the trailing newline.
        delEnd = static_cast<int>(SendMessageW(ed, EM_LINEINDEX, line + 1, 0));
    }
    else if (line > 0)
    {
        // Last line of a multi-line buffer — eat the preceding newline so
        // we don't leave a dangling empty line.
        int prevStart = static_cast<int>(SendMessageW(ed, EM_LINEINDEX, line - 1, 0));
        int prevLen = static_cast<int>(SendMessageW(ed, EM_LINELENGTH, prevStart, 0));
        delStart = prevStart + prevLen;
        delEnd = static_cast<int>(SendMessageW(ed, WM_GETTEXTLENGTH, 0, 0));
        newCaret = delStart;
    }
    else
    {
        // The single line — just clear its content.
        delEnd = static_cast<int>(SendMessageW(ed, WM_GETTEXTLENGTH, 0, 0));
    }

    LRESULT oldMask = SendMessageW(ed, EM_SETEVENTMASK, 0, 0);
    SendMessageW(ed, EM_SETSEL, delStart, delEnd);
    SendMessageW(ed, EM_REPLACESEL, TRUE, reinterpret_cast<LPARAM>(L""));
    SendMessageW(ed, EM_SETEVENTMASK, 0, oldMask);
    SendMessageW(ed, EM_SETSEL, newCaret, newCaret);
    SendMessageW(ed, EM_SCROLLCARET, 0, 0);
}

static void PasteAsPlainText(HWND hwnd)
{
    if (!OpenClipboard(hwnd))
        return;

    // CF_UNICODETEXT is the authoritative source for characters — it's
    // already UTF-16 from Windows, no codepage decoding involved.
    //
    // Detecting "this came from a rich source we should re-format" via
    // CF_HTML rather than CF_RTF: Word / Outlook / browsers put CF_HTML
    // on the clipboard, RichEdit-only copies (our own editor, WordPad)
    // do not. That keeps in-editor copy/paste round-trips exact while
    // still rescuing paragraphs and bullets from Word.
    static const UINT cfHtml = RegisterClipboardFormatW(L"HTML Format");
    bool isWordLike = (cfHtml != 0 && IsClipboardFormatAvailable(cfHtml) != FALSE);

    HANDLE hData = GetClipboardData(CF_UNICODETEXT);
    if (!hData)
    {
        CloseClipboard();
        return;
    }
    LPCWSTR raw = static_cast<LPCWSTR>(GlobalLock(hData));
    if (!raw)
    {
        CloseClipboard();
        return;
    }
    std::wstring textBuf(raw);
    GlobalUnlock(hData);

    if (textBuf.empty())
    {
        CloseClipboard();
        return;
    }
    if (isWordLike)
        textBuf = NormalizeRichPaste(textBuf);

    // RichEdit 2.0+ stores paragraph breaks as a single \r. Inserting a
    // \r\n via EM_REPLACESEL is treated as two separate breaks and adds
    // a stray blank line per line, so collapse all line-ending forms to
    // a lone \r before insertion.
    {
        std::wstring norm;
        norm.reserve(textBuf.size());
        for (size_t i = 0; i < textBuf.size(); ++i)
        {
            wchar_t ch = textBuf[i];
            if (ch == L'\r')
            {
                norm += L'\r';
                if (i + 1 < textBuf.size() && textBuf[i + 1] == L'\n')
                    ++i;
            }
            else if (ch == L'\n')
            {
                norm += L'\r';
            }
            else
            {
                norm += ch;
            }
        }
        textBuf = std::move(norm);
    }
    LPCWSTR text = textBuf.c_str();

    DWORD selStart = 0, selEnd = 0;
    SendMessageW(hwnd, EM_GETSEL, reinterpret_cast<WPARAM>(&selStart), reinterpret_cast<LPARAM>(&selEnd));

    // Configure the editor's default character format on the *insertion
    // point* before the paste, then run a single EM_REPLACESEL. The
    // inserted text inherits the IP format we just set — RichEdit's
    // "replacement text takes on the character formatting of the
    // preceding character / IP" rule kicks in here. The big win: setting
    // SCF_SELECTION on a collapsed selection touches no existing
    // character, so it isn't recorded as an undoable edit. The paste
    // becomes the SOLE undo step. Ctrl+Z reverses it; Ctrl+Y restores it.
    //
    // The earlier post-paste approach (apply CHARFORMAT2 to the inserted
    // range after EM_REPLACESEL) created a second undo entry on top of
    // the paste. First Ctrl+Z then only reversed the format change —
    // visualised as the full pasted block being selected, since RichEdit
    // restores the selection that was active when the format applied —
    // and a second Ctrl+Z was needed to actually remove the text. Both
    // Undo(tomSuspend) and BeginEditCollection() empirically broke the
    // redo stack: Ctrl+Y after Ctrl+Z became a no-op. Pre-format avoids
    // the whole class of problems.
    HDC hdc = GetDC(hwnd);
    int sizeTwips = MulDiv(g_state.fontSize * g_state.zoomLevel / 100, 1440, 72);
    ReleaseDC(hwnd, hdc);

    CHARFORMAT2W cf = {};
    cf.cbSize = sizeof(cf);
    cf.dwMask = CFM_FACE | CFM_SIZE | CFM_BOLD | CFM_ITALIC | CFM_UNDERLINE | CFM_UNDERLINETYPE | CFM_COLOR | CFM_BACKCOLOR;
    wcscpy_s(cf.szFaceName, g_state.fontName.c_str());
    cf.yHeight = sizeTwips;
    // AUTOBACKCOLOR so the pasted text follows the editor's bg colour
    // (EM_SETBKGNDCOLOR) instead of baking in a per-char dark/light value
    // that would survive theme switches.
    cf.dwEffects = CFE_AUTOBACKCOLOR;
    if (g_state.fontWeight >= FW_BOLD)
        cf.dwEffects |= CFE_BOLD;
    if (g_state.fontItalic)
        cf.dwEffects |= CFE_ITALIC;
    if (g_state.fontUnderline)
        cf.dwEffects |= CFE_UNDERLINE;
    cf.bUnderlineType = g_state.fontUnderline ? CFU_UNDERLINE : CFU_UNDERLINENONE;
    cf.crTextColor = GetEditorTextColor();

    LRESULT oldMask = SendMessageW(hwnd, EM_SETEVENTMASK, 0, 0);
    // Collapse to the insertion point, push the IP format, then restore
    // the user's original selection so EM_REPLACESEL still replaces it
    // (instead of just inserting at the caret).
    SendMessageW(hwnd, EM_SETSEL, selStart, selStart);
    SendMessageW(hwnd, EM_SETCHARFORMAT, SCF_SELECTION, reinterpret_cast<LPARAM>(&cf));
    SendMessageW(hwnd, EM_SETSEL, selStart, selEnd);
    SendMessageW(hwnd, EM_SETEVENTMASK, 0, oldMask);

    SendMessageW(hwnd, EM_REPLACESEL, TRUE, reinterpret_cast<LPARAM>(text));

    CloseClipboard();
}

static void DrawSpecialCharMarkers(HWND hwnd)
{
    HDC hdc = GetDC(hwnd);
    if (!hdc)
        return;

    RECT rcClient;
    GetClientRect(hwnd, &rcClient);

    HFONT hFont = reinterpret_cast<HFONT>(SendMessageW(hwnd, WM_GETFONT, 0, 0));
    HFONT oldFont = nullptr;
    if (hFont)
        oldFont = reinterpret_cast<HFONT>(SelectObject(hdc, hFont));

    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, IsMatrixTheme() ? RGB(40, 120, 55)
                                      : (IsDarkMode() ? RGB(110, 110, 110) : RGB(180, 180, 180)));

    int firstLine = static_cast<int>(SendMessageW(hwnd, EM_GETFIRSTVISIBLELINE, 0, 0));
    int totalLines = static_cast<int>(SendMessageW(hwnd, EM_GETLINECOUNT, 0, 0));
    int textLen = static_cast<int>(SendMessageW(hwnd, WM_GETTEXTLENGTH, 0, 0));

    for (int line = firstLine; line < totalLines; ++line)
    {
        int lineStart = static_cast<int>(SendMessageW(hwnd, EM_LINEINDEX, line, 0));
        if (lineStart < 0)
            break;
        int lineLen = static_cast<int>(SendMessageW(hwnd, EM_LINELENGTH, lineStart, 0));

        // Check if first char of this line is off-screen at bottom — stop.
        POINTL ptStart{};
        SendMessageW(hwnd, EM_POSFROMCHAR, reinterpret_cast<WPARAM>(&ptStart), lineStart);
        if (ptStart.y > rcClient.bottom)
            break;

        wchar_t buf[4097];
        int copied = 0;
        if (lineLen > 0 && lineLen < 4096)
        {
            *reinterpret_cast<WORD *>(buf) = static_cast<WORD>(lineLen);
            copied = static_cast<int>(SendMessageW(hwnd, EM_GETLINE, line, reinterpret_cast<LPARAM>(buf)));
            buf[copied] = 0;

            for (int i = 0; i < copied; ++i)
            {
                wchar_t ch = buf[i];
                const wchar_t *marker = nullptr;
                if (ch == L' ')
                    marker = L"·";
                else if (ch == L'\t')
                    marker = L"→";
                if (!marker)
                    continue;
                POINTL pt{};
                SendMessageW(hwnd, EM_POSFROMCHAR, reinterpret_cast<WPARAM>(&pt), lineStart + i);
                TextOutW(hdc, pt.x, pt.y, marker, 1);
            }
        }

        // Draw pilcrow at the end of the line's content.
        // EM_POSFROMCHAR on the CR/LF index returns the start of the next
        // visual line, so drawing there would obscure the next line's first
        // glyph. Measuring the full line via GetTextExtentPoint32W is also
        // unreliable — tabs, kerning, and char-level CHARFORMAT can make
        // the cumulative width drift from what RichEdit actually rendered.
        // The robust approach: ask RichEdit itself for the position of the
        // LAST character on this line, then advance by one character width.
        // Skip soft-wrapped continuations (no hard break at eolIdx).
        int eolIdx = lineStart + lineLen;
        if (eolIdx < textLen)
        {
            int nextLineStart = static_cast<int>(SendMessageW(hwnd, EM_LINEINDEX, line + 1, 0));
            bool isHardBreak = (nextLineStart > eolIdx);
            if (isHardBreak)
            {
                int markerX, markerY;
                if (lineLen > 0 && copied > 0)
                {
                    POINTL ptLast{};
                    SendMessageW(hwnd, EM_POSFROMCHAR, reinterpret_cast<WPARAM>(&ptLast), eolIdx - 1);
                    SIZE chSz{};
                    wchar_t lastCh = buf[copied - 1];
                    GetTextExtentPoint32W(hdc, &lastCh, 1, &chSz);
                    markerX = ptLast.x + chSz.cx;
                    markerY = ptLast.y;
                }
                else
                {
                    // Empty line — marker at line start.
                    markerX = ptStart.x;
                    markerY = ptStart.y;
                }
                TextOutW(hdc, markerX, markerY, L"¶", 1);
            }
        }
    }

    if (oldFont)
        SelectObject(hdc, oldFont);
    ReleaseDC(hwnd, hdc);
}

LRESULT CALLBACK EditorSubclassProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
    case WM_PAINT:
    {
        LRESULT result = CallWindowProcW(g_origEditorProc, hwnd, msg, wParam, lParam);
        // Skip marker overlay while user is drag-selecting — the extra GDI
        // operations on the same DC interfere with RichEdit's selection
        // tracking and cause the selection to release mid-drag.
        if (g_state.showSpecialChars && !(GetKeyState(VK_LBUTTON) & 0x8000))
            DrawSpecialCharMarkers(hwnd);
        if (g_state.showLineNumbers && g_hwndGutter)
            InvalidateRect(g_hwndGutter, nullptr, FALSE);
        return result;
    }
    case WM_LBUTTONUP:
    {
        LRESULT result = CallWindowProcW(g_origEditorProc, hwnd, msg, wParam, lParam);
        if (g_state.showSpecialChars)
            InvalidateRect(hwnd, nullptr, FALSE);
        return result;
    }
    case WM_LBUTTONDBLCLK:
    {
        LRESULT result = CallWindowProcW(g_origEditorProc, hwnd, msg, wParam, lParam);
        // RichEdit's word-select, when the double-click lands past the end
        // of a line, grabs the invisible paragraph mark (a lone \r) — a
        // 1-char "phantom" selection. Collapse it so nothing is selected.
        // A selection containing any real character (incl. genuine
        // trailing spaces) is left untouched.
        DWORD s = 0, e = 0;
        SendMessageW(hwnd, EM_GETSEL, reinterpret_cast<WPARAM>(&s), reinterpret_cast<LPARAM>(&e));
        if (e > s)
        {
            std::wstring sel(static_cast<size_t>(e - s) + 1, L'\0');
            SendMessageW(hwnd, EM_GETSELTEXT, 0, reinterpret_cast<LPARAM>(sel.data()));
            sel.resize(wcslen(sel.c_str()));
            bool onlyBreaks = !sel.empty();
            for (wchar_t c : sel)
            {
                if (c != L'\r' && c != L'\n')
                {
                    onlyBreaks = false;
                    break;
                }
            }
            if (onlyBreaks)
                SendMessageW(hwnd, EM_SETSEL, s, s);
        }
        return result;
    }
    case WM_PASTE:
        PasteAsPlainText(hwnd);
        return 0;
    case WM_CHAR:
        if (wParam == 9)
        {
            // Tab is handled entirely in WM_KEYDOWN — drop the WM_CHAR that
            // TranslateMessage queues for VK_TAB so RichEdit doesn't insert
            // a second \t after our manual handling.
            return 0;
        }
        if (wParam == 13)
        {
            // Plain / Shift+Enter is handled in WM_KEYDOWN (auto-indent) —
            // drop the CR that TranslateMessage queues so RichEdit doesn't
            // add a second, un-indented line break. Ctrl+Enter yields a LF
            // (wParam 10) instead and is left for the default handler.
            return 0;
        }
        if (wParam == 3)
            break;
        if (wParam == 22)
            break;
        if (wParam == 24)
            break;
        if (wParam == 26)
            break;
        if (wParam == 25)
            break;
        if (wParam == 127 || wParam == 8)
        {
            if (GetKeyState(VK_CONTROL) & 0x8000)
                return 0;
        }
        break;
    case WM_KEYDOWN:
        if (wParam == VK_TAB && !(GetKeyState(VK_CONTROL) & 0x8000))
        {
            bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
            DWORD ss = 0, se = 0;
            SendMessageW(hwnd, EM_GETSEL, reinterpret_cast<WPARAM>(&ss), reinterpret_cast<LPARAM>(&se));
            // No selection + plain Tab: insert one \t at the caret. The
            // multi-line indent helper is reserved for selections (and for
            // Shift+Tab, which dedents even with no selection).
            if (ss == se && !shift)
            {
                SendMessageW(hwnd, EM_REPLACESEL, TRUE, reinterpret_cast<LPARAM>(L"\t"));
            }
            else
            {
                IndentDedentLines(hwnd, shift);
            }
            return 0;
        }
        if (wParam == VK_RETURN && !(GetKeyState(VK_CONTROL) & 0x8000))
        {
            // Auto-indent: the new line inherits the leading whitespace of
            // the current one. Covers plain Enter and Shift+Enter.
            InsertNewlineWithIndent(hwnd);
            return 0;
        }
        if (GetKeyState(VK_CONTROL) & 0x8000)
        {
            if (wParam == VK_BACK)
            {
                DeleteWordBackward();
                return 0;
            }
            if (wParam == VK_DELETE)
            {
                DeleteWordForward();
                return 0;
            }
        }
        break;
    case WM_SYSKEYDOWN:
        // Alt+Up / Alt+Down move the current line(s). Everything else
        // (Alt+letter menu access, etc.) falls through to the default.
        if ((GetKeyState(VK_CONTROL) & 0x8000) == 0 &&
            (GetKeyState(VK_SHIFT) & 0x8000) == 0)
        {
            if (wParam == VK_UP)
            {
                MoveLines(hwnd, false);
                return 0;
            }
            if (wParam == VK_DOWN)
            {
                MoveLines(hwnd, true);
                return 0;
            }
        }
        break;
    case WM_MOUSEACTIVATE:
        // RichEdit defaults to MA_ACTIVATEANDEAT for an inactive-window
        // click, which suppresses the WM_LBUTTONDOWN that would otherwise
        // position the caret and start a selection drag. Return MA_ACTIVATE
        // so the first click both activates the window and reaches the
        // editor as a normal mouse-down — matching classic Notepad.
        return MA_ACTIVATE;
    case WM_MOUSEWHEEL:
    {
        int delta = GET_WHEEL_DELTA_WPARAM(wParam);
        if (LOWORD(wParam) & MK_SHIFT)
        {
            UINT scrollLines = 3;
            SystemParametersInfoW(SPI_GETWHEELSCROLLLINES, 0, &scrollLines, 0);
            if (scrollLines == (UINT)WHEEL_PAGESCROLL)
            {
                SendMessageW(hwnd, WM_HSCROLL, (delta > 0) ? SB_PAGELEFT : SB_PAGERIGHT, 0);
            }
            else
            {
                for (UINT i = 0; i < scrollLines; ++i)
                    SendMessageW(hwnd, WM_HSCROLL, (delta > 0) ? SB_LINELEFT : SB_LINERIGHT, 0);
            }
            return 0;
        }
        if (GetKeyState(VK_CONTROL) & 0x8000)
            break;
        // Replace RichEdit's animated smooth-scroll with discrete line
        // steps so the wheel feels like classic Edit-control Notepad.
        UINT scrollLines = 3;
        SystemParametersInfoW(SPI_GETWHEELSCROLLLINES, 0, &scrollLines, 0);
        if (scrollLines == (UINT)WHEEL_PAGESCROLL)
        {
            SendMessageW(hwnd, WM_VSCROLL, (delta > 0) ? SB_PAGEUP : SB_PAGEDOWN, 0);
        }
        else
        {
            for (UINT i = 0; i < scrollLines; ++i)
                SendMessageW(hwnd, WM_VSCROLL, (delta > 0) ? SB_LINEUP : SB_LINEDOWN, 0);
        }
        return 0;
    }
    case WM_MOUSEHWHEEL:
    {
        int delta = GET_WHEEL_DELTA_WPARAM(wParam);
        UINT scrollChars = 3;
        SystemParametersInfoW(SPI_GETWHEELSCROLLCHARS, 0, &scrollChars, 0);
        if (delta != 0)
        {
            for (UINT i = 0; i < scrollChars; ++i)
                SendMessageW(hwnd, WM_HSCROLL, (delta > 0) ? SB_LINERIGHT : SB_LINELEFT, 0);
            return 0;
        }
        break;
    }
    case WM_CONTEXTMENU:
    {
        if (reinterpret_cast<HWND>(wParam) && reinterpret_cast<HWND>(wParam) != hwnd)
            break;

        POINT pt = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
        if (pt.x == -1 && pt.y == -1)
        {
            GetCaretPos(&pt);
            ClientToScreen(hwnd, &pt);
        }

        DWORD selStart = 0, selEnd = 0;
        SendMessageW(hwnd, EM_GETSEL, reinterpret_cast<WPARAM>(&selStart), reinterpret_cast<LPARAM>(&selEnd));
        bool hasSelection = selStart != selEnd;
        bool canUndo = SendMessageW(hwnd, EM_CANUNDO, 0, 0) != 0;
        bool canPaste = IsClipboardFormatAvailable(CF_UNICODETEXT) || IsClipboardFormatAvailable(CF_TEXT);

        const auto &lang = GetLangStrings();
        HMENU hMenu = CreatePopupMenu();
        AppendMenuW(hMenu, MF_STRING, IDM_EDIT_UNDO, lang.menuUndo.c_str());
        AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(hMenu, MF_STRING, IDM_EDIT_CUT, lang.menuCut.c_str());
        AppendMenuW(hMenu, MF_STRING, IDM_EDIT_COPY, lang.menuCopy.c_str());
        AppendMenuW(hMenu, MF_STRING, IDM_EDIT_PASTE, lang.menuPaste.c_str());
        AppendMenuW(hMenu, MF_STRING, IDM_EDIT_DELETE, lang.menuDelete.c_str());
        AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(hMenu, MF_STRING, IDM_EDIT_SELECTALL, lang.menuSelectAll.c_str());

        EnableMenuItem(hMenu, IDM_EDIT_UNDO, MF_BYCOMMAND | (canUndo ? MF_ENABLED : MF_GRAYED));
        EnableMenuItem(hMenu, IDM_EDIT_CUT, MF_BYCOMMAND | (hasSelection ? MF_ENABLED : MF_GRAYED));
        EnableMenuItem(hMenu, IDM_EDIT_COPY, MF_BYCOMMAND | (hasSelection ? MF_ENABLED : MF_GRAYED));
        EnableMenuItem(hMenu, IDM_EDIT_PASTE, MF_BYCOMMAND | (canPaste ? MF_ENABLED : MF_GRAYED));
        EnableMenuItem(hMenu, IDM_EDIT_DELETE, MF_BYCOMMAND | (hasSelection ? MF_ENABLED : MF_GRAYED));

        // Mirror the Tools menu as a submenu, but only when the user has
        // turned Tools on — otherwise the context menu stays minimal.
        // DestroyMenu(hMenu) below recursively frees this submenu too.
        if (g_state.toolsEnabled)
        {
            HMENU hTools = CreatePopupMenu();
            AppendMenuW(hTools, MF_STRING, IDM_TOOLS_NORMALIZE, lang.menuToolsNormalize.c_str());
            AppendMenuW(hTools, MF_STRING, IDM_TOOLS_BASE64, lang.menuToolsBase64.c_str());
            AppendMenuW(hTools, MF_STRING, IDM_TOOLS_SHA1, lang.menuToolsSha1.c_str());
            AppendMenuW(hTools, MF_STRING, IDM_TOOLS_MD5, lang.menuToolsMd5.c_str());
            AppendMenuW(hTools, MF_SEPARATOR, 0, nullptr);
            AppendMenuW(hTools, MF_STRING, IDM_TOOLS_UPPERCASE, lang.menuToolsUppercase.c_str());
            AppendMenuW(hTools, MF_STRING, IDM_TOOLS_LOWERCASE, lang.menuToolsLowercase.c_str());
            AppendMenuW(hTools, MF_STRING, IDM_TOOLS_TITLECASE, lang.menuToolsTitleCase.c_str());
            AppendMenuW(hTools, MF_STRING, IDM_TOOLS_TRIMTRAILING, lang.menuToolsTrimTrailing.c_str());
            AppendMenuW(hTools, MF_STRING, IDM_TOOLS_TABS2SPACES, lang.menuToolsTabsToSpaces.c_str());
            AppendMenuW(hTools, MF_STRING, IDM_TOOLS_SPACES2TABS, lang.menuToolsSpacesToTabs.c_str());
            AppendMenuW(hTools, MF_SEPARATOR, 0, nullptr);
            AppendMenuW(hTools, MF_STRING, IDM_TOOLS_REVERSELINES, lang.menuToolsReverseLines.c_str());
            AppendMenuW(hTools, MF_STRING, IDM_TOOLS_JOINLINES, lang.menuToolsJoinLines.c_str());
            AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
            AppendMenuW(hMenu, MF_POPUP, reinterpret_cast<UINT_PTR>(hTools), lang.menuTools.c_str());
        }

        int cmd = TrackPopupMenu(hMenu, TPM_RETURNCMD | TPM_RIGHTBUTTON, pt.x, pt.y, 0, g_hwndMain, nullptr);
        DestroyMenu(hMenu);
        if (cmd)
            SendMessageW(g_hwndMain, WM_COMMAND, MAKEWPARAM(cmd, 0), 0);
        return 0;
    }
    }
    return CallWindowProcW(g_origEditorProc, hwnd, msg, wParam, lParam);
}
