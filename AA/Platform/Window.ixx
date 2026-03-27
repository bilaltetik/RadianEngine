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
        std::wstring  title{ L"Radian Engine" };
    };

    class Window {
    public:
        // [C++20] [[nodiscard]] — dönüş değeri yok sayılamaz, hata sessizce geçemez
        [[nodiscard]] bool Create(const WindowConfig& config) {
            HINSTANCE hInst = GetModuleHandle(nullptr);

            // [C++20] Designated initializers — hangi alanın ne olduğu okunuyor,
            //         magic positional sıralaması yok
            WNDCLASSEXW wc{
                .cbSize = sizeof(WNDCLASSEXW),
                .style = CS_HREDRAW | CS_VREDRAW,
                .lpfnWndProc = WindowProc,
                .hInstance = hInst,
                .hCursor = LoadCursor(nullptr, IDC_ARROW),
                .lpszClassName = L"RadianWindowClass"
            };

            if (!RegisterClassExW(&wc)) return false;

            // CreateWindowExW'ye `this` gönderiyoruz (lpCreateParams),
            // böylece WindowProc içinde instance'a erişebiliyoruz —
            // artık inline static hack'e gerek yok
            m_hwnd = CreateWindowExW(
                0,
                L"RadianWindowClass",
                config.title.c_str(),
                WS_OVERLAPPEDWINDOW,
                CW_USEDEFAULT, CW_USEDEFAULT,
                static_cast<int>(config.width),
                static_cast<int>(config.height),
                nullptr, nullptr, hInst,
                this    // <-- burası kritik
            );

            if (!m_hwnd) return false;

            ShowWindow(m_hwnd, SW_SHOW);
            return true;
        }

        void ProcessMessages() {
            MSG msg{};  // [C++20] = {} yerine doğrudan {} value-init
            while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
                TranslateMessage(&msg);
                DispatchMessageW(&msg);
            }
        }

        // [C++20] [[nodiscard]] + noexcept — saf sorgulama fonksiyonu
        [[nodiscard]] bool ShouldClose() const noexcept {
            // [C++20] std::atomic<bool>::load — acquire semantiği ile thread-safe okuma
            return m_shouldClose.load(std::memory_order_acquire);
        }

        [[nodiscard]] HWND GetHandle() const noexcept { return m_hwnd; }

    private:
        static LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
            Window* self = nullptr;

            if (uMsg == WM_NCCREATE) {
                // Pencere ilk oluşturulurken this pointer'ı GWLP_USERDATA'ya göm
                auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
                self = static_cast<Window*>(cs->lpCreateParams);
                SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
            }
            else {
                self = reinterpret_cast<Window*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
            }

            if (self) {
                // [C++23] [[likely]] / [[unlikely]] — branch-predictor hint
                if (uMsg == WM_DESTROY || uMsg == WM_CLOSE) [[unlikely]] {
                    // [C++20] atomic release store — thread-safe yazma
                    self->m_shouldClose.store(true, std::memory_order_release);
                    PostQuitMessage(0);
                    return 0;
                }
            }

            return DefWindowProcW(hwnd, uMsg, wParam, lParam);
        }

        HWND m_hwnd{ nullptr };

        // [C++20] std::atomic<bool> — inline static bool'dan daha güvenli:
        //   (1) thread-safe, (2) her Window instance'ına özel (static değil),
        //   (3) derleyici reorder edemez
        std::atomic<bool> m_shouldClose{ false };
    };

} // namespace Radian::Platform
