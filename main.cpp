#include <windows.h>
#include <psapi.h>
#include <tlhelp32.h>
#include <string>
#include <vector>
#include <thread>
#include <atomic>
#include <random>
#include <chrono>
#include <sstream>
#include <iomanip>

#pragma comment(lib, "user32.lib")
#pragma comment(lib, "kernel32.lib")
#pragma comment(lib, "gdi32.lib")

const DWORD PROCESS_ALL_ACCESS = 0x1F0FFF;
const char* PROCESS_NAME = "HD-Player.exe";

const char* AIMBOT_AOB = "FF FF FF FF FF FF FF FF FF FF FF FF 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 FF FF FF FF ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? 80 BF";

const uintptr_t TARGET_OFFSET = 0x90;
const uintptr_t WRITE_OFFSET = 0x8C;
const uintptr_t TEAM_OFFSET = 0x10;
const int LOCAL_TEAM = 1;

const float FOV_LIMIT = 92.0f;
const float SMOOTH_BASE = 0.74f;
const int MAX_ENTITIES = 45;

// Global variables
std::atomic<bool> g_active{ false };
std::atomic<bool> g_running{ true };
char g_current_hotkey = 'R';
HANDLE g_hProcess = nullptr;
DWORD g_procId = 0;
std::vector<uintptr_t> g_entities;
std::vector<BYTE> g_lastAimBytes;
HHOOK g_keyboardHook = nullptr;
HWND g_hwnd = nullptr;

// Random engine
std::mt19937 g_rng(std::random_device{}());

// Forward declarations
LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
LRESULT CALLBACK KeyboardProc(int nCode, WPARAM wParam, LPARAM lParam);
bool OpenProcessByName();
bool ReadRaw(uintptr_t addr, void* buffer, size_t size);
bool WriteRaw(uintptr_t addr, const void* data, size_t size);
std::vector<uintptr_t> AoBScan(uintptr_t start, uintptr_t end, const char* pattern); // TODO: implement
void AimbotLoop();
void UpdateStatus();
void ToggleAimbot();

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
{
    // Request admin rights if not already
    if (!IsUserAnAdmin()) {
        ShellExecuteA(nullptr, "runas", GetCommandLineA(), nullptr, nullptr, SW_SHOWNORMAL);
        return 0;
    }

    // Hide console if exists
    HWND console = GetConsoleWindow();
    if (console) ShowWindow(console, SW_HIDE);

    // Register window class
    WNDCLASSA wc = {};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = "AimbotWindowClass";
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);

    RegisterClassA(&wc);

    // Create window
    g_hwnd = CreateWindowA("AimbotWindowClass", "Aimbot",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU,
        CW_USEDEFAULT, CW_USEDEFAULT, 560, 380,
        nullptr, nullptr, hInstance, nullptr);

    if (!g_hwnd) return 0;

    ShowWindow(g_hwnd, SW_SHOW);
    UpdateWindow(g_hwnd);

    // Keyboard hook
    g_keyboardHook = SetWindowsHookExA(WH_KEYBOARD_LL, KeyboardProc, GetModuleHandleA(nullptr), 0);

    // Start threads
    std::thread attachThread([]() {
        while (g_running && !OpenProcessByName()) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
        if (g_hProcess) {
            // Update UI from main thread if needed (use PostMessage or similar)
            PostMessage(g_hwnd, WM_USER + 1, 0, 0); // Custom message for "Connected"
        }
    });
    attachThread.detach();

    std::thread aimbotThread(AimbotLoop);
    aimbotThread.detach();

    // Message loop
    MSG msg = {};
    while (GetMessageA(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageA(&msg);
    }

    g_running = false;
    if (g_hProcess) CloseHandle(g_hProcess);
    if (g_keyboardHook) UnhookWindowsHookEx(g_keyboardHook);

    return (int)msg.wParam;
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    static HFONT hBigFont = CreateFontA(48, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, DEFAULT_PITCH, "Segoe UI");
    static HFONT hTitleFont = CreateFontA(26, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, DEFAULT_PITCH, "Segoe UI");

    switch (msg) {
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);

        RECT rect;
        GetClientRect(hwnd, &rect);

        // Background
        FillRect(hdc, &rect, (HBRUSH)GetStockObject(BLACK_BRUSH));

        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, RGB(0, 255, 157)); // #00ff9d

        // Title
        SelectObject(hdc, hTitleFont);
        DrawTextA(hdc, "AIMBOT", -1, &rect, DT_CENTER | DT_TOP);

        // Status
        RECT statusRect = { 0, 100, rect.right, 200 };
        SetTextColor(hdc, g_active ? RGB(0, 255, 157) : RGB(255, 45, 85));
        SelectObject(hdc, hBigFont);
        DrawTextA(hdc, g_active ? "ACTIVE" : "OFF", -1, &statusRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

        // Hotkey info
        SetTextColor(hdc, RGB(187, 187, 187));
        char hotkeyText[64];
        sprintf_s(hotkeyText, "Hotkey: %c (click to change)", toupper(g_current_hotkey));
        TextOutA(hdc, 50, 220, hotkeyText, (int)strlen(hotkeyText));

        // Info
        SetTextColor(hdc, RGB(85, 85, 85));
        char infoText[128];
        sprintf_s(infoText, "FOV: %.0f°   •   Smooth: %.2f", FOV_LIMIT, SMOOTH_BASE);
        TextOutA(hdc, 50, 260, infoText, (int)strlen(infoText));

        // Status bar
        RECT barRect = { 0, rect.bottom - 40, rect.right, rect.bottom };
        FillRect(hdc, &barRect, CreateSolidBrush(RGB(26, 26, 26)));
        SetTextColor(hdc, g_hProcess ? RGB(0, 255, 157) : RGB(119, 119, 119));
        TextOutA(hdc, 10, rect.bottom - 35, g_hProcess ? "Connected to HD-Player" : "Waiting for HD-Player...", -1);

        EndPaint(hwnd, &ps);
        break;
    }

    case WM_USER + 1: // Connected message
        InvalidateRect(hwnd, nullptr, TRUE);
        break;

    case WM_DESTROY:
        g_running = false;
        PostQuitMessage(0);
        break;

    default:
        return DefWindowProcA(hwnd, msg, wParam, lParam);
    }
    return 0;
}

