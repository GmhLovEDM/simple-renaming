#include <windows.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <atlbase.h>
#include <filesystem>
#include <chrono>
#include <sstream>
#include <iomanip>
#include <string>
#include <vector>
#include <cwctype>

#pragma comment(lib, "Ole32.lib")
#pragma comment(lib, "Shell32.lib")

#define TRAY_ID 1
#define WM_TRAY (WM_APP + 1)
#define IDM_EXIT 100
#define IDM_AUTORUN 101
#define APP_NAME L"SimpleRenamer"

// ==========================
// 核心逻辑：获取路径
// ==========================
std::wstring GetSelectedPath()
{
    HWND hFg = GetForegroundWindow();
    wchar_t className[256];
    GetClassNameW(hFg, className, 256);

    bool isDesktop = (wcscmp(className, L"Progman") == 0 || wcscmp(className, L"WorkerW") == 0);
    bool isExplorer = (wcscmp(className, L"CabinetWClass") == 0 || wcscmp(className, L"ExploreWClass") == 0);

    if (!isDesktop && !isExplorer) return {};

    CComPtr<IShellWindows> wins;
    if (FAILED(wins.CoCreateInstance(CLSID_ShellWindows))) return {};

    CComPtr<IDispatch> disp;
    if (isDesktop)
    {
        CComVariant varLoc(CSIDL_DESKTOP);
        CComVariant vEmpty;
        long hwndVal = (long)(LONG_PTR)hFg;

        wins->FindWindowSW(&varLoc, &vEmpty, SWC_DESKTOP, &hwndVal, SWFO_NEEDDISPATCH, &disp);
    }
    else
    {
        long count = 0; wins->get_Count(&count);
        for (long i = 0; i < count; ++i)
        {
            CComPtr<IDispatch> d;
            wins->Item(CComVariant(i), &d);
            CComQIPtr<IWebBrowserApp> app(d);
            if (!app) continue;

            HWND h = 0; app->get_HWND((LONG_PTR*)&h);
            if (h == hFg) { disp = d; break; }
        }
    }

    if (!disp) return {};

    CComQIPtr<IWebBrowserApp> app(disp);
    if (!app) return {};

    CComPtr<IServiceProvider> sp; app->QueryInterface(&sp);
    CComPtr<IShellBrowser> sb; sp->QueryService(SID_STopLevelBrowser, &sb);
    CComPtr<IShellView> sv; sb->QueryActiveShellView(&sv);
    CComPtr<IDataObject> data;
    if (!sv || FAILED(sv->GetItemObject(SVGIO_SELECTION, IID_PPV_ARGS(&data)))) return {};

    FORMATETC fmt{ CF_HDROP, 0, DVASPECT_CONTENT, -1, TYMED_HGLOBAL };
    STGMEDIUM stg;
    if (SUCCEEDED(data->GetData(&fmt, &stg)))
    {
        wchar_t path[MAX_PATH];
        if (DragQueryFileW((HDROP)stg.hGlobal, 0, path, MAX_PATH))
        {
            ReleaseStgMedium(&stg);
            return path;
        }
        ReleaseStgMedium(&stg);
    }
    return {};
}

struct AppConfig
{
    std::wstring timeFormat = L"%y%m%d%H%M";
    UINT hotkeyModifiers = 0;
    UINT hotkeyKey = VK_F9;
};

std::wstring GetConfigPath()
{
    wchar_t path[MAX_PATH];
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    std::filesystem::path exePath(path);
    return (exePath.parent_path() / L"SimpleRenamer.ini").wstring();
}

std::wstring ToUpper(const std::wstring& input)
{
    std::wstring out = input;
    for (auto& ch : out) ch = (wchar_t)towupper(ch);
    return out;
}

std::wstring Trim(const std::wstring& input)
{
    size_t start = 0;
    while (start < input.size() && iswspace(input[start])) ++start;
    size_t end = input.size();
    while (end > start && iswspace(input[end - 1])) --end;
    return input.substr(start, end - start);
}

bool ParseHotkey(const std::wstring& hotkey, UINT& modifiers, UINT& key)
{
    modifiers = 0;
    key = 0;

    size_t start = 0;
    while (start < hotkey.size())
    {
        size_t end = hotkey.find(L'+', start);
        if (end == std::wstring::npos) end = hotkey.size();
        std::wstring token = Trim(hotkey.substr(start, end - start));
        token = ToUpper(token);

        if (token == L"CTRL" || token == L"CONTROL") modifiers |= MOD_CONTROL;
        else if (token == L"ALT") modifiers |= MOD_ALT;
        else if (token == L"SHIFT") modifiers |= MOD_SHIFT;
        else if (token == L"WIN" || token == L"WINDOWS") modifiers |= MOD_WIN;
        else if (!token.empty())
        {
            if (token.size() == 1)
            {
                wchar_t ch = token[0];
                if ((ch >= L'A' && ch <= L'Z') || (ch >= L'0' && ch <= L'9'))
                {
                    key = VkKeyScanW(ch) & 0xFF;
                }
            }
            else if (token[0] == L'F')
            {
                int fn = _wtoi(token.c_str() + 1);
                if (fn >= 1 && fn <= 24)
                {
                    key = VK_F1 + (fn - 1);
                }
            }
        }

        start = end + 1;
    }

    return key != 0;
}

