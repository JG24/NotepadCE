#pragma once

#include <windows.h>
#include <string>
#include <cwchar>

enum class LangID
{
    EN,
    JA,
    PL,
    DE,
    CS,
    UK,
    LT,
    RU,
    ZH
};

// Lightweight, non-owning, NUL-terminated wide-string handle. Holds only a
// pointer to a string literal, so the nine LangStrings tables below become
// pure constant data — no per-field std::wstring construction at startup
// (that was ~70 KB of init code across all languages). Exposes just the slice
// of the std::wstring interface the call sites use: implicit const wchar_t*
// conversion, c_str(), size(), empty().
struct LStr
{
    const wchar_t *s;
    constexpr LStr(const wchar_t *p = L"") : s(p) {}
    constexpr operator const wchar_t *() const { return s; }
    const wchar_t *c_str() const { return s; }
    size_t size() const { return wcslen(s); }
    bool empty() const { return s[0] == L'\0'; }
};

// std::operator+ are templates, so the implicit LStr -> const wchar_t*
// conversion can't kick in for them (template deduction ignores user
// conversions). These non-template overloads let every existing
// `lang.field + ...` concatenation keep compiling unchanged.
inline std::wstring operator+(LStr a, const wchar_t *b) { return std::wstring(a.s) + b; }
inline std::wstring operator+(const wchar_t *a, LStr b) { return a + std::wstring(b.s); }
inline std::wstring operator+(LStr a, const std::wstring &b) { return a.s + b; }
inline std::wstring operator+(const std::wstring &a, LStr b) { return a + b.s; }
inline std::wstring operator+(LStr a, LStr b) { return std::wstring(a.s) + b.s; }

struct LangStrings
{
    LStr appName;
    LStr untitled;

    LStr menuFile;
    LStr menuNew;
    LStr menuOpen;
    LStr menuSave;
    LStr menuSaveAs;
    LStr menuPrint;
    LStr menuPageSetup;
    LStr menuExit;
    LStr menuRecentFiles;

    LStr menuEdit;
    LStr menuUndo;
    LStr menuRedo;
    LStr menuCut;
    LStr menuCopy;
    LStr menuPaste;
    LStr menuDelete;
    LStr menuFind;
    LStr menuFindNext;
    LStr menuFindPrev;
    LStr menuReplace;
    LStr menuGoTo;
    LStr menuSelectAll;
    LStr menuTimeDate;

    LStr menuFormat;
    LStr menuWordWrap;
    LStr menuFont;

    LStr menuView;
    LStr menuZoomIn;
    LStr menuZoomOut;
    LStr menuZoomDefault;
    LStr menuStatusBar;
    LStr menuThemeLight;
    LStr menuThemeDark;
    LStr menuThemeMatrix;
    LStr menuShowSpecial;
    LStr menuLineNumbers;
    LStr menuTransparency;
    LStr menuAlwaysOnTop;

    LStr menuSettings;
    LStr menuSettingsDateFormat;
    LStr menuSettingsSpellCheck;
    LStr menuSettingsTools;
    LStr menuSettingsQuickIcons;

    LStr menuTools;
    LStr menuToolsNormalize;
    LStr menuToolsBase64;
    LStr menuToolsSha1;
    LStr menuToolsMd5;
    LStr menuToolsUppercase;
    LStr menuToolsLowercase;
    LStr menuToolsTitleCase;
    LStr menuToolsTrimTrailing;
    LStr menuToolsTabsToSpaces;
    LStr menuToolsSpacesToTabs;
    LStr menuToolsReverseLines;
    LStr menuToolsJoinLines;
    LStr menuToolsRemoveEmpty;
    LStr menuToolsRemoveDupes;

    LStr menuHelp;
    LStr menuAbout;

