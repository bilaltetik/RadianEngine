// Radian.Platform.ixx
module;
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

export module Radian.Platform;
import std;

export namespace Radian::Platform {
    struct WindowConfig {
        std::uint32_t width{ 1280 };
        std::uint32_t height{ 720 };
        std::wstring title{ L"Radian Engine" };
    };

    class Window {
    public:
        bool Create(const WindowConfig& config) {
            HINSTANCE hInst = GetModuleHandle(nullptr);

            WNDCLASSEX wc = {};
            wc.cbSize = sizeof(WNDCLASSEX);
            wc.style = CS_HREDRAW | CS_VREDRAW;
            wc.lpfnWndProc = WindowProc; // Statik fonksiyona bağladık
            wc.hInstance = hInst;
            wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
            wc.lpszClassName = L"RadianWindowClass";

            if (!RegisterClassEx(&wc)) return false;

            m_hwnd = CreateWindowEx(
                0, L"RadianWindowClass", config.title.c_str(),
                WS_OVERLAPPEDWINDOW,
                CW_USEDEFAULT, CW_USEDEFAULT,
                config.width, config.height,
                nullptr, nullptr, hInst, nullptr
            );

            if (!m_hwnd) return false;

            ShowWindow(m_hwnd, SW_SHOW);
            return true;
        }

        void ProcessMessages() {
            MSG msg = {};
            while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
                TranslateMessage(&msg);
                DispatchMessage(&msg);
            }
        }

        bool ShouldClose() const { return m_shouldClose; }
        HWND GetHandle() const { return m_hwnd; }

    private:
        // ÖNEMLİ: WindowProc gövdesi m_shouldClose'u değiştirebilmeli
        static LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
            if (uMsg == WM_DESTROY || uMsg == WM_CLOSE) {
                m_shouldClose = true;
                PostQuitMessage(0);
                return 0;
            }
            return DefWindowProc(hwnd, uMsg, wParam, lParam);
        }

        HWND m_hwnd{ nullptr };
        inline static bool m_shouldClose{ false }; // 'inline static' sayesinde her yerden erişilir
    };
}