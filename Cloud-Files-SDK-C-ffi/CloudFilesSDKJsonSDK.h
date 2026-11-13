/* SPDX-License-Identifier: MIT */
#ifndef CLOUD_FILES_SDK_CSDK_H
#define CLOUD_FILES_SDK_CSDK_H
#include <stdint.h>
#if !defined(_WIN32) || !defined(_WIN64)
#error This SDK requires Windows x64.
#endif
#ifdef __cplusplus
#define CLOUD_FILES_SDK_CSDK_NOEXCEPT noexcept
extern "C" {
#else
#define CLOUD_FILES_SDK_CSDK_NOEXCEPT
#endif
#if defined(CLOUD_FILES_SDK_CSDK_BUILD)
#define CLOUD_FILES_SDK_CSDK_API __declspec(dllexport)
#else
#define CLOUD_FILES_SDK_CSDK_API __declspec(dllimport)
#endif
/* open is inert. One active session per loaded product DLL.
   call accepts length-delimited UTF-8 JSON and returns an allocated UTF-8
   response, or NULL on allocation failure. Always free with this same DLL.
   Session/operation handles are scoped to the client that returned them. */
CLOUD_FILES_SDK_CSDK_API uint32_t cloud_files_sdk_json_open(uint64_t *session) CLOUD_FILES_SDK_CSDK_NOEXCEPT;
CLOUD_FILES_SDK_CSDK_API uint32_t cloud_files_sdk_json_close(uint64_t session) CLOUD_FILES_SDK_CSDK_NOEXCEPT;
CLOUD_FILES_SDK_CSDK_API char *cloud_files_sdk_json_call(uint64_t session,const char *request,uint32_t length) CLOUD_FILES_SDK_CSDK_NOEXCEPT;
CLOUD_FILES_SDK_CSDK_API void cloud_files_sdk_json_free(char *response) CLOUD_FILES_SDK_CSDK_NOEXCEPT;
#ifdef __cplusplus
}
#endif
#undef CLOUD_FILES_SDK_CSDK_NOEXCEPT
#endif
