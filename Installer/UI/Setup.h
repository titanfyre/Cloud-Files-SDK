#pragma once
#include <windows.h>
#include <string>
namespace ProductSetup {
struct Result {DWORD error=0;bool reboot=false;std::wstring detail;};
bool Installed() noexcept;
std::wstring Directory();
Result Execute(bool install) noexcept;
}
