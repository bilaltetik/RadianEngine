#pragma once

// IRenderer.h — hem main.exe hem DLL tarafından #include edilir.
// Bu header'ın C++ modüllerine bağımlılığı yoktur; saf Win32 + STL tiplerini kullanır.

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdint>
#include <string>
#include <string_view>
#include <expected>

namespace Radian::Renderer {

    // -------------------------------------------------------------------
    // DLL'e geçirilen başlatma bilgisi.
    // Radian.Platform modülüne bağımlılık olmadan window handle'ını taşır.
    // -------------------------------------------------------------------
    struct RendererCreateInfo {
        HWND        hwnd{};
        uint32_t    width{ 1280 };
        uint32_t    height{ 720 };
        std::wstring title{ L"Radian" };
    };

    // -------------------------------------------------------------------
    // Saf sanal arayüz — DLL ile exe arasındaki tek sözleşme.
    // ABI stabilitesi için virtual dispatch kullanıyoruz.
    // -------------------------------------------------------------------
    class IRenderer {
    public:
        virtual ~IRenderer() = default;

        // [C++23] std::expected — bool döndürmek yerine hata mesajı taşır;
        // [[nodiscard]] ile sessizce yok sayılamaz.
        [[nodiscard]] virtual std::expected<void, std::string>
            Init(const RendererCreateInfo& info) = 0;

        // Her frame'de main döngüsünden çağrılır
        virtual void RenderFrame() = 0;

        // GPU'nun boş kalmasını bekler, kaynakları serbest bırakır
        virtual void Shutdown() = 0;

        // Sorgulama — başlık çubuğu güncellemesi için main.cpp kullanır
        [[nodiscard]] virtual std::wstring_view GetGPUName() const noexcept = 0;
        [[nodiscard]] virtual size_t            GetVRAMMB()  const noexcept = 0;
    };

} // namespace Radian::Renderer

// -------------------------------------------------------------------
// DLL'in dışa aktardığı fabrika fonksiyonlarının pointer tipleri.
// extern "C" linkage sayesinde name-mangling yoktur;
// GetProcAddress ile isim olarak aranabilir.
// -------------------------------------------------------------------
using PFN_CreateRenderer = Radian::Renderer::IRenderer* (*)();
using PFN_DestroyRenderer = void(*)(Radian::Renderer::IRenderer*);