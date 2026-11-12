cmake_minimum_required(VERSION 3.24)
project(CloudFilesSDK VERSION 0.1.1 LANGUAGES C CXX)
if(NOT WIN32 OR NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
  message(FATAL_ERROR "This SDK requires Windows x64. No driver is installed by this build.")
endif()
include(GNUInstallDirs)
include(CMakePackageConfigHelpers)
add_library(TitanCloudFilesSDK STATIC
  "sdk/SDK/CloudFiles.cpp"
  "sdk/SDK/License.cpp"
)
add_library(CloudFilesSDK::sdk ALIAS TitanCloudFilesSDK)
set_target_properties(TitanCloudFilesSDK PROPERTIES EXPORT_NAME sdk)
target_compile_features(TitanCloudFilesSDK PUBLIC cxx_std_17)
target_compile_definitions(TitanCloudFilesSDK PUBLIC )
target_compile_definitions(TitanCloudFilesSDK PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN _WIN32_WINNT=0x0A00)
if(MSVC)
  target_compile_options(TitanCloudFilesSDK PRIVATE /utf-8 /EHsc /W4)
endif()
target_include_directories(TitanCloudFilesSDK PUBLIC
  "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/sdk>"
  "$<INSTALL_INTERFACE:include/>"
  "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/sdk/SDK>"
  "$<INSTALL_INTERFACE:include/SDK>"
)
target_link_libraries(TitanCloudFilesSDK PUBLIC advapi32 bcrypt cldapi comctl32 crypt32 gdi32 newdev ole32 setupapi shell32 user32 winhttp wintrust)
install(TARGETS TitanCloudFilesSDK EXPORT CloudFilesSDKTargets ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(DIRECTORY sdk/ DESTINATION include FILES_MATCHING PATTERN "*.h" PATTERN "*.hpp" PATTERN "*.inl")
install(FILES LICENSE DESTINATION share/cloud-files-sdk)
install(EXPORT CloudFilesSDKTargets NAMESPACE CloudFilesSDK:: DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/CloudFilesSDK)
configure_file("CloudFilesSDKConfig.cmake.in" "${CMAKE_CURRENT_BINARY_DIR}/CloudFilesSDKConfig.cmake" @ONLY)
write_basic_package_version_file("${CMAKE_CURRENT_BINARY_DIR}/CloudFilesSDKConfigVersion.cmake" VERSION 0.1.1 COMPATIBILITY SameMajorVersion)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/CloudFilesSDKConfig.cmake" "${CMAKE_CURRENT_BINARY_DIR}/CloudFilesSDKConfigVersion.cmake" DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/CloudFilesSDK)
