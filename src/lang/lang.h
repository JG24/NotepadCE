#pragma once

#include <windows.h>
#include <string>

enum class LangID
{
    EN,
    JA,
    PL
};

struct LangStrings
{
    std::wstring appName;
    std::wstring untitled;

    std::wstring menuFile;
    std::wstring menuNew;
    std::wstring menuOpen;
    std::wstring menuSave;
    std::wstring menuSaveAs;
    std::wstring menuPrint;
    std::wstring menuPageSetup;
    std::wstring menuExit;
    std::wstring menuRecentFiles;

    std::wstring menuEdit;
    std::wstring menuUndo;
    std::wstring menuRedo;
    std::wstring menuCut;
    std::wstring menuCopy;
    std::wstring menuPaste;
    std::wstring menuDelete;
    std::wstring menuFind;
    std::wstring menuFindNext;
    std::wstring menuFindPrev;
    std::wstring menuReplace;
    std::wstring menuGoTo;
    std::wstring menuSelectAll;
    std::wstring menuTimeDate;

    std::wstring menuFormat;
    std::wstring menuWordWrap;
    std::wstring menuFont;

    std::wstring menuView;
    std::wstring menuZoomIn;
    std::wstring menuZoomOut;
    std::wstring menuZoomDefault;
    std::wstring menuStatusBar;
    std::wstring menuDarkMode;
    std::wstring menuShowSpecial;
    std::wstring menuLineNumbers;
    std::wstring menuTransparency;
    std::wstring menuAlwaysOnTop;

    std::wstring menuSettings;
    std::wstring menuSettingsDateFormat;
    std::wstring menuSettingsSpellCheck;
    std::wstring menuSettingsTools;
    std::wstring menuSettingsQuickIcons;

    std::wstring menuTools;
    std::wstring menuToolsNormalize;
    std::wstring menuToolsBase64;
    std::wstring menuToolsSha1;
    std::wstring menuToolsMd5;

    std::wstring menuHelp;
    std::wstring menuAbout;

    std::wstring menuLanguage;
    std::wstring menuLangEnglish;
    std::wstring menuLangJapanese;
    std::wstring menuLangPolish;

    std::wstring dialogFind;
    std::wstring dialogFindReplace;
    std::wstring dialogGoTo;
    std::wstring dialogTransparency;
    std::wstring dialogFindLabel;
    std::wstring dialogReplaceLabel;
    std::wstring dialogFindNext;
    std::wstring dialogReplace;
    std::wstring dialogReplaceAll;
    std::wstring dialogClose;
    std::wstring dialogLineNumber;
    std::wstring dialogOK;
    std::wstring dialogCancel;
    std::wstring dialogOpacityLabel;
    std::wstring dialogDateFormatTitle;
    std::wstring dialogDateFormatLabel;
    std::wstring dialogDateFormatPreview;
    std::wstring dialogDateFormatHelp;
    std::wstring dialogDateFormatRestore;

    std::wstring msgCannotFind;
    std::wstring msgSaveChanges;
    std::wstring msgCannotOpenFile;
    std::wstring msgCannotSaveFile;
    std::wstring msgError;
    std::wstring msgAbout;

    std::wstring aboutTitle;
    std::wstring aboutTagline;
    std::wstring aboutAuthor;
    std::wstring aboutBuiltOn;
    std::wstring aboutTech;
    std::wstring aboutOriginalAuthor;

    std::wstring statusChars;
    std::wstring statusLines;
    std::wstring statusLine;
    std::wstring statusColumn;
    std::wstring statusSelected;

    std::wstring encodingUTF8;
    std::wstring encodingUTF8BOM;
    std::wstring encodingUTF16LE;
    std::wstring encodingUTF16BE;
    std::wstring encodingANSI;

    std::wstring lineEndingCRLF;
    std::wstring lineEndingLF;
    std::wstring lineEndingCR;
};

void InitLanguage();
void SetLanguage(LangID lang);
void SaveLanguageSetting();
LangID LoadLanguageSetting();
LangID GetCurrentLanguage();
const LangStrings &GetLangStrings();
const std::wstring &GetString(const std::wstring &key);
