#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "License.h"
#include <winsvc.h>
#pragma comment(lib,"advapi32.lib")

namespace {
struct Service {
 SC_HANDLE value=nullptr;
 explicit Service(SC_HANDLE handle):value(handle){}
 ~Service(){if(value)CloseServiceHandle(value);}
 Service(const Service&)=delete;Service& operator=(const Service&)=delete;
};
struct Device {
 HANDLE value=INVALID_HANDLE_VALUE;
 DWORD error=ERROR_SUCCESS;
 const char* stage="Opening the SDK runtime";
 static HANDLE Open(){return CreateFileW(LR"(\\.\TitanSdkDemoRuntime1)",GENERIC_READ|GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,0,nullptr);}
 Device(){
  value=Open();if(value!=INVALID_HANDLE_VALUE)return;
  error=GetLastError();
  // No SCM work on the ordinary input path, and never reinterpret an access
  // denial or incompatible device as permission to change machine policy.
  if(error!=ERROR_FILE_NOT_FOUND&&error!=ERROR_PATH_NOT_FOUND)return;
  // The poll/retry budget is 10 seconds. Synchronous Windows SCM/RPC calls
  // retain the operating system's own blocking and scheduling guarantees.
  const ULONGLONG deadline=GetTickCount64()+10000;
  stage="Connecting to Windows Service Control Manager for the installed SDK runtime";
  Service manager(OpenSCManagerW(nullptr,nullptr,SC_MANAGER_CONNECT));
  if(!manager.value){error=GetLastError();return;}
  stage="Opening the installed TitanSdkDemoRuntime service";
  Service service(OpenServiceW(manager.value,L"TitanSdkDemoRuntime",SERVICE_QUERY_STATUS|SERVICE_START));
  if(!service.value){error=GetLastError();return;}
  bool started=false;
  for(;;){
   if(GetTickCount64()>=deadline){stage="Waiting up to 10 seconds for the installed SDK runtime";error=ERROR_TIMEOUT;return;}
   SERVICE_STATUS_PROCESS state{};DWORD bytes=0;
   stage="Reading TitanSdkDemoRuntime startup state";
   if(!QueryServiceStatusEx(service.value,SC_STATUS_PROCESS_INFO,reinterpret_cast<LPBYTE>(&state),sizeof(state),&bytes)){error=GetLastError();return;}
   if(state.dwServiceType!=SERVICE_KERNEL_DRIVER){stage="Validating the installed TitanSdkDemoRuntime kernel service identity";error=ERROR_INVALID_DATA;return;}
   if(state.dwCurrentState==SERVICE_RUNNING){
    stage="Opening the started SDK runtime";value=Open();
    if(value!=INVALID_HANDLE_VALUE){error=ERROR_SUCCESS;return;}
    error=GetLastError();if(error!=ERROR_FILE_NOT_FOUND&&error!=ERROR_PATH_NOT_FOUND)return;
   }else if(state.dwCurrentState==SERVICE_STOPPED){
    if(started){stage="Starting the installed SDK runtime";error=state.dwWin32ExitCode?state.dwWin32ExitCode:ERROR_SERVICE_NOT_ACTIVE;return;}
    stage="Starting the installed TitanSdkDemoRuntime service";
    if(!StartServiceW(service.value,0,nullptr)){
     const DWORD startError=GetLastError();if(startError!=ERROR_SERVICE_ALREADY_RUNNING){error=startError;return;}
    }
    started=true;continue;
   }else if(state.dwCurrentState!=SERVICE_START_PENDING&&state.dwCurrentState!=SERVICE_STOP_PENDING){
    stage="Waiting for a usable TitanSdkDemoRuntime service state";error=ERROR_SERVICE_CANNOT_ACCEPT_CTRL;return;
   }else if(state.dwCurrentState==SERVICE_START_PENDING){started=true;}
   const ULONGLONG now=GetTickCount64();
   if(now>=deadline){stage="Waiting up to 10 seconds for the installed SDK runtime";error=ERROR_TIMEOUT;return;}
   const DWORD delay=static_cast<DWORD>((deadline-now)<100?(deadline-now):100);
   Sleep(delay);
  }
 }
 ~Device(){if(value!=INVALID_HANDLE_VALUE)CloseHandle(value);}
 Device(const Device&)=delete;Device& operator=(const Device&)=delete;
};
std::string DeviceFailure(const char* stage,DWORD error){
 std::string message=std::string(stage)+" failed (Windows error "+std::to_string(error)+").";
 if(error==ERROR_FILE_NOT_FOUND||error==ERROR_PATH_NOT_FOUND||error==ERROR_SERVICE_DOES_NOT_EXIST)
  message+=" Install the matching Microsoft-signed Titan SDK Runtime using this product's setup.";
 else if(error==ERROR_ACCESS_DENIED)message+=" Run the application as administrator.";
 else if(error==ERROR_TIMEOUT)message+=" The installed runtime did not become available within its 10-second startup wait. Retry, or inspect the Windows service error before reinstalling the matching signed package.";
 return message;
}
std::string Query(unsigned product,DWORD operation,TS_STATUS& status){
 status={};if(product<7||product>10)return "Unknown SDK product.";
  Device device;if(device.value==INVALID_HANDLE_VALUE)return DeviceFailure(device.stage,device.error);
 TS_PRODUCT input{product,0};DWORD returned=0;
 if(!DeviceIoControl(device.value,operation,&input,sizeof(input),&status,sizeof(status),&returned,nullptr)){
  DWORD error=GetLastError();
  if(operation==TS_IOCTL_BEGIN){TS_STATUS state{};DWORD count=0;
   if(DeviceIoControl(device.value,TS_IOCTL_STATUS,&input,sizeof(input),&state,sizeof(state),&count,nullptr)&&count==sizeof(state)&&state.mode==TITAN_SDK_MODE_EXPIRED){status=state;return "The one-hour demo has expired for this Windows boot. Restart Windows to begin a new demo; activation is unavailable in this demo-only variant.";}
  }
  return DeviceFailure(operation==TS_IOCTL_BEGIN?"Starting SDK demo access":"Reading SDK access state",error);
 }
 if(returned!=sizeof(status)||status.version!=TITAN_SDK_VERSION||status.product!=product||status.mode>TITAN_SDK_MODE_EXPIRED||status.reserved)return "The SDK runtime returned an incompatible or malformed access state.";
 return {};
}
}
namespace TitanSdk {
std::string Begin(unsigned product,TS_STATUS& status){return Query(product,TS_IOCTL_BEGIN,status);}
std::string Status(unsigned product,TS_STATUS& status){return Query(product,TS_IOCTL_STATUS,status);}
std::string Verification(unsigned product,std::string_view key){
 (void)key;
 if(product<7||product>10)return "Unknown SDK product.";
 return "Full activation is unavailable in this demo-only variant.";
}
}
