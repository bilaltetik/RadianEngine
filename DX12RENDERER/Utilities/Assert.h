#pragma once
#include <assert.h>
#include <sstream>

#ifndef HR_ASSERT
#define HR_ASSERT(hrcall)                                                     \
    do {                                                                      \
        HRESULT _hr = (hrcall);                                               \
        if (FAILED(_hr)) {                                                    \
            std::stringstream _ss;                                            \
            _ss << "HRESULT FAILED!\n\n"                                      \
                << "File: " << __FILE__ << "\n"                               \
                << "Line: " << __LINE__ << "\n"                               \
                << "Call: " << #hrcall << "\n"                                \
                << "HRESULT: 0x" << std::hex << _hr;                          \
                                                                              \
            MessageBoxA(nullptr, _ss.str().c_str(), "DX12 ERROR", MB_OK);     \
            __debugbreak();                                                   \
        }                                                                     \
    } while (0)
#endif