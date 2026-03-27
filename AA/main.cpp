// EXE Projesi -> main.cpp (veya benzeri bir dosya) en üstüne ekle:
extern "C" {
    __declspec(dllexport) extern const unsigned int D3D12SDKVersion = 619;
    __declspec(dllexport) extern const char* D3D12SDKPath = ".\\D3D12\\";
}


#ifndef UNICODE
#define UNICODE
#endif

#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3d12.lib")

#define NOMINMAX
#include <windows.h>
#include "IRenderer.h"  // paylaşılan arayüz

import std;
import Radian.Platform;

// ====================================================================
// PluginLoader — RAII DLL yükleyici
// LoadLibraryW / FreeLibrary çiftini kapsüller;
// kopya yok, taşıma var; GetProc<FnPtr>() ile güvenli proc adresi alınır.
// ====================================================================
class PluginLoader {
public:
    explicit PluginLoader(const wchar_t* path)
        : m_handle(LoadLibraryW(path)) {
    }

    ~PluginLoader() { if (m_handle) FreeLibrary(m_handle); }

    PluginLoader(const PluginLoader&) = delete;
    PluginLoader& operator=(const PluginLoader&) = delete;

    PluginLoader(PluginLoader&& o) noexcept
        : m_handle(std::exchange(o.m_handle, nullptr)) {
    }

    [[nodiscard]] bool IsLoaded() const noexcept { return m_handle != nullptr; }

    // [C++20] Fonksiyon şablonu — herhangi bir fonksiyon pointer tipine cast eder
    template<typename FnPtr>
    [[nodiscard]] FnPtr GetProc(const char* name) const noexcept {
        return reinterpret_cast<FnPtr>(GetProcAddress(m_handle, name));
    }

private:
    HMODULE m_handle = nullptr;
};

// ====================================================================
// WinMain
// ====================================================================
int WINAPI WinMain(HINSTANCE, HINSTANCE, PSTR, int) {

    // ------------------------------------------------------------------
    // 1. Pencere
    // ------------------------------------------------------------------
    Radian::Platform::Window window;
    const Radian::Platform::WindowConfig config{
        .width = 1280,
        .height = 720,
        .title = L"THE Radian"
    };
    if (!window.Create(config)) return -1;

    // ------------------------------------------------------------------
    // 2. Plugin yükle
    // ------------------------------------------------------------------
    PluginLoader plugin{ L"DX12RENDERER.dll" };
    if (!plugin.IsLoaded()) [[unlikely]] {
        MessageBoxW(nullptr, L"DX12RENDERER.dll yüklenemedi.", L"Hata", MB_ICONERROR);
        return -1;
    }

    // Fabrika fonksiyon pointer'larını al
    const auto pfnCreate = plugin.GetProc<PFN_CreateRenderer>("CreateRenderer");
    const auto pfnDestroy = plugin.GetProc<PFN_DestroyRenderer>("DestroyRenderer");

    if (!pfnCreate || !pfnDestroy) [[unlikely]] {
        MessageBoxW(nullptr, L"DLL export'ları bulunamadı.", L"Hata", MB_ICONERROR);
        return -1;
    }

    // ------------------------------------------------------------------
    // 3. Renderer oluştur
    // [C++20] unique_ptr + özel silici — renderer sahibiyiz,
    //         kapsam sonunda pfnDestroy otomatik çağrılır.
    // ------------------------------------------------------------------
    using RendererPtr = std::unique_ptr<Radian::Renderer::IRenderer, PFN_DestroyRenderer>;

    RendererPtr renderer{ pfnCreate(), pfnDestroy };
    if (!renderer) [[unlikely]] return -1;

    // ------------------------------------------------------------------
    // 4. Başlat — DLL içindeki tüm DevicePrepare → PSO zinciri burada
    // [C++23] std::expected monadic: hata varsa mesajıyla birlikte erken çık
    // ------------------------------------------------------------------
    const Radian::Renderer::RendererCreateInfo createInfo{
        .hwnd = window.GetHandle(),
        .width = config.width,
        .height = config.height,
        .title = config.title
    };

    if (auto result = renderer->Init(createInfo); !result) [[unlikely]] {
        const std::wstring msg = std::format(
            L"Renderer başlatılamadı:\n{}",
            std::wstring(result.error().begin(), result.error().end())
        );
        MessageBoxW(nullptr, msg.c_str(), L"Başlatma Hatası", MB_ICONERROR);
        return -1;
    }

    // ------------------------------------------------------------------
    // 5. Ana döngü
    // ------------------------------------------------------------------
    using namespace std::chrono;
    using namespace std::chrono_literals;

    // [C++20] steady_clock — monotonic garantili; geriye gitmez
    auto lastTime = steady_clock::now();
    duration<double> elapsed{};
    uint32_t frameCount = 0;

    constexpr duration<double> titleInterval = 1s;

    while (!window.ShouldClose()) {
        window.ProcessMessages();

        // Delta time
        const auto now = steady_clock::now();
        const auto deltaTime = duration<double>(now - lastTime);
        lastTime = now;
        elapsed += deltaTime;
        ++frameCount;

        // Başlık güncelleme (saniyede bir)
        if (elapsed >= titleInterval) {
            const double fps = frameCount / elapsed.count();
            const double msPerF = 1000.0 / fps;

            const std::wstring title = std::format(
                L"{} | FPS: {:.1f} | MS: {:.2f} | DirectX 12 | GPU: {} ({} MB)",
                config.title, fps, msPerF,
                renderer->GetGPUName(),
                renderer->GetVRAMMB()
            );
            SetWindowTextW(window.GetHandle(), title.c_str());

            frameCount = 0;
            elapsed = {};
        }

        // -- Tek satır: tüm render mantığı DLL içinde --
        renderer->RenderFrame();
    }

    // ------------------------------------------------------------------
    // 6. Temizlik
    // Önce explicit Shutdown (GPU flush + kaynak serbest bırakma),
    // ardından unique_ptr kapsam sonunda pfnDestroy() çağırır.
    // ------------------------------------------------------------------
    renderer->Shutdown();

    return 0;
}