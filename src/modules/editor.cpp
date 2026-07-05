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
#include "spellchecker.h"
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

// First visible line captured at WM_KILLFOCUS so it can be restored once
// the editor regains focus. RichEdit's focus handling runs a
// scroll-caret-into-view pass; with the caret at the end of a long pasted
// document that yanks the viewport to the bottom even though the user had
// scrolled elsewhere. Notepad / Notepad++ keep the scroll across focus
// changes — only an actual edit / caret move scrolls. -1 = nothing pinned.
static int g_pinnedFirstLine = -1;

// Caret position captured alongside g_pinnedFirstLine. If the caret has moved
// by the time focus returns, something navigated the document while the editor
// was unfocused (Find / Replace / Go To Line all EM_SETSEL on the unfocused
// editor), so the pinned viewport is stale and must NOT be restored — doing so
// snapped the view back to where it was before the search, undoing the jump to
// the match. -1 = nothing pinned.
static int g_pinnedCaret = -1;

// True only while the user is drag-selecting text *inside* the editor
// (set on WM_LBUTTONDOWN, cleared on WM_LBUTTONUP). The highlight overlay
// must be skipped mid-drag-select (its GDI on the shared DC makes RichEdit
// release the selection), but the earlier GetKeyState(VK_LBUTTON) test also
// fired while the left button was held to drag the *window border* during a
// resize — which wrongly suppressed the current-line highlight and left it
// blank after enlarging the window. This flag is true for text drags only.
static bool g_editorDragSelecting = false;

// Posted to the editor from WM_SETFOCUS as an early (often zero-flicker)
// restore attempt. RichEdit's scroll-caret pass may be synchronous, queued
// as a message, or deferred to a later paint/timer — we can't know which,
// so the restore is attempted at several settle points and the pin is only
// disarmed by the final timer below.
#define WM_RESTORE_SCROLL (WM_USER + 0x137)

// Backstop timer set in WM_SETFOCUS. Fires after the whole activation dance
// (messages drained, deferred scroll done, uncover repaint complete), so it
// catches the scroll-to-caret no matter how RichEdit scheduled it. Also the
// only place the pin is cleared. ~40 ms: long enough to outlast the deferred
// work, short enough that any visible snap-back is a couple of frames.
#define RESTORE_SCROLL_TIMER_ID 0xCE50C011
#define RESTORE_SCROLL_DELAY_MS 40

// Pull the viewport back to the pinned first-visible-line if it has drifted.
// Does NOT clear the pin — that's the timer's job, so earlier attempts can't
// disarm before RichEdit's (possibly later) scroll has actually happened.
static void RestorePinnedScroll(HWND hwnd)
{
    if (g_pinnedFirstLine < 0)
        return;
    // If the caret moved while the editor was unfocused — e.g. Find or Go To
    // Line scrolled to a match — the pinned viewport is stale. Honour the new
    // caret position (RichEdit already scrolled it into view) and abandon the
    // pin instead of yanking the view back to where the search started.
    DWORD selStart = 0, selEnd = 0;
    SendMessageW(hwnd, EM_GETSEL, reinterpret_cast<WPARAM>(&selStart), reinterpret_cast<LPARAM>(&selEnd));
    if (static_cast<int>(selStart) != g_pinnedCaret)
    {
        g_pinnedFirstLine = -1;
        return;
    }
    int now = static_cast<int>(SendMessageW(hwnd, EM_GETFIRSTVISIBLELINE, 0, 0));
    if (now != g_pinnedFirstLine)
        SendMessageW(hwnd, EM_LINESCROLL, 0, g_pinnedFirstLine - now);
}

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

// Make RichEdit behave like the plain EDIT control classic Notepad uses:
// one font for the entire document, never silently swapped. By default
// RichEdit enables IMF_AUTOFONT — it inspects the Unicode script of each
// run and substitutes an "associated font" for characters it thinks need
// one. Word text is full of such characters (smart quotes " " ' ', en/em
// dashes – —, NBSP, ellipsis …), so pasting it via CF_UNICODETEXT — which
// carries no font data at all — still produced a patchwork of faces,
// because RichEdit re-fonts the runs *after* the insert. WM_SETFONT only
// sets the default; it doesn't undo auto-font runs. Clearing these flags
// once at control creation removes the feature entirely, so every glyph
// renders in g_state.hFont regardless of where the text came from.
void SetEditorPlainTextMode(HWND hwnd)
{
    LRESULT opts = SendMessageW(hwnd, EM_GETLANGOPTIONS, 0, 0);
    opts &= ~(IMF_AUTOFONT | IMF_AUTOFONTSIZEADJUST | IMF_DUALFONT);
    SendMessageW(hwnd, EM_SETLANGOPTIONS, 0, opts);
}

