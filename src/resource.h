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

  Resource identifiers for menus, accelerators, icons, and control IDs.
*/

#pragma once

#define IDR_MAINMENU 101
#define IDR_ACCEL 102
#define IDI_NOTEPAD 103

// Human-readable application version, shown in the About dialog. Keep in
// sync with the VERSION in CMakeLists.txt and app.manifest.
#define APP_VERSION L"0.9.5"

#define IDC_EDITOR 1000
#define IDC_STATUSBAR 1001
#define IDC_SNIPPETS 1002

#define IDM_FILE_NEW 40001
#define IDM_FILE_OPEN 40002
#define IDM_FILE_SAVE 40003
#define IDM_FILE_SAVEAS 40004
#define IDM_FILE_PRINT 40005
#define IDM_FILE_PAGESETUP 40006
#define IDM_FILE_EXIT 40007
#define IDM_FILE_OPENFOLDER 40008
#define IDM_FILE_COPYPATH 40009

#define IDM_FILE_RECENT_BASE 40100

#define IDM_EDIT_UNDO 40010
#define IDM_EDIT_CUT 40011
#define IDM_EDIT_COPY 40012
#define IDM_EDIT_PASTE 40013
#define IDM_EDIT_DELETE 40014
#define IDM_EDIT_FIND 40015
#define IDM_EDIT_FINDNEXT 40016
#define IDM_EDIT_REPLACE 40017
#define IDM_EDIT_GOTO 40018
#define IDM_EDIT_SELECTALL 40019
#define IDM_EDIT_TIMEDATE 40020
#define IDM_EDIT_REDO 40021
#define IDM_EDIT_FINDPREV 40022
#define IDM_EDIT_DUPLICATELINE 40028
#define IDM_EDIT_DELETELINE 40029

#define IDM_FORMAT_WORDWRAP 40030
#define IDM_FORMAT_FONT 40031

#define IDM_VIEW_ZOOMIN 40040
#define IDM_VIEW_ZOOMOUT 40041
#define IDM_VIEW_ZOOMDEFAULT 40042
#define IDM_VIEW_STATUSBAR 40043
#define IDM_VIEW_TRANSPARENCY 40045
#define IDM_VIEW_ALWAYSONTOP 40046
#define IDM_VIEW_SHOWSPECIAL 40047
#define IDM_VIEW_LINENUMBERS 40048

// Theme radio items in the View menu (Light / Dark / Matrix).
#define IDM_VIEW_THEME_LIGHT 40050
#define IDM_VIEW_THEME_DARK 40051
#define IDM_VIEW_THEME_MATRIX 40052

#define IDM_HELP_ABOUT 40080

#define IDM_VIEW_LANG_EN 40090
#define IDM_VIEW_LANG_JA 40091
#define IDM_VIEW_LANG_PL 40092
#define IDM_VIEW_LANG_DE 40093
#define IDM_VIEW_LANG_CS 40094
#define IDM_VIEW_LANG_UK 40095
#define IDM_VIEW_LANG_LT 40096
#define IDM_VIEW_LANG_RU 40097

#define IDM_EDIT_SETTINGS 40023
#define IDM_EDIT_SETTINGS_DATEFORMAT 40024
#define IDM_EDIT_SETTINGS_SPELLCHECK 40025
#define IDM_EDIT_SETTINGS_TOOLS 40026
#define IDM_EDIT_SETTINGS_QUICKICONS 40027
#define IDM_EDIT_SETTINGS_HIGHLIGHTLINE 40058
#define IDM_EDIT_SETTINGS_HIGHLIGHTWORD 40059

// Format > Line Endings (radio) and Format > Encoding (radio).
#define IDM_FORMAT_LE_CRLF 40060
#define IDM_FORMAT_LE_LF 40061
#define IDM_FORMAT_LE_CR 40062
#define IDM_FORMAT_ENC_UTF8 40065
#define IDM_FORMAT_ENC_UTF8BOM 40066
#define IDM_FORMAT_ENC_UTF16LE 40067
#define IDM_FORMAT_ENC_UTF16BE 40068
#define IDM_FORMAT_ENC_ANSI 40069

#define IDM_TOOLS_NORMALIZE 40200
#define IDM_TOOLS_BASE64 40201
#define IDM_TOOLS_SHA1 40202
#define IDM_TOOLS_MD5 40203
#define IDM_TOOLS_UPPERCASE 40204
#define IDM_TOOLS_LOWERCASE 40205
#define IDM_TOOLS_TITLECASE 40206
#define IDM_TOOLS_TRIMTRAILING 40207
#define IDM_TOOLS_TABS2SPACES 40208
#define IDM_TOOLS_SPACES2TABS 40209
#define IDM_TOOLS_REVERSELINES 40210
#define IDM_TOOLS_JOINLINES 40211
#define IDM_TOOLS_REMOVEEMPTY 40212
#define IDM_TOOLS_REMOVEDUPES 40213
#define IDM_TOOLS_BASE64_DECODE 40214
#define IDM_TOOLS_SORTASC 40215
#define IDM_TOOLS_SORTDESC 40216
#define IDM_TOOLS_URLENCODE 40217
#define IDM_TOOLS_URLDECODE 40218

#define IDM_QUICK_SPELLCHECK 40300
#define IDM_QUICK_ONTOP 40301
#define IDM_QUICK_DARKMODE 40302
#define IDM_QUICK_INSERTCHAR 40303
#define IDM_QUICK_SNIPPETS 40304

// Special-character insert commands — the list popped up from the
// quick-access "insert symbol" icon. Consecutive; order matches the popup.
#define IDM_INSCHAR_FIRST 40320
#define IDM_INSCHAR_EURO 40320
#define IDM_INSCHAR_POUND 40321
#define IDM_INSCHAR_COPYRIGHT 40322
#define IDM_INSCHAR_REGISTERED 40323
#define IDM_INSCHAR_TRADEMARK 40324
#define IDM_INSCHAR_SECTION 40325
#define IDM_INSCHAR_DEGREE 40326
#define IDM_INSCHAR_BULLET 40327
#define IDM_INSCHAR_MIDDLEDOT 40328
#define IDM_INSCHAR_LAST 40328

// Synthetic IDs for the top-level menu bar popups (Plik / Edycja / ...)
// when they're converted to MFT_OWNERDRAW for dark-mode painting.
// Position-based: IDM_TOPLEVEL_BASE + index_in_menu_bar.
#define IDM_TOPLEVEL_BASE 50000
#define IDM_TOPLEVEL_MAX  50031
