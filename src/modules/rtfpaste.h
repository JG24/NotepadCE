#pragma once

#include <string>

// Normalize CF_UNICODETEXT that came from a rich-text source (Word,
// Outlook, OneNote, WordPad ...). We rely on CF_UNICODETEXT for the
// actual character data — it's authoritative UTF-16 from Windows, so
// Polish letters, fractions, smart quotes etc. are correct without
// codepage gymnastics. We detect list items heuristically:
//   * lines starting with a common bullet glyph + whitespace
//   * lines starting with a number+`.` or `)` + whitespace
// List markers become "- ", list items stay tight (single \r\n between),
// non-list paragraphs get a blank line between them.
//
// Soft line breaks from Word's Shift+Enter (vertical tab in CF_UNICODETEXT)
// are folded into a regular line break.
std::wstring NormalizeRichPaste(const std::wstring &text);
