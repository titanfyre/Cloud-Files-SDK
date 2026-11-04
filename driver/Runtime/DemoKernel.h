#pragma once
#include <ntddk.h>
#include "LicenseProtocol.h"
#define TS_RUNTIME_DEVICE L"\\Device\\TitanSdkDemoRuntime1"
#define TS_RUNTIME_EXTENSION ((PVOID)(ULONG_PTR)0x54534431u)
typedef NTSTATUS (*TS_BEGIN_FUNCTION)(ULONG product);
typedef BOOLEAN (*TS_ALLOWED_FUNCTION)(ULONG product);
typedef NTSTATUS (*TS_STATUS_FUNCTION)(ULONG product,TS_STATUS* result);
typedef struct TS_RUNTIME_IDENTITY {
 ULONG size,version;
 PDRIVER_OBJECT owner;
 PDEVICE_OBJECT device;
 TS_BEGIN_FUNCTION begin;
 TS_ALLOWED_FUNCTION allowed;
 TS_STATUS_FUNCTION status;
} TS_RUNTIME_IDENTITY;
typedef struct TS_RUNTIME_CLIENT { PFILE_OBJECT file; const TS_RUNTIME_IDENTITY* api; } TS_RUNTIME_CLIENT;
_IRQL_requires_(PASSIVE_LEVEL)
static __inline void TsRuntimeClose(TS_RUNTIME_CLIENT* client){
 if(client->file)ObDereferenceObject(client->file);
 client->file=NULL;client->api=NULL;
}
_IRQL_requires_(PASSIVE_LEVEL)
static __inline NTSTATUS TsRuntimeConnect(TS_RUNTIME_CLIENT* client){
 PDEVICE_OBJECT device=NULL;NTSTATUS status;const TS_RUNTIME_IDENTITY* api;
 DECLARE_CONST_UNICODE_STRING(name,TS_RUNTIME_DEVICE);
 if(client->file&&client->api)return STATUS_SUCCESS;
 status=IoGetDeviceObjectPointer((PUNICODE_STRING)&name,FILE_READ_DATA|FILE_WRITE_DATA,&client->file,&device);
 if(!NT_SUCCESS(status)){
  DECLARE_CONST_UNICODE_STRING(service,L"\\Registry\\Machine\\System\\CurrentControlSet\\Services\\TitanSdkDemoRuntime");
  (void)ZwLoadDriver((PUNICODE_STRING)&service);
  status=IoGetDeviceObjectPointer((PUNICODE_STRING)&name,FILE_READ_DATA|FILE_WRITE_DATA,&client->file,&device);
 }
 if(!NT_SUCCESS(status)){client->file=NULL;client->api=NULL;return status;}
 api=(const TS_RUNTIME_IDENTITY*)IoGetDriverObjectExtension(device->DriverObject,TS_RUNTIME_EXTENSION);
 if(!api||api->size!=sizeof(*api)||api->version!=TITAN_SDK_VERSION||api->owner!=device->DriverObject||api->device!=device||!api->begin||!api->allowed||!api->status){
  TsRuntimeClose(client);return STATUS_OBJECT_TYPE_MISMATCH;
 }
 client->api=api;return STATUS_SUCCESS;
}
_IRQL_requires_max_(DISPATCH_LEVEL)
static __inline NTSTATUS TsRuntimeBegin(TS_RUNTIME_CLIENT* client,ULONG product){return client->api?client->api->begin(product):STATUS_DEVICE_NOT_READY;}
_IRQL_requires_max_(DISPATCH_LEVEL)
static __inline BOOLEAN TsRuntimeAllowed(const TS_RUNTIME_CLIENT* client,ULONG product){return client->api?client->api->allowed(product):FALSE;}
_IRQL_requires_max_(DISPATCH_LEVEL)
static __inline NTSTATUS TsRuntimeStatus(const TS_RUNTIME_CLIENT* client,ULONG product,TS_STATUS* result){return client->api?client->api->status(product,result):STATUS_DEVICE_NOT_READY;}
