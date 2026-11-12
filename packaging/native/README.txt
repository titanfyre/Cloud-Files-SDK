Cloud-Files-SDK - native SDK source package, version 0.1.1

Contains the existing public user-mode C++ SDK, its local header dependencies and licenses.
This is the Cloud-Files-SDK SDK, not an application installer or a kernel driver package.
Requires Windows x64, a Windows SDK and a C++17 compiler. Runtime driver/access
requirements and demo restrictions remain those of the original product.
No driver installation, activation, service creation or administrator action is performed.
Source repository: https://github.com/titanfyre/Cloud-Files-SDK

Build: cmake -S . -B build -A x64
       cmake --build build --config Release
       cmake --install build --config Release --prefix install
Consume: find_package(CloudFilesSDK CONFIG REQUIRED)
         target_link_libraries(your_target PRIVATE CloudFilesSDK::sdk)
Headers preserve the original SDK layout; API signatures and license checks are unchanged.

Publication requires registry/repository ownership and a verified downloadable source archive.
Preparation does not mean acceptance into vcpkg, Conan Center or publication on NuGet.org.
