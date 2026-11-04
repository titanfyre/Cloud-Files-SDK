#pragma once
#ifndef TITAN_SHARED_SDK_LICENSE_PROTOCOL_V2_INCLUDED
#define TITAN_SHARED_SDK_LICENSE_PROTOCOL_V2_INCLUDED
#ifdef _KERNEL_MODE
#include <ntddk.h>
typedef ULONG TS_U32;
typedef ULONGLONG TS_U64;
typedef UCHAR TS_U8;
#else
#include <windows.h>
#include <winioctl.h>
#include <stdint.h>
typedef uint32_t TS_U32;
typedef uint64_t TS_U64;
typedef uint8_t TS_U8;
#endif
#define TITAN_SDK_MAGIC 0x324C5654u
#define TITAN_SDK_VERSION 2u
#define TITAN_SDK_CLOUD_FILES 7u
#define TITAN_SDK_REMOTE_WORKSTATION 8u
#define TITAN_SDK_USB_OVER_IP 9u
#define TITAN_SDK_CHANGED_BLOCK_TRACKING 10u
#define TITAN_SDK_FULL_BOOT 0xffffffffu
#define TITAN_SDK_DEMO_SECONDS 3600u
#define TITAN_SDK_MODE_NOT_STARTED 0u
#define TITAN_SDK_MODE_DEMO 1u
#define TITAN_SDK_MODE_EXPIRED 2u
#define TITAN_SDK_MODE_FULL 3u
#define TS_IOCTL_STATUS CTL_CODE(FILE_DEVICE_UNKNOWN,0x940,METHOD_BUFFERED,FILE_READ_ACCESS)
#define TS_IOCTL_BEGIN CTL_CODE(FILE_DEVICE_UNKNOWN,0x941,METHOD_BUFFERED,FILE_READ_ACCESS|FILE_WRITE_ACCESS)
#define TS_IOCTL_CHALLENGE CTL_CODE(FILE_DEVICE_UNKNOWN,0x942,METHOD_BUFFERED,FILE_READ_ACCESS|FILE_WRITE_ACCESS)
#define TS_IOCTL_ACTIVATE CTL_CODE(FILE_DEVICE_UNKNOWN,0x943,METHOD_BUFFERED,FILE_READ_ACCESS|FILE_WRITE_ACCESS)
typedef struct TS_PRODUCT { TS_U32 product; TS_U32 reserved; } TS_PRODUCT;
typedef struct TS_CHALLENGE {
 TS_U32 magic,version,product,permission;
 TS_U8 nonce[32];
} TS_CHALLENGE;
typedef struct TS_TOKEN { TS_CHALLENGE grant; TS_U8 signature[64]; } TS_TOKEN;
typedef struct TS_STATUS {
 TS_U32 version,product,mode,reserved;
 TS_U64 remaining_100ns;
 TS_U64 elapsed_100ns;
} TS_STATUS;
#ifdef __cplusplus
static_assert(sizeof(TS_CHALLENGE)==48);
static_assert(sizeof(TS_TOKEN)==112);
static_assert(sizeof(TS_STATUS)==32);
#else
C_ASSERT(sizeof(TS_CHALLENGE)==48);
C_ASSERT(sizeof(TS_TOKEN)==112);
C_ASSERT(sizeof(TS_STATUS)==32);
#endif
#endif // TITAN_SHARED_SDK_LICENSE_PROTOCOL_V2_INCLUDED
