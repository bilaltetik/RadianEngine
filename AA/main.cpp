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
#include "IRenderer.h"  

import std;
import Radian.Platform;

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

    template<typename FnPtr>
    [[nodiscard]] FnPtr GetProc(const char* name) const noexcept {
        return reinterpret_cast<FnPtr>(GetProcAddress(m_handle, name));
    }

private:
    HMODULE m_handle = nullptr;
};

int WINAPI WinMain(HINSTANCE, HINSTANCE, PSTR, int) {

    Radian::Platform::Window window;
    const Radian::Platform::WindowConfig config{
        .width = 1280,
        .height = 720,
        .title = L"THE Radian"
    };
    if (!window.Create(config)) return -1;

    PluginLoader plugin{ L"D3D12Renderer.dll" };
    if (!plugin.IsLoaded()) [[unlikely]] {
        MessageBoxW(nullptr, L"D3D12Renderer.dll yüklenemedi.", L"Hata", MB_ICONERROR);
        return -1;
    }

    const auto pfnCreate = plugin.GetProc<PFN_CreateRenderer>("CreateRenderer");
    const auto pfnDestroy = plugin.GetProc<PFN_DestroyRenderer>("DestroyRenderer");

    if (!pfnCreate || !pfnDestroy) [[unlikely]] {
        MessageBoxW(nullptr, L"DLL export'ları bulunamadı.", L"Hata", MB_ICONERROR);
        return -1;
    }

    using RendererPtr = std::unique_ptr<Radian::Renderer::IRenderer, PFN_DestroyRenderer>;

    RendererPtr renderer{ pfnCreate(), pfnDestroy };
    if (!renderer) [[unlikely]] return -1;

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

    using namespace std::chrono;
    using namespace std::chrono_literals;

    auto lastTime = steady_clock::now();
    duration<double> elapsed{};
    uint32_t frameCount = 0;

    constexpr duration<double> titleInterval = 1s;

    while (!window.ShouldClose()) {
        window.ProcessMessages();

        const auto now = steady_clock::now();
        const auto deltaTime = duration<double>(now - lastTime);
        lastTime = now;
        elapsed += deltaTime;
        ++frameCount;

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

        renderer->RenderFrame();
    }

    renderer->Shutdown();

    return 0;
}