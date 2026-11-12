#include <SDK/CloudFiles.h>
#include <vector>

int main() {
    CloudFiles::Backend backend;
    backend.identity = L"conan-package-test";
    backend.read = [](std::wstring_view, uint64_t, std::vector<uint8_t>&, HANDLE) {};
    if (!CloudFiles::Validate(backend).empty()) return 1;
    CloudFiles::Entry unsafe;
    unsafe.path = L"..\\escape";
    backend.entries.push_back(unsafe);
    // Only pure catalog validation is exercised. No sync root or driver is started.
    return CloudFiles::Validate(backend).empty() ? 2 : 0;
}
