#pragma once

#include <Windows.h>

namespace single_module {

HMODULE LoadSystemModule(const wchar_t* basename) noexcept;
void Log(const wchar_t* text) noexcept;

}  // namespace single_module