LRESULT CALLBACK KeyboardProc(int nCode, WPARAM wParam, LPARAM lParam)
{
    if (nCode == HC_ACTION && wParam == WM_KEYDOWN) {
        KBDLLHOOKSTRUCT* p = (KBDLLHOOKSTRUCT*)lParam;
        if (p->vkCode == toupper(g_current_hotkey)) {
            ToggleAimbot();
        }
    }
    return CallNextHookEx(g_keyboardHook, nCode, wParam, lParam);
}

void ToggleAimbot()
{
    g_active = !g_active;
    InvalidateRect(g_hwnd, nullptr, TRUE); // Redraw
}

bool OpenProcessByName()
{
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) return false;

    PROCESSENTRY32 pe = { sizeof(PROCESSENTRY32) };
    if (Process32First(snapshot, &pe)) {
        do {
            if (_stricmp(pe.szExeFile, PROCESS_NAME) == 0) {
                g_hProcess = OpenProcess(PROCESS_ALL_ACCESS, FALSE, pe.th32ProcessID);
                if (g_hProcess) {
                    g_procId = pe.th32ProcessID;
                    CloseHandle(snapshot);
                    return true;
                }
            }
        } while (Process32Next(snapshot, &pe));
    }
    CloseHandle(snapshot);
    return false;
}

bool ReadRaw(uintptr_t addr, void* buffer, size_t size)
{
    SIZE_T bytesRead;
    return ReadProcessMemory(g_hProcess, (LPCVOID)addr, buffer, size, &bytesRead) && bytesRead == size;
}

bool WriteRaw(uintptr_t addr, const void* data, size_t size)
{
    SIZE_T bytesWritten;
    return WriteProcessMemory(g_hProcess, (LPVOID)addr, data, size, &bytesWritten) && bytesWritten == size;
}

// TODO: Implement proper AoB scan (pattern with ?? as wildcard)
std::vector<uintptr_t> AoBScan(uintptr_t start, uintptr_t end, const char* pattern)
{
    // Placeholder - returns empty like Python version
    return {};
}

void AimbotLoop()
{
    std::uniform_real_distribution<float> rand018_072(0.018f, 0.072f);
    std::uniform_real_distribution<float> randSmooth(0.01f, 0.06f);

    while (g_running) {
        if (g_active && g_hProcess) {
            auto now = std::chrono::steady_clock::now(); // Simple timing

            if (g_entities.empty()) { // Scan periodically
                g_entities = AoBScan(0x10000, 0x7FFFFFEFFFF, AIMBOT_AOB);
            }

            for (uintptr_t base : g_entities) {
                try {
                    int team = 0;
                    if (ReadRaw(base + TEAM_OFFSET, &team, 4) && team == LOCAL_TEAM) continue;

                    float targetVal = 0.0f;
                    if (ReadRaw(base + TARGET_OFFSET, &targetVal, 4)) {
                        std::this_thread::sleep_for(std::chrono::milliseconds((int)(rand018_072(g_rng) * 1000)));

                        // Smart smooth write
                        float current = 0.0f;
                        if (!g_lastAimBytes.empty()) {
                            memcpy(&current, g_lastAimBytes.data(), 4);
                        }

                        float diff = fabs(targetVal - current);
                        float factor = SMOOTH_BASE;
                        if (diff > 18) factor = 0.91f;
                        else if (diff > 8) factor = 0.82f;
                        else factor = SMOOTH_BASE - randSmooth(g_rng);

                        float newVal = current + (targetVal - current) * factor;
                        WriteRaw(base + WRITE_OFFSET, &newVal, 4);

                        g_lastAimBytes.assign((BYTE*)&newVal, (BYTE*)&newVal + 4);
                    }
                }
                catch (...) {}
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(42));
    }
}