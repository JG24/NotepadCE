/*
  Snippets side panel. The panel is a plain Win32 TreeView child of the main
  window, mirroring the real directory <exe dir>\Snippets: sub-directories
  become folder nodes (bold), *.txt files become snippet leaves shown without
  their extension. Every file operation goes straight to the filesystem and
  the tree is rebuilt from disk afterwards, so Explorer and the panel can
  never disagree for long.
*/

#include "snippets.h"
#include "core/globals.h"
#include "core/types.h"
#include "theme.h"
#include "file.h"
#include "commands.h"
#include "ui.h"
#include "settings.h"
#include "resource.h"
#include "lang/lang.h"
#include <windowsx.h>
#include <commctrl.h>
#include <shlwapi.h>
#include <shellapi.h>
#include <uxtheme.h>
#include <richedit.h>
#include <string>
#include <vector>
#include <algorithm>

// Per-item payload, owned by the tree; freed in TVN_DELETEITEM.
struct SnippetItem
{
    std::wstring path; // full filesystem path
    bool isFolder;
};

static const int kMinPanelW = 175;
static bool g_splitterDragging = false;
static HIMAGELIST g_snipImages = nullptr;
static int g_iconFolder = 0;
static int g_iconFile = 0;

// Tree drag & drop (move snippets/folders between folders).
static bool g_treeDragging = false;
static HTREEITEM g_dragItem = nullptr;
static HIMAGELIST g_dragImage = nullptr;

// Set while RefreshSnippetTree re-applies the saved expansion state, so the
// TVN_ITEMEXPANDED it triggers doesn't overwrite that state mid-restore.
static bool g_restoringTree = false;