AppConfig LoadConfig()
{
    AppConfig config;
    std::wstring configPath = GetConfigPath();

    wchar_t buffer[256];
    GetPrivateProfileStringW(L"Settings", L"TimeFormat", config.timeFormat.c_str(), buffer, 256, configPath.c_str());
    config.timeFormat = buffer;

    GetPrivateProfileStringW(L"Settings", L"Hotkey", L"F9", buffer, 256, configPath.c_str());
    UINT modifiers = 0;
    UINT key = 0;
    if (ParseHotkey(buffer, modifiers, key))
    {
        config.hotkeyModifiers = modifiers;
        config.hotkeyKey = key;
    }

    return config;
}

// ==========================
void Rename(const std::wstring& oldPath, const std::wstring& timeFormat)
    ss << std::put_time(&tm, timeFormat.c_str());
void Rename(const std::wstring& oldPath)
{
    namespace fs = std::filesystem;
    auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::tm tm; localtime_s(&tm, &now);

    std::wstringstream ss;
    ss << std::put_time(&tm, L"%y%m%d%H%M");

    fs::path p(oldPath);
    if (!fs::exists(p)) return;

    fs::path newPath = p.parent_path() / (p.stem().wstring() + ss.str() + p.extension().wstring());

    std::error_code ec;
    fs::rename(p, newPath, ec);

    if (!ec)
    {
        SHChangeNotify(SHCNE_RENAMEITEM, SHCNF_PATH, oldPath.c_str(), newPath.c_str());
    }
}

// ==========================
// 开机自启逻辑
// ==========================
bool IsAutoRun()
{
    HKEY hKey;
    if (RegOpenKeyW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run", &hKey) == ERROR_SUCCESS)
    {
        DWORD type = 0;
        LSTATUS status = RegQueryValueExW(hKey, APP_NAME, nullptr, &type, nullptr, nullptr);
        RegCloseKey(hKey);
        return (status == ERROR_SUCCESS);
    }
    return false;
}

void SetAutoRun(bool enable)
{
    HKEY hKey;
    if (RegOpenKeyW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run", &hKey) == ERROR_SUCCESS)
    {
        if (enable)
        {
            wchar_t path[MAX_PATH];
            GetModuleFileNameW(nullptr, path, MAX_PATH);
            std::wstring cmd = L"\"" + std::wstring(path) + L"\"";
            RegSetValueExW(hKey, APP_NAME, 0, REG_SZ, (BYTE*)cmd.c_str(), (DWORD)(cmd.size() + 1) * 2);
        }
        else
        {
            RegDeleteValueW(hKey, APP_NAME);
        }
        RegCloseKey(hKey);
    }
}

// ==========================
// 界面逻辑
// ==========================
void ToggleTray(HWND hwnd, DWORD msg)
{
    NOTIFYICONDATA nid{ sizeof(nid), hwnd, TRAY_ID, NIF_MESSAGE | NIF_ICON | NIF_TIP };
    nid.uCallbackMessage = WM_TRAY;
    nid.hIcon = LoadIcon(nullptr, IDI_APPLICATION);
    wcscpy_s(nid.szTip, APP_NAME);
    Shell_NotifyIcon(msg, &nid);
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    if (msg == WM_TRAY && lp == WM_RBUTTONUP)
    {
        POINT pt; GetCursorPos(&pt);
        SetForegroundWindow(hwnd);

        HMENU hMenu = CreatePopupMenu();
        bool isRun = IsAutoRun();
    AppConfig config = LoadConfig();

    if (!RegisterHotKey(nullptr, 1, config.hotkeyModifiers, config.hotkeyKey))
    {
        RegisterHotKey(nullptr, 1, 0, VK_F9);
    }
            if (!path.empty()) Rename(path, config.timeFormat);
        AppendMenu(hMenu, MF_STRING, IDM_EXIT, L"退出");

        int cmd = TrackPopupMenu(hMenu, TPM_RETURNCMD | TPM_NONOTIFY, pt.x, pt.y, 0, hwnd, nullptr);
        DestroyMenu(hMenu);

        if (cmd == IDM_EXIT) DestroyWindow(hwnd);
        else if (cmd == IDM_AUTORUN) SetAutoRun(!isRun);
    }
    else if (msg == WM_DESTROY)
    {
        ToggleTray(hwnd, NIM_DELETE);
        PostQuitMessage(0);
    }
    return DefWindowProc(hwnd, msg, wp, lp);
}

// ==========================
// 程序入口
// ==========================
int WINAPI WinMain(_In_ HINSTANCE hInst, _In_opt_ HINSTANCE, _In_ LPSTR, _In_ int)
{
    if (FAILED(CoInitialize(nullptr))) return 0;

    WNDCLASS wc{ 0, WndProc, 0, 0, hInst, 0, 0, 0, 0, L"TrayWnd" };
    RegisterClass(&wc);
    HWND hwnd = CreateWindow(L"TrayWnd", nullptr, 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, hInst, nullptr);

    ToggleTray(hwnd, NIM_ADD);
    RegisterHotKey(nullptr, 1, 0, VK_F9);

    MSG msg;
    while (GetMessage(&msg, nullptr, 0, 0))
    {
        if (msg.message == WM_HOTKEY)
        {
            std::wstring path = GetSelectedPath();
            if (!path.empty()) Rename(path);
        }
        DispatchMessage(&msg);
    }

    CoUninitialize();
    return 0;
}