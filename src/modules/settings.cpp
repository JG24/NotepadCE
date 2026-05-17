/*
  Portable JSON config — NotepadCE.json next to the executable.
  Replaces upstream's Windows Registry persistence.
*/

#include "settings.h"
#include "core/globals.h"
#include "core/types.h"
#include "lang/lang.h"
#include <windows.h>
#include <string>
#include <vector>
#include <map>
#include <cstdio>

static bool g_loaded = false;

bool IsSettingsLoaded() { return g_loaded; }

static std::wstring GetConfigPath()
{
    wchar_t path[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    wchar_t *slash = wcsrchr(path, L'\\');
    if (slash)
        *(slash + 1) = 0;
    return std::wstring(path) + L"NotepadCE.json";
}

static std::string WideToUTF8(const std::wstring &w)
{
    if (w.empty())
        return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
    std::string s(n, 0);
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()), s.data(), n, nullptr, nullptr);
    return s;
}

static std::wstring UTF8ToWide(const std::string &s)
{
    if (s.empty())
        return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring w(n, 0);
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), w.data(), n);
    return w;
}

static std::string JsonEscape(const std::wstring &w)
{
    std::string utf8 = WideToUTF8(w);
    std::string out;
    out.reserve(utf8.size() + 2);
    for (unsigned char c : utf8)
    {
        switch (c)
        {
        case '"':  out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n";  break;
        case '\r': out += "\\r";  break;
        case '\t': out += "\\t";  break;
        case '\b': out += "\\b";  break;
        case '\f': out += "\\f";  break;
        default:
            if (c < 0x20)
            {
                char buf[8];
                snprintf(buf, sizeof(buf), "\\u%04x", c);
                out += buf;
            }
            else
            {
                out += static_cast<char>(c);
            }
        }
    }
    return out;
}

struct JsonObj
{
    std::map<std::string, std::string> strs;
    std::map<std::string, double> nums;
    std::map<std::string, bool> bools;
    std::map<std::string, std::vector<std::string>> arrs;

    std::wstring getStr(const char *k, const std::wstring &def = L"") const
    {
        auto it = strs.find(k);
        return it != strs.end() ? UTF8ToWide(it->second) : def;
    }
    int getInt(const char *k, int def) const
    {
        auto it = nums.find(k);
        return it != nums.end() ? static_cast<int>(it->second) : def;
    }
    bool getBool(const char *k, bool def) const
    {
        auto it = bools.find(k);
        return it != bools.end() ? it->second : def;
    }
    std::vector<std::wstring> getArr(const char *k) const
    {
        std::vector<std::wstring> v;
        auto it = arrs.find(k);
        if (it != arrs.end())
            for (auto &s : it->second)
                v.push_back(UTF8ToWide(s));
        return v;
    }
};

static void skipWs(const std::string &s, size_t &i)
{
    while (i < s.size() && (s[i] == ' ' || s[i] == '\t' || s[i] == '\n' || s[i] == '\r'))
        ++i;
}

static std::string parseStr(const std::string &s, size_t &i)
{
    if (i >= s.size() || s[i] != '"')
        return {};
    ++i;
    std::string out;
    while (i < s.size() && s[i] != '"')
    {
        if (s[i] == '\\' && i + 1 < s.size())
        {
            char c = s[i + 1];
            switch (c)
            {
            case 'n': out += '\n'; break;
            case 'r': out += '\r'; break;
            case 't': out += '\t'; break;
            case 'b': out += '\b'; break;
            case 'f': out += '\f'; break;
            case '"': out += '"'; break;
            case '\\': out += '\\'; break;
            case '/': out += '/'; break;
            case 'u':
                if (i + 5 < s.size())
                {
                    unsigned int code = 0;
                    for (int j = 0; j < 4; ++j)
                    {
                        char h = s[i + 2 + j];
                        code <<= 4;
                        if (h >= '0' && h <= '9') code |= h - '0';
                        else if (h >= 'a' && h <= 'f') code |= h - 'a' + 10;
                        else if (h >= 'A' && h <= 'F') code |= h - 'A' + 10;
                    }
                    wchar_t wc = static_cast<wchar_t>(code);
                    out += WideToUTF8(std::wstring(1, wc));
                    i += 6;
                    continue;
                }
                break;
            default: out += c;
            }
            i += 2;
        }
        else
        {
            out += s[i];
            ++i;
        }
    }
    if (i < s.size())
        ++i;
    return out;
}