static std::wstring SnippetsRoot()
{
    wchar_t path[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    wchar_t *slash = wcsrchr(path, L'\\');
    if (slash)
        *(slash + 1) = 0;
    return std::wstring(path) + L"Snippets";
}

// The shell's own small icons for a directory and a .txt file, so the tree
// matches Explorer. SHGFI_USEFILEATTRIBUTES resolves the icon from the
// attributes/extension alone — no real file has to exist.
static void InitSnippetIcons()
{
    if (g_snipImages)
        return;
    int cx = GetSystemMetrics(SM_CXSMICON);
    int cy = GetSystemMetrics(SM_CYSMICON);
    HIMAGELIST iml = ImageList_Create(cx, cy, ILC_COLOR32 | ILC_MASK, 2, 0);
    if (!iml)
        return;
    SHFILEINFOW sfi{};
    if (!SHGetFileInfoW(L"folder", FILE_ATTRIBUTE_DIRECTORY, &sfi, sizeof(sfi),
                        SHGFI_ICON | SHGFI_SMALLICON | SHGFI_USEFILEATTRIBUTES) ||
        !sfi.hIcon)
    {
        ImageList_Destroy(iml);
        return;
    }
    int folderIdx = ImageList_AddIcon(iml, sfi.hIcon);
    DestroyIcon(sfi.hIcon);
    sfi = {};
    if (!SHGetFileInfoW(L"snippet.txt", FILE_ATTRIBUTE_NORMAL, &sfi, sizeof(sfi),
                        SHGFI_ICON | SHGFI_SMALLICON | SHGFI_USEFILEATTRIBUTES) ||
        !sfi.hIcon)
    {
        ImageList_Destroy(iml);
        return;
    }
    int fileIdx = ImageList_AddIcon(iml, sfi.hIcon);
    DestroyIcon(sfi.hIcon);
    if (folderIdx < 0 || fileIdx < 0)
    {
        ImageList_Destroy(iml);
        return;
    }
    g_snipImages = iml;
    g_iconFolder = folderIdx;
    g_iconFile = fileIdx;
    TreeView_SetImageList(g_hwndSnippets, g_snipImages, TVSIL_NORMAL);
}

void CreateSnippetsWindow(HWND parent)
{
    INITCOMMONCONTROLSEX icc = {sizeof(icc), ICC_TREEVIEW_CLASSES};
    InitCommonControlsEx(&icc);
    g_hwndSnippets = CreateWindowExW(0, WC_TREEVIEWW, nullptr,
                                     WS_CHILD | WS_TABSTOP | TVS_HASBUTTONS | TVS_HASLINES |
                                         TVS_LINESATROOT | TVS_SHOWSELALWAYS | TVS_EDITLABELS,
                                     0, 0, 100, 100, parent, reinterpret_cast<HMENU>(IDC_SNIPPETS),
                                     GetModuleHandleW(nullptr), nullptr);
    InitSnippetIcons();
    ApplySnippetsTheme();
}

int SnippetsLayoutWidth()
{
    if (!g_state.snippetsPanelVisible || !g_hwndSnippets)
        return 0;
    RECT rc;
    GetClientRect(g_hwndMain, &rc);
    int maxW = rc.right / 2;
    if (maxW < kMinPanelW)
        maxW = kMinPanelW;
    int w = g_state.snippetsPanelWidth;
    if (w < kMinPanelW)
        w = kMinPanelW;
    if (w > maxW)
        w = maxW;
    return w;
}

// ---- Tree population -------------------------------------------------------

static HTREEITEM InsertNode(HTREEITEM parent, const std::wstring &label,
                            const std::wstring &path, bool isFolder)
{
    TVINSERTSTRUCTW ins{};
    ins.hParent = parent;
    ins.hInsertAfter = TVI_LAST;
    ins.item.mask = TVIF_TEXT | TVIF_PARAM;
    ins.item.pszText = const_cast<LPWSTR>(label.c_str());
    ins.item.lParam = reinterpret_cast<LPARAM>(new SnippetItem{path, isFolder});
    if (g_snipImages)
    {
        ins.item.mask |= TVIF_IMAGE | TVIF_SELECTEDIMAGE;
        ins.item.iImage = isFolder ? g_iconFolder : g_iconFile;
        ins.item.iSelectedImage = ins.item.iImage;
    }
    else
    {
        // No shell icons available — fall back to bold folders.
        ins.item.mask |= TVIF_STATE;
        ins.item.state = isFolder ? TVIS_BOLD : 0;
        ins.item.stateMask = TVIS_BOLD;
    }
    return TreeView_InsertItem(g_hwndSnippets, &ins);
}

static void PopulateDir(HTREEITEM parent, const std::wstring &dir, int depth)
{
    if (depth > 16) // symlink-loop backstop
        return;
    struct Entry
    {
        std::wstring name;
        bool isFolder;
    };
    std::vector<Entry> entries;
    WIN32_FIND_DATAW fd;
    HANDLE hFind = FindFirstFileW((dir + L"\\*").c_str(), &fd);
    if (hFind == INVALID_HANDLE_VALUE)
        return;
    do
    {
        if (!wcscmp(fd.cFileName, L".") || !wcscmp(fd.cFileName, L".."))
            continue;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)
            continue;
        bool isDir = (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
        if (!isDir && _wcsicmp(PathFindExtensionW(fd.cFileName), L".txt") != 0)
            continue; // snippets are .txt only
        entries.push_back({fd.cFileName, isDir});
    } while (FindNextFileW(hFind, &fd));
    FindClose(hFind);

    std::sort(entries.begin(), entries.end(), [](const Entry &a, const Entry &b) {
        if (a.isFolder != b.isFolder)
            return a.isFolder; // folders first
        return _wcsicmp(a.name.c_str(), b.name.c_str()) < 0;
    });

    for (const auto &e : entries)
    {
        std::wstring full = dir + L"\\" + e.name;
        std::wstring label = e.isFolder ? e.name : e.name.substr(0, e.name.size() - 4);
        HTREEITEM node = InsertNode(parent, label, full, e.isFolder);
        if (e.isFolder && node)
            PopulateDir(node, full, depth + 1);
    }
}

// Folder expansion state is persisted as paths relative to the Snippets
// root, so it survives app restarts and the full-tree rebuilds after
// rename / create / delete / drag-drop.
static std::wstring RelPath(const std::wstring &full)
{
    std::wstring root = SnippetsRoot() + L"\\";
    if (full.size() > root.size() && _wcsnicmp(full.c_str(), root.c_str(), root.size()) == 0)
        return full.substr(root.size());
    return full;
}

static SnippetItem *GetItemData(HTREEITEM item); // fwd

static void CollectExpanded(HTREEITEM item, std::vector<std::wstring> &out)
{
    for (HTREEITEM it = item; it; it = TreeView_GetNextSibling(g_hwndSnippets, it))
    {
        SnippetItem *d = GetItemData(it);
        if (d && d->isFolder &&
            (TreeView_GetItemState(g_hwndSnippets, it, TVIS_EXPANDED) & TVIS_EXPANDED))
            out.push_back(RelPath(d->path));
        HTREEITEM child = TreeView_GetChild(g_hwndSnippets, it);
        if (child)
            CollectExpanded(child, out);
    }
}

static void RestoreExpanded(HTREEITEM item)
{
    for (HTREEITEM it = item; it; it = TreeView_GetNextSibling(g_hwndSnippets, it))
    {
        SnippetItem *d = GetItemData(it);
        if (d && d->isFolder)
        {
            std::wstring rel = RelPath(d->path);
            for (const auto &e : g_state.snippetsExpanded)
            {
                if (_wcsicmp(e.c_str(), rel.c_str()) == 0)
                {
                    TreeView_Expand(g_hwndSnippets, it, TVE_EXPAND);
                    break;
                }
            }
        }
        HTREEITEM child = TreeView_GetChild(g_hwndSnippets, it);
        if (child)
            RestoreExpanded(child);
    }
}

void RefreshSnippetTree()
{
    if (!g_hwndSnippets)
        return;
    SendMessageW(g_hwndSnippets, WM_SETREDRAW, FALSE, 0);
    g_restoringTree = true;
    TreeView_DeleteAllItems(g_hwndSnippets); // payloads freed via TVN_DELETEITEM
    std::wstring root = SnippetsRoot();
    CreateDirectoryW(root.c_str(), nullptr); // no-op when it already exists
    PopulateDir(TVI_ROOT, root, 0);
    RestoreExpanded(TreeView_GetRoot(g_hwndSnippets));
    g_restoringTree = false;
    SendMessageW(g_hwndSnippets, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(g_hwndSnippets, nullptr, TRUE);
}

void UpdateSnippetsVisibility()
{
    if (!g_hwndSnippets)
        return;
    if (g_state.snippetsPanelVisible)
        RefreshSnippetTree();
    ResizeControls(); // shows/hides the tree per SnippetsLayoutWidth()
}

void ApplySnippetsTheme()
{
    if (!g_hwndSnippets)
        return;
    bool dark = IsDarkMode();
    TreeView_SetBkColor(g_hwndSnippets, GetEditorBgColor());
    TreeView_SetTextColor(g_hwndSnippets, GetEditorTextColor());
    TreeView_SetLineColor(g_hwndSnippets, dark ? RGB(100, 100, 100) : CLR_DEFAULT);
    // DarkMode_Explorer themes the scrollbars + expand chevrons; the item
    // area itself follows the TVM_SET*COLOR values above.
    SetWindowTheme(g_hwndSnippets, dark ? L"DarkMode_Explorer" : L"Explorer", nullptr);
    InvalidateRect(g_hwndSnippets, nullptr, TRUE);
}

// ---- Item helpers ----------------------------------------------------------

static SnippetItem *GetItemData(HTREEITEM item)
{
    if (!item)
        return nullptr;
    TVITEMW tvi{};
    tvi.mask = TVIF_PARAM;
    tvi.hItem = item;
    if (!TreeView_GetItem(g_hwndSnippets, &tvi))
        return nullptr;
    return reinterpret_cast<SnippetItem *>(tvi.lParam);
}

static HTREEITEM FindByPath(HTREEITEM start, const std::wstring &path)
{
    for (HTREEITEM it = start; it; it = TreeView_GetNextSibling(g_hwndSnippets, it))
    {
        SnippetItem *d = GetItemData(it);
        if (d && _wcsicmp(d->path.c_str(), path.c_str()) == 0)
            return it;
        HTREEITEM child = TreeView_GetChild(g_hwndSnippets, it);
        if (child)
        {
            HTREEITEM hit = FindByPath(child, path);
            if (hit)
                return hit;
        }
    }
    return nullptr;
}

// Select the freshly created entry and drop straight into label editing so
// the user can type the real name immediately.
static void SelectAndEditLabel(const std::wstring &path)
{
    HTREEITEM item = FindByPath(TreeView_GetRoot(g_hwndSnippets), path);
    if (!item)
        return;
    TreeView_EnsureVisible(g_hwndSnippets, item);
    TreeView_SelectItem(g_hwndSnippets, item);
    SetFocus(g_hwndSnippets); // EditLabel silently fails without focus
    TreeView_EditLabel(g_hwndSnippets, item);
}

static std::wstring UniquePath(const std::wstring &dir, const std::wstring &base,
                               const wchar_t *ext)
{
    for (int i = 1; i < 1000; ++i)
    {
        std::wstring name = (i == 1) ? base : base + L" (" + std::to_wstring(i) + L")";
        std::wstring full = dir + L"\\" + name + ext;
        if (GetFileAttributesW(full.c_str()) == INVALID_FILE_ATTRIBUTES)
            return full;
    }
    return L"";
}

// ---- Operations ------------------------------------------------------------

static void InsertSnippetIntoEditor(const std::wstring &path)
{
    HANDLE hFile = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                               OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile == INVALID_HANDLE_VALUE)
        return;
    LARGE_INTEGER sz{};
    if (!GetFileSizeEx(hFile, &sz) || sz.HighPart != 0)
    {
        CloseHandle(hFile);
        return;
    }
    std::vector<BYTE> data(sz.LowPart);
    DWORD read = 0;
    BOOL ok = sz.LowPart == 0 ? TRUE : ReadFile(hFile, data.data(), sz.LowPart, &read, nullptr);
    CloseHandle(hFile);
    if (!ok || read != sz.LowPart)
        return;
    auto [enc, le] = DetectEncoding(data);
    (void)le;
    std::wstring text = DecodeText(data, enc);
    // RichEdit's internal line break is a lone CR.
    std::wstring norm;
    norm.reserve(text.size());
    for (size_t i = 0; i < text.size(); ++i)
    {
        if (text[i] == L'\r' && i + 1 < text.size() && text[i + 1] == L'\n')
        {
            norm += L'\r';
            ++i;
        }
        else if (text[i] == L'\n')
            norm += L'\r';
        else
            norm += text[i];
    }
    SendMessageW(g_hwndEditor, EM_REPLACESEL, TRUE, reinterpret_cast<LPARAM>(norm.c_str()));
    SetFocus(g_hwndEditor);
}

static void DeleteSnippet(HTREEITEM item, SnippetItem *data)
{
    const auto &lang = GetLangStrings();
    wchar_t label[260] = {};
    TVITEMW tvi{};
    tvi.mask = TVIF_TEXT;
    tvi.hItem = item;
    tvi.pszText = label;
    tvi.cchTextMax = 259;
    TreeView_GetItem(g_hwndSnippets, &tvi);
    std::wstring msg = lang.snipDeleteConfirm + std::wstring(label) + L"?";
    if (MessageBoxW(g_hwndMain, msg.c_str(), lang.appName.c_str(),
                    MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) != IDYES)
        return;
    // Recycle bin, not a hard delete — this is the only place in the app
    // that destroys user data, so keep it recoverable. SHFileOperation also
    // handles non-empty folders. pFrom must be double-NUL-terminated.
    std::wstring from = data->path;
    from.push_back(L'\0');
    SHFILEOPSTRUCTW op{};
    op.hwnd = g_hwndMain;
    op.wFunc = FO_DELETE;
    op.pFrom = from.c_str();
    op.fFlags = FOF_NOCONFIRMATION | FOF_SILENT | FOF_ALLOWUNDO;
    SHFileOperationW(&op);
    RefreshSnippetTree();
}

static void ShowContextMenu(HTREEITEM item, POINT ptScreen)
{
    const auto &lang = GetLangStrings();
    SnippetItem *data = GetItemData(item);
    enum
    {
        CMD_INSERT = 1,
        CMD_OPEN,
        CMD_NEWSNIP,
        CMD_NEWFOLDER,
        CMD_RENAME,
        CMD_DELETE
    };
    HMENU pop = CreatePopupMenu();
    if (!pop)
        return;
    if (data && !data->isFolder)
    {
        AppendMenuW(pop, MF_STRING, CMD_INSERT, lang.snipInsert.c_str());
        AppendMenuW(pop, MF_STRING, CMD_OPEN, lang.snipOpen.c_str());
        AppendMenuW(pop, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(pop, MF_STRING, CMD_RENAME, lang.snipRename.c_str());
        AppendMenuW(pop, MF_STRING, CMD_DELETE, lang.snipDelete.c_str());
    }
    else // folder, or the empty area below the tree
    {
        AppendMenuW(pop, MF_STRING, CMD_NEWSNIP, lang.snipNewSnippet.c_str());
        AppendMenuW(pop, MF_STRING, CMD_NEWFOLDER, lang.snipNewFolder.c_str());
        if (data)
        {
            AppendMenuW(pop, MF_SEPARATOR, 0, nullptr);
            AppendMenuW(pop, MF_STRING, CMD_RENAME, lang.snipRename.c_str());
            AppendMenuW(pop, MF_STRING, CMD_DELETE, lang.snipDelete.c_str());
        }
    }
    if (item)
        TreeView_SelectItem(g_hwndSnippets, item);
    // TPM_RETURNCMD hands the choice back here — no WM_COMMAND round trip,
    // same pattern as the special-character popup.
    UINT cmd = static_cast<UINT>(TrackPopupMenu(pop, TPM_RETURNCMD | TPM_LEFTALIGN | TPM_TOPALIGN,
                                                ptScreen.x, ptScreen.y, 0, g_hwndMain, nullptr));
    DestroyMenu(pop);

    // New entries land in the clicked folder, next to the clicked snippet,
    // or at the root when the click hit empty space.
    std::wstring dir;
    if (data)
        dir = data->isFolder ? data->path : data->path.substr(0, data->path.find_last_of(L'\\'));
    else
        dir = SnippetsRoot();

    switch (cmd)
    {
    case CMD_INSERT:
        if (data)
            InsertSnippetIntoEditor(data->path);
        break;
    case CMD_OPEN:
        if (data && ConfirmDiscard())
            LoadFile(data->path);
        break;
    case CMD_NEWSNIP:
    {
        std::wstring path = UniquePath(dir, lang.snipDefaultName.c_str(), L".txt");
        if (path.empty())
            break;
        HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                               FILE_ATTRIBUTE_NORMAL, nullptr);
        if (h == INVALID_HANDLE_VALUE)
            break;
        CloseHandle(h);
        RefreshSnippetTree();
        SelectAndEditLabel(path);
        break;
    }
    case CMD_NEWFOLDER:
    {
        std::wstring path = UniquePath(dir, lang.snipDefaultFolder.c_str(), L"");
        if (path.empty() || !CreateDirectoryW(path.c_str(), nullptr))
            break;
        RefreshSnippetTree();
        SelectAndEditLabel(path);
        break;
    }
    case CMD_RENAME:
        if (item)
        {
            SetFocus(g_hwndSnippets);
            TreeView_EditLabel(g_hwndSnippets, item);
        }
        break;
    case CMD_DELETE:
        if (data)
            DeleteSnippet(item, data);
        break;
    }
}

// ---- Notifications ---------------------------------------------------------

LRESULT HandleSnippetsNotify(NMHDR *pnmh, bool *handled)
{
    *handled = true;
    switch (pnmh->code)
    {
    case NM_DBLCLK:
    {
        TVHITTESTINFO ht{};
        GetCursorPos(&ht.pt);
        ScreenToClient(g_hwndSnippets, &ht.pt);
        HTREEITEM item = TreeView_HitTest(g_hwndSnippets, &ht);
        SnippetItem *d = GetItemData(item);
        if (d && !d->isFolder && ConfirmDiscard())
            LoadFile(d->path);
        return 0; // folders keep the default expand/collapse behaviour
    }
    case NM_RCLICK:
    {
        TVHITTESTINFO ht{};
        GetCursorPos(&ht.pt);
        POINT ptScreen = ht.pt;
        ScreenToClient(g_hwndSnippets, &ht.pt);
        HTREEITEM item = TreeView_HitTest(g_hwndSnippets, &ht);
        ShowContextMenu(item, ptScreen);
        return 1; // suppress the default WM_CONTEXTMENU
    }
    case TVN_DELETEITEMW:
    {
        NMTREEVIEWW *tv = reinterpret_cast<NMTREEVIEWW *>(pnmh);
        delete reinterpret_cast<SnippetItem *>(tv->itemOld.lParam);
        return 0;
    }
    case TVN_BEGINLABELEDITW:
        return FALSE; // allow editing
    case TVN_ENDLABELEDITW:
    {
        NMTVDISPINFOW *di = reinterpret_cast<NMTVDISPINFOW *>(pnmh);
        if (!di->item.pszText) // edit cancelled
            return FALSE;
        SnippetItem *d = reinterpret_cast<SnippetItem *>(di->item.lParam);
        if (!d)
            return FALSE;
        std::wstring newName = di->item.pszText;
        if (newName.empty() || newName.find_first_of(L"\\/:*?\"<>|") != std::wstring::npos)
            return FALSE;
        std::wstring parentDir = d->path.substr(0, d->path.find_last_of(L'\\'));
        std::wstring newPath = parentDir + L"\\" + newName + (d->isFolder ? L"" : L".txt");
        if (_wcsicmp(newPath.c_str(), d->path.c_str()) == 0)
            return FALSE; // unchanged
        if (!MoveFileW(d->path.c_str(), newPath.c_str()))
            return FALSE; // collision / locked — keep the old name
        // Child payload paths (for folders) are now stale; rebuilding the
        // tree from inside this notification is unsafe, so defer it. The
        // refresh renders the new name — rejecting the label here is fine.
        PostMessageW(g_hwndMain, WM_SNIPPETS_REFRESH, 0, 0);
        return FALSE;
    }
    case TVN_KEYDOWN:
    {
        const NMTVKEYDOWN *kd = reinterpret_cast<const NMTVKEYDOWN *>(pnmh);
        HTREEITEM sel = TreeView_GetSelection(g_hwndSnippets);
        if (!sel)
            return 0;
        if (kd->wVKey == VK_F2)
        {
            TreeView_EditLabel(g_hwndSnippets, sel);
        }
        else if (kd->wVKey == VK_DELETE)
        {
            SnippetItem *d = GetItemData(sel);
            if (d)
                DeleteSnippet(sel, d);
        }
        else if (kd->wVKey == VK_RETURN)
        {
            SnippetItem *d = GetItemData(sel);
            if (d && !d->isFolder && ConfirmDiscard())
                LoadFile(d->path);
        }
        return 0;
    }
    case TVN_ITEMEXPANDEDW:
    {
        if (g_restoringTree)
            return 0;
        g_state.snippetsExpanded.clear();
        CollectExpanded(TreeView_GetRoot(g_hwndSnippets), g_state.snippetsExpanded);
        SaveSettings();
        return 0;
    }
    case TVN_BEGINDRAGW:
    {
        const NMTREEVIEWW *tv = reinterpret_cast<const NMTREEVIEWW *>(pnmh);
        g_dragItem = tv->itemNew.hItem;
        if (!g_dragItem)
            return 0;
        g_dragImage = TreeView_CreateDragImage(g_hwndSnippets, g_dragItem);
        if (g_dragImage)
        {
            ImageList_BeginDrag(g_dragImage, 0, 0, 0);
            POINT pt = tv->ptDrag;
            ClientToScreen(g_hwndSnippets, &pt);
            ImageList_DragEnter(nullptr, pt.x, pt.y); // NULL = drag over the screen
        }
        g_treeDragging = true;
        SetCapture(g_hwndMain); // mouse traffic now lands in the main WndProc
        return 0;
    }
    }
    *handled = false;
    return 0;
}

// Executed on mouse-up while a tree item is being dragged. Releasing over a
// folder moves into it, over a snippet moves next to it, over empty panel
// space moves to the root; releasing outside the panel cancels.
static void DropDraggedItem()
{
    SnippetItem *src = GetItemData(g_dragItem);
    if (!src)
        return;
    POINT pt;
    GetCursorPos(&pt);
    RECT rcTree;
    GetWindowRect(g_hwndSnippets, &rcTree);
    if (!PtInRect(&rcTree, pt))
        return; // dropped outside the panel — cancel
    TVHITTESTINFO ht{};
    ht.pt = pt;
    ScreenToClient(g_hwndSnippets, &ht.pt);
    HTREEITEM target = TreeView_HitTest(g_hwndSnippets, &ht);
    SnippetItem *dst = GetItemData(target);

    std::wstring destDir;
    if (!dst)
        destDir = SnippetsRoot();
    else if (dst->isFolder)
        destDir = dst->path;
    else
        destDir = dst->path.substr(0, dst->path.find_last_of(L'\\'));

    std::wstring srcDir = src->path.substr(0, src->path.find_last_of(L'\\'));
    if (_wcsicmp(destDir.c_str(), srcDir.c_str()) == 0)
        return; // already there
    if (_wcsicmp(destDir.c_str(), src->path.c_str()) == 0)
        return; // folder onto itself
    std::wstring prefix = src->path + L"\\";
    if (destDir.size() >= prefix.size() &&
        _wcsnicmp(destDir.c_str(), prefix.c_str(), prefix.size()) == 0)
        return; // folder into its own descendant

    std::wstring name = src->path.substr(src->path.find_last_of(L'\\') + 1);
    std::wstring newPath = destDir + L"\\" + name;
    if (GetFileAttributesW(newPath.c_str()) != INVALID_FILE_ATTRIBUTES)
        return; // name collision at the destination — leave everything as-is
    if (MoveFileW(src->path.c_str(), newPath.c_str()))
        RefreshSnippetTree();
}

// ---- Splitter --------------------------------------------------------------

// A 1px vertical line centred in the splitter gap so the panel is visually
// separated from the editor. Painted by the main window's WM_PAINT — the gap
// is bare main-window client area.
void DrawSnippetsSplitter(HDC hdc)
{
    if (!g_state.snippetsPanelVisible || !g_hwndSnippets || !IsWindowVisible(g_hwndSnippets))
        return;
    RECT rcTree;
    GetWindowRect(g_hwndSnippets, &rcTree);
    POINT tl = {rcTree.left, rcTree.top};
    POINT bl = {rcTree.left, rcTree.bottom};
    ScreenToClient(g_hwndMain, &tl);
    ScreenToClient(g_hwndMain, &bl);
    COLORREF c;
    if (IsMatrixTheme())
        c = RGB(0, 110, 40);
    else if (IsDarkMode())
        c = RGB(95, 95, 95);
    else
        c = RGB(185, 185, 185);
    int x = tl.x - SNIPPETS_SPLITTER_W / 2 - 1;
    RECT line = {x, tl.y, x + 1, bl.y};
    HBRUSH hbr = CreateSolidBrush(c);
    FillRect(hdc, &line, hbr);
    DeleteObject(hbr);
}

static bool SplitterHitTest(POINT ptClient)
{
    if (!g_state.snippetsPanelVisible || !g_hwndSnippets || !IsWindowVisible(g_hwndSnippets))
        return false;
    RECT rcTree;
    GetWindowRect(g_hwndSnippets, &rcTree);
    POINT treeLeft = {rcTree.left, rcTree.top};
    ScreenToClient(g_hwndMain, &treeLeft);
    RECT rc;
    GetClientRect(g_hwndMain, &rc);
    return ptClient.x >= treeLeft.x - SNIPPETS_SPLITTER_W && ptClient.x < treeLeft.x &&
           ptClient.y >= 0 && ptClient.y < rc.bottom;
}

bool SnippetsHandleSetCursor()
{
    POINT pt;
    GetCursorPos(&pt);
    ScreenToClient(g_hwndMain, &pt);
    if (g_splitterDragging || SplitterHitTest(pt))
    {
        SetCursor(LoadCursorW(nullptr, IDC_SIZEWE));
        return true;
    }
    return false;
}

bool SnippetsHandleLButtonDown(LPARAM lParam)
{
    POINT pt = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
    if (!SplitterHitTest(pt))
        return false;
    g_splitterDragging = true;
    SetCapture(g_hwndMain);
    return true;
}

bool SnippetsHandleMouseMove(LPARAM lParam)
{
    if (g_treeDragging)
    {
        POINT pt = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
        ClientToScreen(g_hwndMain, &pt);
        if (g_dragImage)
            ImageList_DragMove(pt.x, pt.y);
        TVHITTESTINFO ht{};
        ht.pt = pt;
        ScreenToClient(g_hwndSnippets, &ht.pt);
        HTREEITEM hit = TreeView_HitTest(g_hwndSnippets, &ht);
        // Hide the drag image while the control repaints the drop highlight,
        // otherwise it leaves trails.
        if (g_dragImage)
            ImageList_DragShowNolock(FALSE);
        TreeView_SelectDropTarget(g_hwndSnippets, hit);
        if (g_dragImage)
            ImageList_DragShowNolock(TRUE);
        return true;
    }
    if (!g_splitterDragging)
        return false;
    RECT rc;
    GetClientRect(g_hwndMain, &rc);
    int x = GET_X_LPARAM(lParam); // signed — capture can report x < 0
    int w = rc.right - 2 /*layout pad*/ - x - SNIPPETS_SPLITTER_W / 2;
    int maxW = rc.right / 2;
    if (w < kMinPanelW)
        w = kMinPanelW;
    if (w > maxW)
        w = maxW;
    if (w != g_state.snippetsPanelWidth)
    {
        g_state.snippetsPanelWidth = w;
        ResizeControls();
        // The gap (and its separator line) moved — repaint the exposed
        // main-window background. Children aren't repainted by this.
        InvalidateRect(g_hwndMain, nullptr, TRUE);
    }
    return true;
}

bool SnippetsHandleLButtonUp()
{
    if (g_treeDragging)
    {
        g_treeDragging = false;
        if (g_dragImage)
        {
            ImageList_DragLeave(nullptr);
            ImageList_EndDrag();
            ImageList_Destroy(g_dragImage);
            g_dragImage = nullptr;
        }
        ReleaseCapture();
        TreeView_SelectDropTarget(g_hwndSnippets, nullptr);
        DropDraggedItem();
        g_dragItem = nullptr;
        return true;
    }
    if (!g_splitterDragging)
        return false;
    g_splitterDragging = false;
    ReleaseCapture();
    SaveSettings();
    return true;
}
