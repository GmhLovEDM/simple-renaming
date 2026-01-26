#include <windows.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <atlbase.h>
#include <filesystem>
#include <chrono>
#include <sstream>
#include <iomanip>

#pragma comment(lib, "Ole32.lib")
#pragma comment(lib, "Shell32.lib")

#define TRAY_ID 1
#define WM_TRAY (WM_APP + 1)
#define IDM_EXIT 100
#define IDM_AUTORUN 101
#define APP_NAME L"SimpleRenamer"

// ==========================
// 核心逻辑：获取路径 (保持不变)
// ==========================
std::wstring GetSelectedPath()
{
    CComPtr<IShellWindows> wins;
    if (FAILED(wins.CoCreateInstance(CLSID_ShellWindows))) return {};

    HWND hFg = GetForegroundWindow();
    long count = 0; wins->get_Count(&count);

    for (long i = 0; i < count; ++i)
    {
        CComPtr<IDispatch> disp;
        wins->Item(CComVariant(i), &disp);
        CComQIPtr<IWebBrowserApp> app(disp);
        if (!app) continue;

        HWND hwnd = 0; app->get_HWND((LONG_PTR*)&hwnd);
        if (hwnd != hFg) continue;

        CComPtr<IServiceProvider> sp; app->QueryInterface(&sp);
        CComPtr<IShellBrowser> sb; sp->QueryService(SID_STopLevelBrowser, &sb);
        CComPtr<IShellView> sv; sb->QueryActiveShellView(&sv);
        CComPtr<IDataObject> data;
        if (!sv || FAILED(sv->GetItemObject(SVGIO_SELECTION, IID_PPV_ARGS(&data)))) continue;

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
    }
    return {};
}

// ==========================
// 重命名逻辑 (保持不变)
// ==========================
void Rename(const std::wstring& path)
{
    namespace fs = std::filesystem;
    auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::tm tm; localtime_s(&tm, &now);

    std::wstringstream ss;
    ss << std::put_time(&tm, L"%y%m%d%H%M");

    fs::path p(path);
    fs::rename(p, p.parent_path() / (p.stem().wstring() + ss.str() + p.extension().wstring()));
}

// ==========================
// 新增：开机自启逻辑 (注册表操作)
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
            // 为了安全，给路径加个引号
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
// 界面逻辑：托盘 & 窗口
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

        // 1. 获取当前状态
        bool isRun = IsAutoRun();

        // 2. 添加菜单项：如果已开启，则前面打勾 (MF_CHECKED)
        AppendMenu(hMenu, MF_STRING | (isRun ? MF_CHECKED : 0), IDM_AUTORUN, L"开机自启");
        AppendMenu(hMenu, MF_SEPARATOR, 0, nullptr); // 分隔线
        AppendMenu(hMenu, MF_STRING, IDM_EXIT, L"退出");

        // 3. 阻塞等待用户点击，并返回 Command ID
        int cmd = TrackPopupMenu(hMenu, TPM_RETURNCMD | TPM_NONOTIFY, pt.x, pt.y, 0, hwnd, nullptr);
        DestroyMenu(hMenu);

        // 4. 处理点击
        if (cmd == IDM_EXIT)
        {
            DestroyWindow(hwnd);
        }
        else if (cmd == IDM_AUTORUN)
        {
            SetAutoRun(!isRun); // 切换状态
        }
    }
    else if (msg == WM_DESTROY)
    {
        ToggleTray(hwnd, NIM_DELETE);
        PostQuitMessage(0);
    }
    return DefWindowProc(hwnd, msg, wp, lp);
}

// ==========================
// 程序入口 (保持不变)
// ==========================
int WINAPI WinMain(HINSTANCE hInst, HINSTANCE, LPSTR, int)
{
    CoInitialize(nullptr);

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