static bool parseJson(const std::string &s, JsonObj &obj)
{
    size_t i = 0;
    skipWs(s, i);
    if (i >= s.size() || s[i] != '{')
        return false;
    ++i;
    while (i < s.size())
    {
        skipWs(s, i);
        if (i >= s.size() || s[i] == '}')
            break;
        std::string key = parseStr(s, i);
        skipWs(s, i);
        if (i >= s.size() || s[i] != ':')
            return false;
        ++i;
        skipWs(s, i);
        if (i >= s.size())
            return false;
        if (s[i] == '"')
        {
            obj.strs[key] = parseStr(s, i);
        }
        else if (s.compare(i, 4, "true") == 0)
        {
            obj.bools[key] = true;
            i += 4;
        }
        else if (s.compare(i, 5, "false") == 0)
        {
            obj.bools[key] = false;
            i += 5;
        }
        else if (s.compare(i, 4, "null") == 0)
        {
            i += 4;
        }
        else if (s[i] == '[')
        {
            ++i;
            std::vector<std::string> arr;
            while (i < s.size())
            {
                skipWs(s, i);
                if (i >= s.size() || s[i] == ']') break;
                if (s[i] == '"')
                    arr.push_back(parseStr(s, i));
                skipWs(s, i);
                if (i < s.size() && s[i] == ',')
                    ++i;
            }
            if (i < s.size() && s[i] == ']')
                ++i;
            obj.arrs[key] = std::move(arr);
        }
        else if (s[i] == '-' || (s[i] >= '0' && s[i] <= '9'))
        {
            std::string num;
            while (i < s.size() &&
                   (s[i] == '-' || s[i] == '.' || s[i] == 'e' ||
                    s[i] == 'E' || s[i] == '+' || (s[i] >= '0' && s[i] <= '9')))
            {
                num += s[i];
                ++i;
            }
            try { obj.nums[key] = std::stod(num); } catch (...) {}
        }
        skipWs(s, i);
        if (i < s.size() && s[i] == ',')
            ++i;
    }
    return true;
}

static std::string readFileUtf8(const std::wstring &path)
{
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE)
        return {};
    DWORD size = GetFileSize(h, nullptr);
    std::string content(size, 0);
    DWORD read = 0;
    ReadFile(h, content.data(), size, &read, nullptr);
    CloseHandle(h);
    content.resize(read);
    // strip optional UTF-8 BOM
    if (content.size() >= 3 &&
        static_cast<unsigned char>(content[0]) == 0xEF &&
        static_cast<unsigned char>(content[1]) == 0xBB &&
        static_cast<unsigned char>(content[2]) == 0xBF)
        content.erase(0, 3);
    return content;
}

static void writeFileUtf8(const std::wstring &path, const std::string &content)
{
    HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE)
        return;
    DWORD written = 0;
    WriteFile(h, content.data(), static_cast<DWORD>(content.size()), &written, nullptr);
    CloseHandle(h);
}

static void DoLoad()
{
    std::string content = readFileUtf8(GetConfigPath());
    if (content.empty())
        return;
    JsonObj obj;
    if (!parseJson(content, obj))
        return;

    std::wstring fn = obj.getStr("fontName");
    if (!fn.empty()) g_state.fontName = fn;
    int fs = obj.getInt("fontSize", g_state.fontSize);
    if (fs >= 8 && fs <= 72) g_state.fontSize = fs;
    g_state.fontWeight = obj.getInt("fontWeight", g_state.fontWeight);
    g_state.fontItalic = obj.getBool("fontItalic", g_state.fontItalic);
    g_state.fontUnderline = obj.getBool("fontUnderline", g_state.fontUnderline);

    g_state.windowX = obj.getInt("windowX", g_state.windowX);
    g_state.windowY = obj.getInt("windowY", g_state.windowY);
    int w = obj.getInt("windowWidth", g_state.windowWidth);
    int h = obj.getInt("windowHeight", g_state.windowHeight);
    if (w > 0) g_state.windowWidth = w;
    if (h > 0) g_state.windowHeight = h;

    g_state.alwaysOnTop  = obj.getBool("alwaysOnTop", g_state.alwaysOnTop);
    g_state.wordWrap     = obj.getBool("wordWrap", g_state.wordWrap);
    g_state.showStatusBar = obj.getBool("showStatusBar", g_state.showStatusBar);

    int z = obj.getInt("zoomLevel", g_state.zoomLevel);
    if (z >= ZOOM_MIN && z <= ZOOM_MAX) g_state.zoomLevel = z;

    int op = obj.getInt("windowOpacity", g_state.windowOpacity);
    if (op > 0 && op <= 255) g_state.windowOpacity = static_cast<BYTE>(op);

    int t = obj.getInt("theme", static_cast<int>(g_state.theme));
    if (t >= 0 && t <= 2) g_state.theme = static_cast<Theme>(t);

    std::wstring df = obj.getStr("dateTimeFormat");
    if (!df.empty())
        g_state.dateTimeFormat = df;

    g_state.spellCheckEnabled = obj.getBool("spellCheckEnabled", g_state.spellCheckEnabled);
    std::wstring scl = obj.getStr("spellCheckLanguage");
    if (!scl.empty())
        g_state.spellCheckLanguage = scl;
    g_state.showSpecialChars = obj.getBool("showSpecialChars", g_state.showSpecialChars);
    g_state.showLineNumbers = obj.getBool("showLineNumbers", g_state.showLineNumbers);
    g_state.toolsEnabled = obj.getBool("toolsEnabled", g_state.toolsEnabled);
    g_state.quickAccessIcons = obj.getBool("quickAccessIcons", g_state.quickAccessIcons);

    auto recent = obj.getArr("recentFiles");
    g_state.recentFiles.clear();
    for (auto &f : recent)
        g_state.recentFiles.push_back(f);

    int langVal = obj.getInt("language", -1);
    if (langVal >= 0 && langVal <= 2)
        SetLanguage(static_cast<LangID>(langVal));

    RECT rc = {g_state.windowX, g_state.windowY,
               g_state.windowX + g_state.windowWidth, g_state.windowY + g_state.windowHeight};
    HMONITOR hMon = MonitorFromRect(&rc, MONITOR_DEFAULTTONULL);
    if (!hMon)
    {
        g_state.windowX = CW_USEDEFAULT;
        g_state.windowY = CW_USEDEFAULT;
    }
}

