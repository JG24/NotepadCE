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

  File I/O operations with multi-encoding support (UTF-8, UTF-16, ANSI).
  Handles BOM detection, line ending conversion, and recent files tracking.
*/

#pragma once
#include <windows.h>
#include <string>
#include <vector>
#include <utility>
#include "core/types.h"

const wchar_t *GetEncodingName(Encoding e);
const wchar_t *GetLineEndingName(LineEnding le);
std::pair<Encoding, LineEnding> DetectEncoding(const std::vector<BYTE> &data);
std::wstring DecodeText(const std::vector<BYTE> &data, Encoding enc);
std::vector<BYTE> EncodeText(const std::wstring &text, Encoding enc, LineEnding le);
void LoadFile(const std::wstring &path);
void SaveToPath(const std::wstring &path);
void AddRecentFile(const std::wstring &path);
void UpdateRecentFilesMenu();
