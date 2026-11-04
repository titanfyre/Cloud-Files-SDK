#include <ntddk.h>
#include <wdmsec.h>
#include "DemoKernel.h"

#define TS_POOL_TAG '2RST'
#define TS_SLOT_COUNT 4u
#ifdef TS_RUNTIME_LAB_SECONDS
#if TS_RUNTIME_LAB_SECONDS < 1 || TS_RUNTIME_LAB_SECONDS > 120
#error Private runtime lab duration must be between 1 and 120 seconds.
#endif
#define TS_DEMO_SECONDS TS_RUNTIME_LAB_SECONDS
#else
#define TS_DEMO_SECONDS TITAN_SDK_DEMO_SECONDS
#endif
typedef __declspec(align(8)) struct TS_SLOT { volatile LONG64 first; } TS_SLOT;
typedef struct TS_FILE { EX_PUSH_LOCK lock; BOOLEAN closing; } TS_FILE;
static TS_SLOT slots[TS_SLOT_COUNT];
static BOOLEAN Product(ULONG product){return product>=7&&product<=10;}
static ULONGLONG Now(void){ULONGLONG qpc;return KeQueryInterruptTimePrecise(&qpc);}
static NTSTATUS Status(ULONG product,TS_STATUS* result){
 ULONGLONG first,now,elapsed;
 if(!Product(product)||!result)return STATUS_INVALID_PARAMETER;
 RtlZeroMemory(result,sizeof(*result));result->version=TITAN_SDK_VERSION;result->product=product;
 first=(ULONGLONG)InterlockedCompareExchange64(&slots[product-7].first,0,0);
 now=Now();elapsed=first&&now>=first?now-first:0;result->elapsed_100ns=elapsed;
 if(!first){result->mode=TITAN_SDK_MODE_NOT_STARTED;result->remaining_100ns=(ULONGLONG)TS_DEMO_SECONDS*10000000;return STATUS_SUCCESS;}
 if(now<first||elapsed>=(ULONGLONG)TS_DEMO_SECONDS*10000000){result->mode=TITAN_SDK_MODE_EXPIRED;return STATUS_SUCCESS;}
 result->mode=TITAN_SDK_MODE_DEMO;result->remaining_100ns=(ULONGLONG)TS_DEMO_SECONDS*10000000-elapsed;return STATUS_SUCCESS;
}
static BOOLEAN Allowed(ULONG product){
 ULONGLONG first,now;
 if(!Product(product))return FALSE;
 first=(ULONGLONG)InterlockedCompareExchange64(&slots[product-7].first,0,0);
 now=Now();
 return first&&now>=first&&now-first<(ULONGLONG)TS_DEMO_SECONDS*10000000;
}
static NTSTATUS Begin(ULONG product){
 ULONGLONG now;
 if(!Product(product))return STATUS_INVALID_PARAMETER;
 now=Now();if(!now)now=1;
 (void)InterlockedCompareExchange64(&slots[product-7].first,(LONG64)now,0);
 return Allowed(product)?STATUS_SUCCESS:STATUS_LICENSE_VIOLATION;
}
DRIVER_INITIALIZE DriverEntry;
_Dispatch_type_(IRP_MJ_CREATE)
_Dispatch_type_(IRP_MJ_CLOSE)
_Dispatch_type_(IRP_MJ_CLEANUP)
_Dispatch_type_(IRP_MJ_DEVICE_CONTROL)
DRIVER_DISPATCH Dispatch;
NTSTATUS Dispatch(PDEVICE_OBJECT device,PIRP irp){
 PIO_STACK_LOCATION stack=IoGetCurrentIrpStackLocation(irp);
 TS_FILE* file=stack->FileObject?(TS_FILE*)stack->FileObject->FsContext:NULL;
 NTSTATUS status=STATUS_INVALID_DEVICE_REQUEST;ULONG_PTR bytes=0;
 UNREFERENCED_PARAMETER(device);
 if(KeGetCurrentIrql()!=PASSIVE_LEVEL||!stack->FileObject){status=STATUS_INVALID_DEVICE_STATE;goto Complete;}
 if(stack->MajorFunction==IRP_MJ_CREATE){
  file=(TS_FILE*)ExAllocatePool2(POOL_FLAG_NON_PAGED,sizeof(*file),TS_POOL_TAG);
  if(!file)status=STATUS_INSUFFICIENT_RESOURCES;
  else{ExInitializePushLock(&file->lock);stack->FileObject->FsContext=file;status=STATUS_SUCCESS;}
 }else if(stack->MajorFunction==IRP_MJ_CLEANUP){
  if(file){KeEnterCriticalRegion();ExAcquirePushLockExclusive(&file->lock);file->closing=TRUE;ExReleasePushLockExclusive(&file->lock);KeLeaveCriticalRegion();}
  status=STATUS_SUCCESS;
 }else if(stack->MajorFunction==IRP_MJ_CLOSE){
  if(file){RtlSecureZeroMemory(file,sizeof(*file));ExFreePoolWithTag(file,TS_POOL_TAG);stack->FileObject->FsContext=NULL;}
  status=STATUS_SUCCESS;
 }else if(stack->MajorFunction==IRP_MJ_DEVICE_CONTROL&&file){
  ULONG code=stack->Parameters.DeviceIoControl.IoControlCode;
  // ABI operation numbers remain reserved, but this variant never issues or
  // accepts activation material, regardless of buffer or caller contents.
  if(code==TS_IOCTL_CHALLENGE||code==TS_IOCTL_ACTIVATE)status=STATUS_NOT_SUPPORTED;
  else if(code==TS_IOCTL_STATUS||code==TS_IOCTL_BEGIN){
   TS_PRODUCT request;
   status=IoValidateDeviceIoControlAccess(irp,code==TS_IOCTL_STATUS?FILE_READ_DATA:FILE_READ_DATA|FILE_WRITE_DATA);
   if(NT_SUCCESS(status)){
    if(!irp->AssociatedIrp.SystemBuffer)status=STATUS_INVALID_USER_BUFFER;
    else if(stack->Parameters.DeviceIoControl.InputBufferLength!=sizeof(request)||stack->Parameters.DeviceIoControl.OutputBufferLength<sizeof(TS_STATUS))status=STATUS_INVALID_BUFFER_SIZE;
    else{
     RtlCopyMemory(&request,irp->AssociatedIrp.SystemBuffer,sizeof(request));
     if(!Product(request.product)||request.reserved)status=STATUS_INVALID_PARAMETER;
     else{
      KeEnterCriticalRegion();ExAcquirePushLockExclusive(&file->lock);
      if(file->closing)status=STATUS_FILE_CLOSED;
      else{
       TS_STATUS result;status=code==TS_IOCTL_BEGIN?Begin(request.product):STATUS_SUCCESS;
       if(NT_SUCCESS(status)){status=Status(request.product,&result);if(NT_SUCCESS(status)){RtlCopyMemory(irp->AssociatedIrp.SystemBuffer,&result,sizeof(result));bytes=sizeof(result);}}
      }
      ExReleasePushLockExclusive(&file->lock);KeLeaveCriticalRegion();
     }
    }
   }
  }
 }
Complete:
 irp->IoStatus.Status=status;irp->IoStatus.Information=bytes;IoCompleteRequest(irp,IO_NO_INCREMENT);return status;
}
NTSTATUS DriverEntry(PDRIVER_OBJECT driver,PUNICODE_STRING registry){
 PDEVICE_OBJECT device=NULL;TS_RUNTIME_IDENTITY* identity=NULL;NTSTATUS status;
 const GUID cls={0x6aa724f2,0xc16b,0x49a1,{0x94,0xc1,0x64,0x73,0x12,0xa3,0x46,0x5f}};
 DECLARE_CONST_UNICODE_STRING(name,TS_RUNTIME_DEVICE);
 DECLARE_CONST_UNICODE_STRING(link,L"\\DosDevices\\TitanSdkDemoRuntime1");
 DECLARE_CONST_UNICODE_STRING(security,L"D:P(A;;GA;;;SY)(A;;GA;;;BA)");
 UNREFERENCED_PARAMETER(registry);
 driver->MajorFunction[IRP_MJ_CREATE]=Dispatch;driver->MajorFunction[IRP_MJ_CLOSE]=Dispatch;driver->MajorFunction[IRP_MJ_CLEANUP]=Dispatch;driver->MajorFunction[IRP_MJ_DEVICE_CONTROL]=Dispatch;
 driver->DriverUnload=NULL;
 status=IoAllocateDriverObjectExtension(driver,TS_RUNTIME_EXTENSION,sizeof(*identity),(PVOID*)&identity);if(!NT_SUCCESS(status))return status;
 status=IoCreateDeviceSecure(driver,0,(PUNICODE_STRING)&name,FILE_DEVICE_UNKNOWN,FILE_DEVICE_SECURE_OPEN,FALSE,(PUNICODE_STRING)&security,&cls,&device);if(!NT_SUCCESS(status))return status;
 status=IoCreateSymbolicLink((PUNICODE_STRING)&link,(PUNICODE_STRING)&name);if(!NT_SUCCESS(status)){IoDeleteDevice(device);return status;}
 identity->size=sizeof(*identity);identity->version=TITAN_SDK_VERSION;identity->owner=driver;identity->device=device;identity->begin=Begin;identity->allowed=Allowed;identity->status=Status;
 device->Flags|=DO_BUFFERED_IO;device->Flags&=~DO_DEVICE_INITIALIZING;return STATUS_SUCCESS;
}
