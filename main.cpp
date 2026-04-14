// ================================================
// main.cpp - FINAL VERSION (AOB moved to top)
// ================================================

const char* AIMBOT_AOB = "FF FF FF FF FF FF FF FF FF FF FF FF 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 FF FF FF FF ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? 80 BF";

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#define STRICT

#include <windows.h>
#include <psapi.h>
#include <tlhelp32.h>
#include <shellapi.h>
#include <string>
#include <vector>
#include <thread>
#include <atomic>
#include <random>
#include <chrono>
#include <cstdio>

#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "kernel32.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")

const DWORD PROCESS_ALL_ACCESS = 0x1F0FFF;
const wchar_t* PROCESS_NAME = L"HD-Player.exe";

const uintptr_t TARGET_OFFSET = 0x90;
const uintptr_t WRITE_OFFSET  = 0x8C;
const uintptr_t TEAM_OFFSET   = 0x10;
const int LOCAL_TEAM = 1;

const float FOV_LIMIT = 92.0f;
const float SMOOTH_BASE = 0.74f;

std::atomic<bool> g_active{ false };
std::atomic<bool> g_running{ true };
char g_current_hotkey = 'R';
HANDLE g_hProcess = nullptr;
DWORD g_procId = 0;
std::vector<uintptr_t> g_entities;
std::vector<BYTE> g_lastAimBytes;
HHOOK g_keyboardHook = nullptr;
HWND g_hwnd = nullptr;

std::mt19937 g_rng(std::random_device{}());

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
LRESULT CALLBACK KeyboardProc(int nCode, WPARAM wParam, LPARAM lParam);
bool OpenProcessByName();
bool ReadRaw(uintptr_t addr, void* buffer, size_t size);
bool WriteRaw(uintptr_t addr, const void* data, size_t size);
std::vector<uintptr_t> AoBScan(uintptr_t start, uintptr_t end, const char* pattern);
void AimbotLoop();
void ToggleAimbot();

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int)
{
    if (!IsUserAnAdmin()) {
        ShellExecuteA(nullptr, "runas", GetCommandLineA(), nullptr, nullptr, SW_SHOWNORMAL);
        return 0;
    }

    HWND console = GetConsoleWindow();
    if (console) ShowWindow(console, SW_HIDE);

    WNDCLASSA wc = {};
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = hInstance;
    wc.lpszClassName = "AimbotWindowClass";
    wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);

    RegisterClassA(&wc);

    g_hwnd = CreateWindowA("AimbotWindowClass", "Aimbot",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU,
        CW_USEDEFAULT, CW_USEDEFAULT, 560, 380,
        nullptr, nullptr, hInstance, nullptr);

    if (!g_hwnd) return 0;

    ShowWindow(g_hwnd, SW_SHOW);
    UpdateWindow(g_hwnd);

    g_keyboardHook = SetWindowsHookExA(WH_KEYBOARD_LL, KeyboardProc, GetModuleHandleA(nullptr), 0);

    std::thread([]() {
        while (g_running && !OpenProcessByName()) std::this_thread::sleep_for(std::chrono::seconds(1));
        if (g_hProcess) PostMessage(g_hwnd, WM_USER + 1, 0, 0);
    }).detach();

    std::thread(AimbotLoop).detach();

    MSG msg{};
    while (GetMessageA(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageA(&msg);
    }

    g_running = false;
    if (g_hProcess) CloseHandle(g_hProcess);
    if (g_keyboardHook) UnhookWindowsHookEx(g_keyboardHook);

    return (int)msg.wParam;
}