// Suspend / resume RichEdit's undo recording via TOM. Applying the theme or
// font colour goes through EM_SETCHARFORMAT SCF_ALL, which RichEdit records as
// an undoable edit — so Ctrl+Z would revert a colour change instead of a real
// text edit (typed text flipping green->white->black under the Matrix theme,
// and the startup theme passes piling several colour steps onto the stack).
// Bracketing the format calls in suspend/resume keeps colour changes out of
// the undo history entirely. Calls must be balanced.
static const long kTomSuspend = -9999995;
static const long kTomResume = -9999994;
void SetEditorUndoSuspended(bool suspend)
{
    if (!g_hwndEditor)
        return;
    IRichEditOle *ole = nullptr;
    if (!SendMessageW(g_hwndEditor, EM_GETOLEINTERFACE, 0, reinterpret_cast<LPARAM>(&ole)) || !ole)
        return;
    ITextDocument *doc = nullptr;
    if (SUCCEEDED(ole->QueryInterface(kIID_ITextDocument, reinterpret_cast<void **>(&doc))) && doc)
    {
        doc->Undo(suspend ? kTomSuspend : kTomResume, nullptr);
        doc->Release();
    }
    ole->Release();
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
    SetEditorUndoSuspended(true);
    SendMessageW(g_hwndEditor, WM_SETFONT, reinterpret_cast<WPARAM>(g_state.hFont), TRUE);
    COLORREF textColor = GetEditorTextColor();
    CHARFORMAT2W cf = {};
    cf.cbSize = sizeof(cf);
    cf.dwMask = CFM_COLOR;
    cf.crTextColor = textColor;
    SendMessageW(g_hwndEditor, EM_SETCHARFORMAT, SCF_ALL, reinterpret_cast<LPARAM>(&cf));
    SendMessageW(g_hwndEditor, EM_SETCHARFORMAT, SCF_DEFAULT, reinterpret_cast<LPARAM>(&cf));
    SetEditorUndoSuspended(false);
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
    SetEditorPlainTextMode(g_hwndEditor);
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
    // Let RichEdit do the paste through its native EM_PASTESPECIAL path,
    // restricted to CF_UNICODETEXT — that is exactly "plain text only,
    // no formatting / no list rescue / nothing cut", the same as Notepad
    // / Notepad++. RichEdit handles clipboard open/close, line-ending
    // normalisation, scrollbar / layout updates, and undo internally.
    //
    // Earlier we did this manually (open clipboard → read CF_UNICODETEXT
    // → CRLF→CR → EM_REPLACESEL → EM_SCROLLCARET). That path left
    // RichEdit's layout cache in an inconsistent state for large Word
    // pastes: after Alt-Tab away and back, or after another window was
    // brought over and then hidden, the editor area painted blank until
    // the user scrolled. Going through EM_PASTESPECIAL fixes it because
    // RichEdit's own paste finishes by issuing the same scrollbar /
    // visible-region updates it issues for typed input — which our
    // manual path skipped.
    SendMessageW(hwnd, EM_PASTESPECIAL, CF_UNICODETEXT, 0);
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

// Draw a small zig-zag (triangle wave, ±1 px) between x1 and x2 at the
// given baseline. Used as a non-invasive spell-check underline that
// lives purely on the editor's overlay — no CHARFORMAT touched, so
// RichEdit's layout stays clean even on huge documents with many errors.
static void DrawSpellWave(HDC hdc, int x1, int x2, int y)
{
    if (x2 <= x1)
        return;
    const int step = 2;
    int count = ((x2 - x1) / step) + 2;
    if (count > 4096)
        count = 4096; // sanity cap for absurdly wide errors
    std::vector<POINT> pts;
    pts.reserve(static_cast<size_t>(count));
    bool up = true;
    for (int x = x1; x <= x2; x += step)
    {
        pts.push_back({x, up ? y - 1 : y + 1});
        up = !up;
    }
    if (pts.size() >= 2)
        Polyline(hdc, pts.data(), static_cast<int>(pts.size()));
}

static void DrawSpellErrors(HWND hwnd)
{
    const auto &errors = GetSpellErrors();
    if (errors.empty())
        return;

    RECT rcClient;
    GetClientRect(hwnd, &rcClient);
    int totalLines = static_cast<int>(SendMessageW(hwnd, EM_GETLINECOUNT, 0, 0));
    int totalChars = static_cast<int>(SendMessageW(hwnd, WM_GETTEXTLENGTH, 0, 0));
    int firstVisLine = static_cast<int>(SendMessageW(hwnd, EM_GETFIRSTVISIBLELINE, 0, 0));
    int firstVisChar = static_cast<int>(SendMessageW(hwnd, EM_LINEINDEX, firstVisLine, 0));
    if (firstVisChar < 0)
        firstVisChar = 0;

    // Sample line height from two known-laid-out adjacent visible lines.
    // EM_POSFROMCHAR is only reliable for char positions RichEdit has
    // already laid out — sampling from char 0 when scrolled far down a
    // huge document returned bogus coordinates and the wave landed on
    // the menu / status bar. Visible lines are always laid out.
    int lineHeight = 0;
    if (firstVisLine + 1 < totalLines)
    {
        POINTL p0{}, p1{};
        int line0Char = firstVisChar;
        int line1Char = static_cast<int>(SendMessageW(hwnd, EM_LINEINDEX, firstVisLine + 1, 0));
        SendMessageW(hwnd, EM_POSFROMCHAR, reinterpret_cast<WPARAM>(&p0), line0Char);
        SendMessageW(hwnd, EM_POSFROMCHAR, reinterpret_cast<WPARAM>(&p1), line1Char);
        lineHeight = p1.y - p0.y;
    }
    if (lineHeight < 6 || lineHeight > 200)
    {
        HDC dcTmp = GetDC(hwnd);
        HFONT hFont = reinterpret_cast<HFONT>(SendMessageW(hwnd, WM_GETFONT, 0, 0));
        HFONT oldFont = hFont ? reinterpret_cast<HFONT>(SelectObject(dcTmp, hFont)) : nullptr;
        TEXTMETRICW tm{};
        GetTextMetricsW(dcTmp, &tm);
        lineHeight = tm.tmHeight + tm.tmExternalLeading;
        if (oldFont)
            SelectObject(dcTmp, oldFont);
        ReleaseDC(hwnd, dcTmp);
        if (lineHeight < 6)
            lineHeight = 16;
    }

    HDC hdc = GetDC(hwnd);
    if (!hdc)
        return;
    // Clip strictly to the editor's client area. GetDC already does this
    // by default, but be paranoid: any stray draw at a y outside [0,
    // rcClient.bottom] would otherwise risk bleeding into the parent
    // (menu bar above, status bar below) on certain DWM compositing
    // paths.
    IntersectClipRect(hdc, rcClient.left, rcClient.top, rcClient.right, rcClient.bottom);

    HPEN pen = CreatePen(PS_SOLID, 1, RGB(220, 50, 50));
    HPEN oldPen = reinterpret_cast<HPEN>(SelectObject(hdc, pen));

    for (const auto &e : errors)
    {
        int eEnd = e.start + e.length;
        // Skip errors entirely above the visible char range — their
        // EM_POSFROMCHAR may return stale / unlaid-out coords.
        if (eEnd <= firstVisChar)
            continue;
        // Past end of document → ignore (shouldn't happen normally).
        if (e.start > totalChars)
            break;

        POINTL ptStart{};
        SendMessageW(hwnd, EM_POSFROMCHAR, reinterpret_cast<WPARAM>(&ptStart), e.start);
        // Past the visible bottom — errors are in char order, so done.
        if (ptStart.y > rcClient.bottom)
            break;
        if (ptStart.y + lineHeight < 0)
            continue;

        POINTL ptEnd{};
        SendMessageW(hwnd, EM_POSFROMCHAR, reinterpret_cast<WPARAM>(&ptEnd), eEnd);
        // Skip multi-line errors (a spell-check error is a single word,
        // which essentially never wraps).
        if (ptEnd.y != ptStart.y)
            continue;
        // Sanity: x range must make sense.
        if (ptEnd.x <= ptStart.x)
            continue;

        int waveY = ptStart.y + lineHeight - 2;
        if (waveY < rcClient.top || waveY > rcClient.bottom)
            continue;
        DrawSpellWave(hdc, ptStart.x, ptEnd.x, waveY);
    }

    SelectObject(hdc, oldPen);
    DeleteObject(pen);
    ReleaseDC(hwnd, hdc);
}

// Shared with the highlight overlays below. Samples the rendered line
// height from two adjacent visible lines (EM_POSFROMCHAR is only reliable
// for laid-out — i.e. visible — char positions), falling back to the font
// metrics. Same technique DrawSpellErrors uses inline.
static int EditorLineHeight(HWND hwnd)
{
    int totalLines = static_cast<int>(SendMessageW(hwnd, EM_GETLINECOUNT, 0, 0));
    int firstVisLine = static_cast<int>(SendMessageW(hwnd, EM_GETFIRSTVISIBLELINE, 0, 0));
    int firstVisChar = static_cast<int>(SendMessageW(hwnd, EM_LINEINDEX, firstVisLine, 0));
    if (firstVisChar < 0)
        firstVisChar = 0;
    int lineHeight = 0;
    if (firstVisLine + 1 < totalLines)
    {
        POINTL p0{}, p1{};
        int line1Char = static_cast<int>(SendMessageW(hwnd, EM_LINEINDEX, firstVisLine + 1, 0));
        SendMessageW(hwnd, EM_POSFROMCHAR, reinterpret_cast<WPARAM>(&p0), firstVisChar);
        SendMessageW(hwnd, EM_POSFROMCHAR, reinterpret_cast<WPARAM>(&p1), line1Char);
        lineHeight = p1.y - p0.y;
    }
    if (lineHeight < 6 || lineHeight > 200)
    {
        // Fallback for a single-line document (no adjacent line to sample).
        // Measure the editor's ACTUAL font via g_state.hFont, not WM_GETFONT:
        // msftedit manages its font through CHARFORMAT and returns NULL for
        // WM_GETFONT (verified), so the old path measured the DC's default
        // stock font instead of Consolas and came out ~2px short of RichEdit's
        // real line height — the current-line highlight then looked shorter
        // than the text selection on a single-line file.
        HDC dc = GetDC(hwnd);
        HFONT f = g_state.hFont ? g_state.hFont
                                : reinterpret_cast<HFONT>(SendMessageW(hwnd, WM_GETFONT, 0, 0));
        HFONT of = f ? reinterpret_cast<HFONT>(SelectObject(dc, f)) : nullptr;
        TEXTMETRICW tm{};
        GetTextMetricsW(dc, &tm);
        lineHeight = tm.tmHeight + tm.tmExternalLeading;
        if (of)
            SelectObject(dc, of);
        ReleaseDC(hwnd, dc);
        if (lineHeight < 6)
            lineHeight = 16;
    }
    return lineHeight;
}

// Blend a solid colour over rc using a caller-prepared 1x1 32-bit DIB
// (memDC has `bmp` selected; `bits` is its pixel). Reusing one DIB across
// every fill in a paint — instead of creating/destroying a DC + DIB section
// per match — is what keeps wheel-scrolling smooth when a common word has
// many matches on screen. Safe against cumulative darkening because the
// caller clips the destination DC to the WM_PAINT update region.
static void FillRectAlpha(HDC dst, HDC memDC, void *bits, const RECT &rc, COLORREF color, BYTE alpha)
{
    int w = rc.right - rc.left, h = rc.bottom - rc.top;
    if (w <= 0 || h <= 0 || !bits)
        return;
    BYTE *px = static_cast<BYTE *>(bits);
    px[0] = GetBValue(color);
    px[1] = GetGValue(color);
    px[2] = GetRValue(color);
    px[3] = 0; // no per-pixel alpha; SourceConstantAlpha does the blend
    BLENDFUNCTION bf{AC_SRC_OVER, 0, alpha, 0};
    AlphaBlend(dst, rc.left, rc.top, w, h, memDC, 0, 0, 1, 1, bf);
}

// Optional "highlight current line" overlay (Edit > Settings, off by
// default). Fills the caret's line with a faint translucent wash. The
// caller (WM_PAINT) passes a DC already clipped to the paint update region,
// which is what makes the alpha fill safe from cumulative darkening.
static void DrawCurrentLineHighlight(HWND hwnd, HDC hdc, HDC memDC, void *bits)
{
    RECT rcClient;
    GetClientRect(hwnd, &rcClient);
    DWORD a = 0, b = 0;
    SendMessageW(hwnd, EM_GETSEL, reinterpret_cast<WPARAM>(&a), reinterpret_cast<LPARAM>(&b));
    int caretLine = static_cast<int>(SendMessageW(hwnd, EM_EXLINEFROMCHAR, 0, static_cast<LPARAM>(a)));
    int firstVisLine = static_cast<int>(SendMessageW(hwnd, EM_GETFIRSTVISIBLELINE, 0, 0));
    if (caretLine < firstVisLine)
        return;
    int lineStart = static_cast<int>(SendMessageW(hwnd, EM_LINEINDEX, caretLine, 0));
    if (lineStart < 0)
        return;
    POINTL pt{};
    SendMessageW(hwnd, EM_POSFROMCHAR, reinterpret_cast<WPARAM>(&pt), lineStart);
    int lh = EditorLineHeight(hwnd);
    if (pt.y >= rcClient.bottom || pt.y + lh <= rcClient.top)
        return;

    COLORREF c = IsMatrixTheme() ? RGB(0, 255, 90)
                 : IsDarkMode()  ? RGB(90, 150, 235)
                                 : RGB(0, 120, 215);
    BYTE alpha = IsMatrixTheme() ? 26 : IsDarkMode() ? 44 : 28;
    RECT rc = {rcClient.left, pt.y, rcClient.right, pt.y + lh};
    FillRectAlpha(hdc, memDC, bits, rc, c, alpha);
}

// Optional "highlight matches" overlay (Edit > Settings, off by default).
// Fills every visible occurrence of the current selection with a faint
// translucent wash. DC is pre-clipped to the paint update region by the
// caller, so the alpha fills don't accumulate across partial repaints.
static void DrawOccurrenceHighlights(HWND hwnd, HDC hdc, HDC memDC, void *bits)
{
    DWORD selA = 0, selB = 0;
    SendMessageW(hwnd, EM_GETSEL, reinterpret_cast<WPARAM>(&selA), reinterpret_cast<LPARAM>(&selB));
    int selLen = static_cast<int>(selB) - static_cast<int>(selA);
    if (selLen <= 0 || selLen > 128)
        return; // nothing selected, or selection too long to be a "word"

    std::vector<wchar_t> buf(static_cast<size_t>(selLen) + 1, 0);
    TEXTRANGEW tr;
    tr.chrg.cpMin = static_cast<LONG>(selA);
    tr.chrg.cpMax = static_cast<LONG>(selB);
    tr.lpstrText = buf.data();
    SendMessageW(hwnd, EM_GETTEXTRANGE, 0, reinterpret_cast<LPARAM>(&tr));
    std::wstring needle(buf.data());
    if (needle.empty())
        return;
    for (wchar_t ch : needle)
        if (ch == L'\r' || ch == L'\n' || ch == L'\t')
            return; // only highlight single-line, tab-free selections

    RECT rcClient;
    GetClientRect(hwnd, &rcClient);
    int lh = EditorLineHeight(hwnd);
    int firstVisLine = static_cast<int>(SendMessageW(hwnd, EM_GETFIRSTVISIBLELINE, 0, 0));
    int firstVisChar = static_cast<int>(SendMessageW(hwnd, EM_LINEINDEX, firstVisLine, 0));
    if (firstVisChar < 0)
        firstVisChar = 0;

    COLORREF c = IsMatrixTheme() ? RGB(0, 255, 90)
                 : IsDarkMode()  ? RGB(60, 200, 60)
                                 : RGB(0, 190, 0);
    BYTE alpha = IsMatrixTheme() ? 40 : IsDarkMode() ? 60 : 52;

    // Bound the search to the visible char range. Searching to the end of
    // the document (cpMax = -1) meant that whenever no further match existed
    // below the fold, EM_FINDTEXTEXW scanned the entire rest of the file —
    // on every WM_PAINT, three times per wheel notch. That was the sluggish
    // scrolling + busy-cursor flash. EM_CHARFROMPOS at the bottom-right
    // gives the last visible character; a small margin catches a match
    // straddling the fold.
    int totalChars = static_cast<int>(SendMessageW(hwnd, WM_GETTEXTLENGTH, 0, 0));
    POINT ptBottom = {rcClient.right, rcClient.bottom};
    int lastVisChar = static_cast<int>(SendMessageW(hwnd, EM_CHARFROMPOS, 0, reinterpret_cast<LPARAM>(&ptBottom)));
    if (lastVisChar < firstVisChar || lastVisChar > totalChars)
        lastVisChar = totalChars;

    FINDTEXTEXW ft{};
    ft.lpstrText = needle.c_str();
    ft.chrg.cpMin = firstVisChar;
    ft.chrg.cpMax = lastVisChar + selLen + 1;
    int guard = 0;
    while (guard++ < 2000)
    {
        LONG found = static_cast<LONG>(SendMessageW(hwnd, EM_FINDTEXTEXW, FR_DOWN, reinterpret_cast<LPARAM>(&ft)));
        if (found < 0)
            break;
        int mStart = ft.chrgText.cpMin;
        int mEnd = ft.chrgText.cpMax;
        if (mEnd <= mStart)
            break;
        ft.chrg.cpMin = mEnd; // continue past this match next iteration

        // Skip the active selection itself — RichEdit already paints it
        // with the selection colour.
        if (!(mStart == static_cast<int>(selA) && mEnd == static_cast<int>(selB)))
        {
            POINTL p0{}, p1{};
            SendMessageW(hwnd, EM_POSFROMCHAR, reinterpret_cast<WPARAM>(&p0), mStart);
            if (p0.y > rcClient.bottom)
                break; // matches come in order — nothing more is visible
            SendMessageW(hwnd, EM_POSFROMCHAR, reinterpret_cast<WPARAM>(&p1), mEnd);
            if (p1.y == p0.y && p1.x > p0.x && p0.y + lh > rcClient.top)
            {
                RECT rc = {p0.x, p0.y, p1.x, p0.y + lh};
                FillRectAlpha(hdc, memDC, bits, rc, c, alpha);
            }
        }
    }
}

// Called from the main window's EN_SELCHANGE. Repaints only what the
// highlight overlays need: a selection change relevant to "highlight
// matches" forces a full repaint (matches can be anywhere); otherwise just
// the old and new caret-line bands are invalidated so "highlight current
// line" moves without repainting the whole editor on every arrow key.
void OnEditorSelChangeHighlights()
{
    if (!g_state.highlightCurrentLine && !g_state.highlightOccurrences)
        return;
    HWND hwnd = g_hwndEditor;
    if (!hwnd)
        return;
    static int s_lastLine = -1, s_lastSelStart = -1, s_lastSelLen = -1;
    DWORD a = 0, b = 0;
    SendMessageW(hwnd, EM_GETSEL, reinterpret_cast<WPARAM>(&a), reinterpret_cast<LPARAM>(&b));
    int line = static_cast<int>(SendMessageW(hwnd, EM_EXLINEFROMCHAR, 0, static_cast<LPARAM>(a)));
    int selLen = static_cast<int>(b) - static_cast<int>(a);

    bool occChanged = g_state.highlightOccurrences &&
                      (static_cast<int>(a) != s_lastSelStart || selLen != s_lastSelLen) &&
                      (selLen > 0 || s_lastSelLen > 0);

    if (occChanged)
    {
        InvalidateRect(hwnd, nullptr, TRUE); // matches may be anywhere on screen
    }
    else if (g_state.highlightCurrentLine && line != s_lastLine)
    {
        int lh = EditorLineHeight(hwnd);
        RECT rc;
        GetClientRect(hwnd, &rc);
        auto invalLine = [&](int ln) {
            if (ln < 0)
                return;
            int ls = static_cast<int>(SendMessageW(hwnd, EM_LINEINDEX, ln, 0));
            if (ls < 0)
                return;
            POINTL pt{};
            SendMessageW(hwnd, EM_POSFROMCHAR, reinterpret_cast<WPARAM>(&pt), ls);
            if (pt.y > rc.bottom || pt.y + lh < 0)
                return;
            RECT band = {rc.left, pt.y - 1, rc.right, pt.y + lh + 1};
            InvalidateRect(hwnd, &band, TRUE);
        };
        invalLine(s_lastLine);
        invalLine(line);
    }

    s_lastLine = line;
    s_lastSelStart = static_cast<int>(a);
    s_lastSelLen = selLen;
}

LRESULT CALLBACK EditorSubclassProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
    case WM_PAINT:
    {
        // Capture the update region BEFORE RichEdit validates it, so the
        // alpha-fill highlights can be clipped to only the freshly-painted
        // pixels (otherwise the blend would stack on un-erased ones and
        // darken on every partial repaint / scroll).
        bool wantFill = g_state.highlightCurrentLine || g_state.highlightOccurrences;
        HRGN updRgn = nullptr;
        if (wantFill)
        {
            updRgn = CreateRectRgn(0, 0, 0, 0);
            if (GetUpdateRgn(hwnd, updRgn, FALSE) == ERROR)
            {
                DeleteObject(updRgn);
                updRgn = nullptr;
            }
        }
        LRESULT result = CallWindowProcW(g_origEditorProc, hwnd, msg, wParam, lParam);
        // Skip overlay drawing while user is drag-selecting — extra GDI
        // operations on the same DC interfere with RichEdit's selection
        // tracking and cause the selection to release mid-drag. (A finished
        // click/drag re-shows the fills via the WM_LBUTTONUP invalidate.)
        bool dragging = g_editorDragSelecting;
        if (wantFill && !dragging)
        {
            HDC hdc = GetDC(hwnd);
            if (hdc)
            {
                RECT rcClient;
                GetClientRect(hwnd, &rcClient);
                IntersectClipRect(hdc, rcClient.left, rcClient.top, rcClient.right, rcClient.bottom);
                if (updRgn)
                    ExtSelectClipRgn(hdc, updRgn, RGN_AND);
                // One reusable 1x1 blend bitmap for every fill this paint.
                HDC memDC = CreateCompatibleDC(hdc);
                void *bits = nullptr;
                HBITMAP bmp = nullptr;
                HBITMAP oldBmp = nullptr;
                if (memDC)
                {
                    BITMAPINFO bi{};
                    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
                    bi.bmiHeader.biWidth = 1;
                    bi.bmiHeader.biHeight = 1;
                    bi.bmiHeader.biPlanes = 1;
                    bi.bmiHeader.biBitCount = 32;
                    bi.bmiHeader.biCompression = BI_RGB;
                    bmp = CreateDIBSection(memDC, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
                    if (bmp)
                        oldBmp = static_cast<HBITMAP>(SelectObject(memDC, bmp));
                }
                if (memDC && bmp && bits)
                {
                    if (g_state.highlightCurrentLine)
                        DrawCurrentLineHighlight(hwnd, hdc, memDC, bits);
                    if (g_state.highlightOccurrences)
                        DrawOccurrenceHighlights(hwnd, hdc, memDC, bits);
                }
                if (bmp)
                {
                    // Deselect first: DeleteObject silently fails on a bitmap
                    // still selected into a DC, leaking one handle per paint.
                    SelectObject(memDC, oldBmp);
                    DeleteObject(bmp);
                }
                if (memDC)
                    DeleteDC(memDC);
                ReleaseDC(hwnd, hdc);
            }
        }
        if (updRgn)
            DeleteObject(updRgn);
        if (g_state.showSpecialChars && !dragging)
            DrawSpecialCharMarkers(hwnd);
        if (g_state.spellCheckEnabled && !dragging)
            DrawSpellErrors(hwnd);
        if (g_state.showLineNumbers && g_hwndGutter)
            InvalidateRect(g_hwndGutter, nullptr, FALSE);
        return result;
    }
    case WM_KILLFOCUS:
    {
        // Capture the user's viewport while it's still intact — before
        // deactivation and before the next focus's scroll-caret pass.
        // Line-based (EM_GETFIRSTVISIBLELINE) survives RichEdit's layout
        // passes; a pixel-based EM_GETSCROLLPOS snapshot does not.
        g_pinnedFirstLine = static_cast<int>(SendMessageW(hwnd, EM_GETFIRSTVISIBLELINE, 0, 0));
        DWORD pinSelStart = 0, pinSelEnd = 0;
        SendMessageW(hwnd, EM_GETSEL, reinterpret_cast<WPARAM>(&pinSelStart), reinterpret_cast<LPARAM>(&pinSelEnd));
        g_pinnedCaret = static_cast<int>(pinSelStart);
        g_editorDragSelecting = false; // a drag can't be in progress without focus
        break; // let the default handler run too
    }
    case WM_SETFOCUS:
    {
        LRESULT result = CallWindowProcW(g_origEditorProc, hwnd, msg, wParam, lParam);
        // Three restore attempts at increasing delays — none disarm the
        // pin except the timer, so whichever one runs after RichEdit's
        // actual scroll wins:
        //   1) synchronous, here (catches an immediate scroll, no flicker)
        //   2) posted message (catches a message-queued scroll)
        //   3) timer (catches a paint/timer-deferred scroll; disarms)
        if (g_pinnedFirstLine >= 0)
        {
            RestorePinnedScroll(hwnd);
            PostMessageW(hwnd, WM_RESTORE_SCROLL, 0, 0);
            SetTimer(hwnd, RESTORE_SCROLL_TIMER_ID, RESTORE_SCROLL_DELAY_MS, nullptr);
        }
        return result;
    }
    case WM_RESTORE_SCROLL:
        RestorePinnedScroll(hwnd);
        return 0;
    case WM_TIMER:
        if (wParam == RESTORE_SCROLL_TIMER_ID)
        {
            KillTimer(hwnd, RESTORE_SCROLL_TIMER_ID);
            RestorePinnedScroll(hwnd);
            g_pinnedFirstLine = -1; // disarm — restore cycle complete
            return 0;
        }
        break;
    case WM_LBUTTONDOWN:
        // Mark the start of an in-editor text drag-select so the overlay
        // guard suppresses fills only for real selections (not window resize).
        g_editorDragSelecting = true;
        break; // let RichEdit run its normal selection handling
    case WM_SIZE:
    {
        LRESULT result = CallWindowProcW(g_origEditorProc, hwnd, msg, wParam, lParam);
        // RichEdit relays out and repaints on resize, but nothing guarantees
        // the caret-line / match fills are redrawn (and the old surgical
        // invalidation only runs on selection change). Force a clean repaint
        // so the current-line highlight survives enlarging / maximizing the
        // window instead of vanishing until the next caret move.
        if (g_state.highlightCurrentLine || g_state.highlightOccurrences)
            InvalidateRect(hwnd, nullptr, FALSE);
        return result;
    }
    case WM_LBUTTONUP:
    {
        g_editorDragSelecting = false;
        LRESULT result = CallWindowProcW(g_origEditorProc, hwnd, msg, wParam, lParam);
        if (g_state.showSpecialChars)
            InvalidateRect(hwnd, nullptr, FALSE);
        // The highlight fills are skipped mid-gesture (the WM_PAINT dragging
        // guard). Now the click / drag / double-click is finished, repaint so
        // they appear for the new caret line and selection — this is what
        // makes mouse selection and double-click-to-highlight work.
        if (g_state.highlightCurrentLine || g_state.highlightOccurrences)
            InvalidateRect(hwnd, nullptr, TRUE);
        return result;
    }
    case WM_LBUTTONDBLCLK:
    {
        // On an empty document there is no word to select — RichEdit's
        // word-select instead grabs the mandatory trailing paragraph mark and
        // draws a stray selection sliver (EM_GETSEL still reports 0,0, so the
        // collapse below never catches it). Skip RichEdit's double-click
        // handling entirely and just keep the caret at the start, matching the
        // classic EDIT control which selects nothing on empty.
        if (SendMessageW(hwnd, WM_GETTEXTLENGTH, 0, 0) == 0)
        {
            SendMessageW(hwnd, EM_SETSEL, 0, 0);
            return 0;
        }
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
    case WM_VSCROLL:
    case WM_HSCROLL:
    {
        // RichEdit scrolls by blitting and repainting only the newly
        // exposed strip. The translucent highlight fills can't survive that
        // (the overlay is clipped to the paint update region, so the blitted
        // fills in the middle aren't re-laid), so force a clean full repaint
        // when a highlight is on. The visible-range-bounded match search
        // above keeps that cheap. Wheel sends several of these per notch but
        // the InvalidateRect calls coalesce into a single WM_PAINT.
        LRESULT result = CallWindowProcW(g_origEditorProc, hwnd, msg, wParam, lParam);
        if (g_state.highlightCurrentLine || g_state.highlightOccurrences)
            InvalidateRect(hwnd, nullptr, FALSE);
        return result;
    }
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
            // One EM_LINESCROLL instead of N separate WM_VSCROLL line steps.
            // Each WM_VSCROLL makes RichEdit blit + repaint (and run the
            // overlay), so N per notch meant N paints — the wheel felt much
            // heavier than dragging the scrollbar, which is a single repaint.
            // Positive lParam scrolls down. Then one invalidate so the
            // translucent highlights re-lay cleanly (see WM_VSCROLL handler).
            int lines = static_cast<int>(scrollLines);
            SendMessageW(hwnd, EM_LINESCROLL, 0, (delta > 0) ? -lines : lines);
            // EM_LINESCROLL lets msftedit overshoot the true bottom by up to
            // ~1 line, exposing the trailing phantom line as a blank gap below
            // the last text — the scrollbar thumb clamps exactly, the wheel
            // didn't (measured: nPos 10408 vs the bottom's 10407). When
            // scrolling down, snap to the exact bottom if we overshot. The
            // vertical scrollbar is in pixels; the last valid position is
            // nMax - nPage (verified against WM_VSCROLL SB_BOTTOM).
            if (delta < 0)
            {
                SCROLLINFO si{};
                si.cbSize = sizeof(si);
                si.fMask = SIF_RANGE | SIF_PAGE | SIF_POS;
                if (GetScrollInfo(hwnd, SB_VERT, &si) &&
                    si.nPos > si.nMax - static_cast<int>(si.nPage))
                    SendMessageW(hwnd, WM_VSCROLL, SB_BOTTOM, 0);
            }
            if (g_state.highlightCurrentLine || g_state.highlightOccurrences)
                InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;
    }
    case WM_MOUSEHWHEEL:
    {
        int delta = GET_WHEEL_DELTA_WPARAM(wParam);
        UINT scrollChars = 3;
        SystemParametersInfoW(SPI_GETWHEELSCROLLCHARS, 0, &scrollChars, 0);
        // WHEEL_PAGESCROLL (UINT_MAX) or a bogus driver value would spin the
        // loop below for billions of synchronous sends — hang.
        if (scrollChars > 100)
            scrollChars = 3;
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
            AppendMenuW(hTools, MF_STRING, IDM_TOOLS_BASE64_DECODE, lang.menuToolsBase64Decode.c_str());
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
            AppendMenuW(hTools, MF_STRING, IDM_TOOLS_REMOVEEMPTY, lang.menuToolsRemoveEmpty.c_str());
            AppendMenuW(hTools, MF_STRING, IDM_TOOLS_REMOVEDUPES, lang.menuToolsRemoveDupes.c_str());
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
