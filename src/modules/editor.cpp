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
#include <windowsx.h>
#include <algorithm>
#include <vector>

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
    SendMessageW(g_hwndEditor, WM_SETFONT, reinterpret_cast<WPARAM>(g_state.hFont), TRUE);
    COLORREF textColor = IsDarkMode() ? RGB(255, 255, 255) : GetSysColor(COLOR_WINDOWTEXT);
    CHARFORMAT2W cf = {};
    cf.cbSize = sizeof(cf);
    cf.dwMask = CFM_COLOR;
    cf.crTextColor = textColor;
    SendMessageW(g_hwndEditor, EM_SETCHARFORMAT, SCF_ALL, reinterpret_cast<LPARAM>(&cf));
    SendMessageW(g_hwndEditor, EM_SETCHARFORMAT, SCF_DEFAULT, reinterpret_cast<LPARAM>(&cf));
    if (g_state.showLineNumbers && g_hwndGutter && UpdateGutterWidth())
        ResizeControls();
}

void ApplyZoom()
{
    ApplyFont();
}

void ApplyWordWrap()
{
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

    // Insert with the standard replacement path so undo records it.
    SendMessageW(hwnd, EM_REPLACESEL, TRUE, reinterpret_cast<LPARAM>(text));

    DWORD newEnd = 0;
    SendMessageW(hwnd, EM_GETSEL, 0, reinterpret_cast<LPARAM>(&newEnd));

    // Force editor's default font on the just-inserted range. RichEdit's
    // own paste — and even EM_PASTESPECIAL CF_UNICODETEXT — can keep the
    // font that was active at the insertion point, which means a previous
    // RTF fragment "infects" later plain pastes. Apply CHARFORMAT2 with
    // the full mask so the inserted text matches the editor settings.
    if (newEnd > selStart)
    {
        LRESULT oldMask = SendMessageW(hwnd, EM_SETEVENTMASK, 0, 0);
        SendMessageW(hwnd, WM_SETREDRAW, FALSE, 0);

        CHARRANGE range = {static_cast<LONG>(selStart), static_cast<LONG>(newEnd)};
        SendMessageW(hwnd, EM_EXSETSEL, 0, reinterpret_cast<LPARAM>(&range));

        HDC hdc = GetDC(hwnd);
        int sizeTwips = MulDiv(g_state.fontSize * g_state.zoomLevel / 100, 1440, 72);
        ReleaseDC(hwnd, hdc);

        CHARFORMAT2W cf = {};
        cf.cbSize = sizeof(cf);
        cf.dwMask = CFM_FACE | CFM_SIZE | CFM_BOLD | CFM_ITALIC | CFM_UNDERLINE | CFM_UNDERLINETYPE | CFM_COLOR | CFM_BACKCOLOR;
        wcscpy_s(cf.szFaceName, g_state.fontName.c_str());
        cf.yHeight = sizeTwips;
        // Use AUTOBACKCOLOR so pasted text inherits the editor's bg colour
        // from EM_SETBKGNDCOLOR rather than locking in a per-char dark/light
        // colour that survives theme switches.
        cf.dwEffects = CFE_AUTOBACKCOLOR;
        if (g_state.fontWeight >= FW_BOLD)
            cf.dwEffects |= CFE_BOLD;
        if (g_state.fontItalic)
            cf.dwEffects |= CFE_ITALIC;
        if (g_state.fontUnderline)
            cf.dwEffects |= CFE_UNDERLINE;
        cf.bUnderlineType = g_state.fontUnderline ? CFU_UNDERLINE : CFU_UNDERLINENONE;
        cf.crTextColor = IsDarkMode() ? RGB(255, 255, 255) : GetSysColor(COLOR_WINDOWTEXT);
        SendMessageW(hwnd, EM_SETCHARFORMAT, SCF_SELECTION, reinterpret_cast<LPARAM>(&cf));

        // Restore caret to end of inserted region.
        SendMessageW(hwnd, EM_SETSEL, newEnd, newEnd);

        SendMessageW(hwnd, WM_SETREDRAW, TRUE, 0);
        SendMessageW(hwnd, EM_SETEVENTMASK, 0, oldMask);
        InvalidateRect(hwnd, nullptr, TRUE);
    }

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
    SetTextColor(hdc, IsDarkMode() ? RGB(110, 110, 110) : RGB(180, 180, 180));

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
    case WM_PASTE:
        PasteAsPlainText(hwnd);
        return 0;
    case WM_CHAR:
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
    case WM_MOUSEWHEEL:
    {
        if (LOWORD(wParam) & MK_SHIFT)
        {
            int delta = GET_WHEEL_DELTA_WPARAM(wParam);
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
        break;
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

        int cmd = TrackPopupMenu(hMenu, TPM_RETURNCMD | TPM_RIGHTBUTTON, pt.x, pt.y, 0, g_hwndMain, nullptr);
        DestroyMenu(hMenu);
        if (cmd)
            SendMessageW(g_hwndMain, WM_COMMAND, MAKEWPARAM(cmd, 0), 0);
        return 0;
    }
    }
    return CallWindowProcW(g_origEditorProc, hwnd, msg, wParam, lParam);
}
