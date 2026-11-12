vcpkg_download_distfile(ARCHIVE
    URLS "https://github.com/titanfyre/Cloud-Files-SDK/releases/download/sdk-v0.1.1/cloud-files-sdk-sdk-0.1.1.tar.gz"
    FILENAME "cloud-files-sdk-sdk-0.1.1.tar.gz"
    SHA512 4db29b2b3fa5dec21df7f767a7c49f6f46ad15f938c4a3915957bf6c2dacf4cbe01ea9da8ded02765f19a2b4dfb6ef1ce08a945f6dedc31d65bc6a966275d06d
)
vcpkg_extract_source_archive(SOURCE_PATH ARCHIVE "${ARCHIVE}" NO_REMOVE_ONE_LEVEL)
vcpkg_cmake_configure(SOURCE_PATH "${SOURCE_PATH}")
vcpkg_cmake_install()
vcpkg_cmake_config_fixup(PACKAGE_NAME CloudFilesSDK CONFIG_PATH lib/cmake/CloudFilesSDK)
file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/debug/include" "${CURRENT_PACKAGES_DIR}/debug/share")
vcpkg_install_copyright(FILE_LIST "${SOURCE_PATH}/LICENSE")
