#pragma once
#include <windows.h>
#include <string>
namespace RuntimeSetup {
struct Result { DWORD error=0; bool reboot=false; std::wstring detail; };
bool Installed() noexcept;
Result Install() noexcept;
}