    // The "Language" submenu label is translated; the individual language
    // entries below it are NOT — they are shown as autonyms (each language
    // in its own name) so they stay recognisable whatever the current UI
    // language is. See the hard-coded list in UpdateMenuStrings().
    LStr menuLanguage;

    LStr dialogFind;
    LStr dialogFindReplace;
    LStr dialogGoTo;
    LStr dialogTransparency;
    LStr dialogFindLabel;
    LStr dialogReplaceLabel;
    LStr dialogFindNext;
    LStr dialogReplace;
    LStr dialogReplaceAll;
    LStr dialogClose;
    LStr dialogLineNumber;
    LStr dialogOK;
    LStr dialogCancel;
    LStr dialogOpacityLabel;
    LStr dialogDateFormatTitle;
    LStr dialogDateFormatLabel;
    LStr dialogDateFormatPreview;
    LStr dialogDateFormatHelp;
    LStr dialogDateFormatRestore;

    LStr msgCannotFind;
    LStr msgSaveChanges;
    LStr msgCannotOpenFile;
    LStr msgCannotSaveFile;
    LStr msgError;
    LStr msgAbout;

    LStr aboutTitle;
    LStr aboutTagline;
    LStr aboutAuthor;
    LStr aboutBuiltOn;
    LStr aboutTech;
    LStr aboutOriginalAuthor;
    LStr aboutBuild;

    LStr statusChars;
    LStr statusLines;
    LStr statusLine;
    LStr statusColumn;
    LStr statusSelected;

    LStr encodingUTF8;
    LStr encodingUTF8BOM;
    LStr encodingUTF16LE;
    LStr encodingUTF16BE;
    LStr encodingANSI;

    LStr lineEndingCRLF;
    LStr lineEndingLF;
    LStr lineEndingCR;

    // Hover tooltips for the right-justified quick-access menu-bar icons
    // (spell check / always on top / theme toggle). Plain text, no
    // accelerator ampersands — they are shown in a tracking tooltip.
    LStr tipQuickSpell;
    LStr tipQuickOnTop;
    LStr tipQuickTheme;
    LStr tipQuickInsertChar;

    // Names shown next to each glyph in the special-character popup
    // (opened from the quick-access "insert symbol" icon).
    LStr scEuro;
    LStr scPound;
    LStr scCopyright;
    LStr scRegistered;
    LStr scTrademark;
    LStr scSection;
    LStr scDegree;
    LStr scBullet;
    LStr scMiddleDot;

    // Edit > Settings toggles (highlight current line / all matches) and
    // Format submenus (line endings / encoding) and File menu extras.
    LStr menuSettingsHighlightLine;
    LStr menuSettingsHighlightWord;
    LStr menuFormatLineEndings;
    LStr menuFormatEncoding;
    LStr menuFileOpenFolder;
    LStr menuFileCopyPath;
    LStr menuToolsBase64Decode;

    // Snippets side panel: quick-icon tooltip, context-menu entries,
    // delete-confirmation prefix and default names for new entries.
    LStr tipQuickSnippets;
    LStr snipNewSnippet;
    LStr snipNewFolder;
    LStr snipInsert;
    LStr snipOpen;
    LStr snipRename;
    LStr snipDelete;
    LStr snipDeleteConfirm; // message: snipDeleteConfirm + name + "?"
    LStr snipDefaultName;   // new snippet file name (without extension)
    LStr snipDefaultFolder; // new folder name

    // Tools added in 0.9.5: locale-aware line sorting and URL percent-
    // encoding/decoding (the Base64 pair's sibling).
    LStr menuToolsSortAsc;
    LStr menuToolsSortDesc;
    LStr menuToolsUrlEncode;
    LStr menuToolsUrlDecode;
};

void InitLanguage();
void SetLanguage(LangID lang);
void SaveLanguageSetting();
LangID LoadLanguageSetting();
LangID GetCurrentLanguage();
const LangStrings &GetLangStrings();
const std::wstring &GetString(const std::wstring &key);