static void DoSave()
{
    std::string out;
    out.reserve(1024);
    auto kvStr = [&](const char *k, const std::wstring &v) {
        out += "  \"";
        out += k;
        out += "\": \"";
        out += JsonEscape(v);
        out += "\",\n";
    };
    auto kvInt = [&](const char *k, int v) {
        char buf[32];
        snprintf(buf, sizeof(buf), "%d", v);
        out += "  \"";
        out += k;
        out += "\": ";
        out += buf;
        out += ",\n";
    };
    auto kvBool = [&](const char *k, bool v) {
        out += "  \"";
        out += k;
        out += "\": ";
        out += (v ? "true" : "false");
        out += ",\n";
    };

    out += "{\n";
    kvStr ("fontName",       g_state.fontName);
    kvInt ("fontSize",       g_state.fontSize);
    kvInt ("fontWeight",     g_state.fontWeight);
    kvBool("fontItalic",     g_state.fontItalic);
    kvBool("fontUnderline",  g_state.fontUnderline);
    kvInt ("windowX",        g_state.windowX);
    kvInt ("windowY",        g_state.windowY);
    kvInt ("windowWidth",    g_state.windowWidth);
    kvInt ("windowHeight",   g_state.windowHeight);
    kvBool("alwaysOnTop",    g_state.alwaysOnTop);
    kvBool("wordWrap",       g_state.wordWrap);
    kvBool("showStatusBar",  g_state.showStatusBar);
    kvInt ("zoomLevel",      g_state.zoomLevel);
    kvInt ("windowOpacity",  static_cast<int>(g_state.windowOpacity));
    kvInt ("theme",          static_cast<int>(g_state.theme));
    kvInt ("language",       static_cast<int>(GetCurrentLanguage()));
    kvStr ("dateTimeFormat", g_state.dateTimeFormat);
    kvBool("spellCheckEnabled", g_state.spellCheckEnabled);
    kvStr ("spellCheckLanguage", g_state.spellCheckLanguage);
    kvBool("showSpecialChars", g_state.showSpecialChars);
    kvBool("showLineNumbers", g_state.showLineNumbers);
    kvBool("toolsEnabled", g_state.toolsEnabled);
    kvBool("quickAccessIcons", g_state.quickAccessIcons);

    out += "  \"recentFiles\": [";
    bool first = true;
    for (auto &f : g_state.recentFiles)
    {
        if (!first) out += ", ";
        out += "\"";
        out += JsonEscape(f);
        out += "\"";
        first = false;
    }
    out += "]\n}\n";

    writeFileUtf8(GetConfigPath(), out);
}

void LoadFontSettings()
{
    DoLoad();
    g_loaded = true;
}

void LoadWindowSettings() {}

void SaveFontSettings()
{
    if (g_loaded) DoSave();
}

void SaveWindowSettings()
{
    if (g_loaded) DoSave();
}

void SaveSettings()
{
    if (g_loaded) DoSave();
}