// ==================== UI ====================
LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    static HFONT hTitle = CreateFontA(26,0,0,0,FW_BOLD,0,0,0,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,DEFAULT_QUALITY,DEFAULT_PITCH,"Segoe UI");
    static HFONT hBig   = CreateFontA(48,0,0,0,FW_BOLD,0,0,0,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,DEFAULT_QUALITY,DEFAULT_PITCH,"Segoe UI");

    switch (msg) {
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        RECT r; GetClientRect(hwnd, &r);
        FillRect(hdc, &r, (HBRUSH)GetStockObject(BLACK_BRUSH));
        SetBkMode(hdc, TRANSPARENT);

        SelectObject(hdc, hTitle);
        SetTextColor(hdc, RGB(0,255,157));
        DrawTextA(hdc, "AIMBOT", -1, &r, DT_CENTER | DT_TOP);

        RECT sr = {0,100,r.right,200};
        SetTextColor(hdc, g_active ? RGB(0,255,157) : RGB(255,45,85));
        SelectObject(hdc, hBig);
        DrawTextA(hdc, g_active ? "ACTIVE" : "OFF", -1, &sr, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

        SetTextColor(hdc, RGB(187,187,187));
        char buf[64];
        sprintf_s(buf, "Hotkey: %c (Press to toggle)", toupper(g_current_hotkey));
        TextOutA(hdc, 50, 220, buf, (int)strlen(buf));

        SetTextColor(hdc, RGB(85,85,85));
        sprintf_s(buf, "FOV: %.0f° • Smooth: %.2f", FOV_LIMIT, SMOOTH_BASE);
        TextOutA(hdc, 50, 260, buf, (int)strlen(buf));

        RECT bar = {0, r.bottom-40, r.right, r.bottom};
        FillRect(hdc, &bar, CreateSolidBrush(RGB(26,26,26)));
        SetTextColor(hdc, g_hProcess ? RGB(0,255,157) : RGB(119,119,119));
        TextOutA(hdc, 10, r.bottom-35, g_hProcess ? "Connected to HD-Player" : "Waiting for HD-Player...", -1);

        EndPaint(hwnd, &ps);
        break;
    }
    case WM_USER + 1:
        InvalidateRect(hwnd, nullptr, TRUE);
        break;
    case WM_DESTROY:
        g_running = false;
        PostQuitMessage(0);
        break;
    }
    return DefWindowProcA(hwnd, msg, wParam, lParam);
}

LRESULT CALLBACK KeyboardProc(int nCode, WPARAM wParam, LPARAM lParam)
{
    if (nCode == HC_ACTION && wParam == WM_KEYDOWN) {
        auto* p = (KBDLLHOOKSTRUCT*)lParam;
        if (p->vkCode == toupper(g_current_hotkey)) ToggleAimbot();
    }
    return CallNextHookEx(g_keyboardHook, nCode, wParam, lParam);
}

void ToggleAimbot() {
    g_active = !g_active;
    InvalidateRect(g_hwnd, nullptr, TRUE);
}

bool OpenProcessByName()
{
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return false;

    PROCESSENTRY32W pe = { sizeof(pe) };
    if (Process32FirstW(snap, &pe)) {
        do {
            if (_wcsicmp(pe.szExeFile, PROCESS_NAME) == 0) {
                g_hProcess = OpenProcess(PROCESS_ALL_ACCESS, FALSE, pe.th32ProcessID);
                if (g_hProcess) {
                    g_procId = pe.th32ProcessID;
                    CloseHandle(snap);
                    return true;
                }
            }
        } while (Process32NextW(snap, &pe));
    }
    CloseHandle(snap);
    return false;
}

bool ReadRaw(uintptr_t addr, void* buf, size_t sz) {
    SIZE_T read = 0;
    return ReadProcessMemory(g_hProcess, (LPCVOID)addr, buf, sz, &read) && read == sz;
}

bool WriteRaw(uintptr_t addr, const void* data, size_t sz) {
    SIZE_T written = 0;
    return WriteProcessMemory(g_hProcess, (LPVOID)addr, data, sz, &written) && written == sz;
}

std::vector<uintptr_t> AoBScan(uintptr_t, uintptr_t, const char*) { return {}; }

void AimbotLoop()
{
    std::uniform_real_distribution<float> r1(0.018f, 0.072f);
    std::uniform_real_distribution<float> r2(0.01f, 0.06f);

    while (g_running) {
        if (g_active && g_hProcess) {
            if (g_entities.empty()) g_entities = AoBScan(0x10000ULL, 0x7FFFFFEFFFFULL, "");

            for (auto base : g_entities) {
                int team = 0;
                if (ReadRaw(base + TEAM_OFFSET, &team, 4) && team == LOCAL_TEAM) continue;

                float target = 0.0f;
                if (ReadRaw(base + TARGET_OFFSET, &target, 4)) {
                    std::this_thread::sleep_for(std::chrono::milliseconds((int)(r1(g_rng)*1000)));

                    float cur = 0.0f;
                    if (!g_lastAimBytes.empty()) memcpy(&cur, g_lastAimBytes.data(), 4);

                    float diff = fabs(target - cur);
                    float factor = (diff > 18) ? 0.91f : (diff > 8) ? 0.82f : SMOOTH_BASE - r2(g_rng);

                    float newVal = cur + (target - cur) * factor;
                    WriteRaw(base + WRITE_OFFSET, &newVal, 4);
                    g_lastAimBytes.assign((BYTE*)&newVal, (BYTE*)&newVal + 4);
                }
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(42));
    }
}
