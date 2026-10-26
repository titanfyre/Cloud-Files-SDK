#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "License.h"
#include <winhttp.h>
#include <wincrypt.h>
#include <winsvc.h>
#include <array>
#include <cstring>
#include <utility>
#pragma comment(lib,"winhttp.lib")
#pragma comment(lib,"crypt32.lib")
#pragma comment(lib,"advapi32.lib")

namespace {
struct Http {
    HINTERNET value = nullptr;
    explicit Http(HINTERNET handle) : value(handle) {}
    ~Http() { if (value) WinHttpCloseHandle(value); }
    Http(const Http&) = delete;
    Http& operator=(const Http&) = delete;
};
std::string Failure(const char* stage, DWORD error = GetLastError()) {
    return std::string(stage) + " failed (Windows error " +
        std::to_string(error) + ").";
}
struct ActivationDeadline {
    const ULONGLONG end = GetTickCount64() + 30000;
    std::string Check(const char* stage) const {
        if (GetTickCount64() < end) return {};
        return std::string("Activation exceeded its 30-second deadline during ") +
            stage + ". Full access was not confirmed. Check the network and retry.";
    }
    std::string Timeout(HINTERNET request, DWORD option, DWORD ceiling) const {
        auto error = Check("HTTPS timeout setup"); if (!error.empty()) return error;
        const ULONGLONG left = end - GetTickCount64();
        const DWORD milliseconds = static_cast<DWORD>(left < ceiling ? left : ceiling);
        // Zero means infinite to WinHTTP. Recheck instead of ever installing it.
        if (!milliseconds) return Check("HTTPS timeout setup");
        DWORD value = milliseconds;
        if (!WinHttpSetOption(request, option, &value, sizeof(value)))
            return Failure("Setting bounded HTTPS timeouts");
        return Check("HTTPS timeout setup");
    }
};
struct WipeString {
    std::string& value;
    ~WipeString() { SecureZeroMemory(value.data(), value.size()); }
};
bool ValidKey(std::string_view key) {
    if (key.size() != 81 || key.substr(0, 5) != "app1_" || key[37] != '_')
        return false;
    for (size_t i = 5; i != 37; ++i)
        if (!((key[i] >= '0' && key[i] <= '9') ||
              (key[i] >= 'a' && key[i] <= 'f'))) return false;
    for (size_t i = 38; i != key.size(); ++i)
        if (!((key[i] >= 'a' && key[i] <= 'z') ||
              (key[i] >= 'A' && key[i] <= 'Z') ||
              (key[i] >= '0' && key[i] <= '9') ||
               key[i] == '_' || key[i] == '-')) return false;
    return true;
}
}

std::string RequestActivation(std::string_view key,
    const TS_CHALLENGE& challenge,
    TS_TOKEN& token) {
    token = {};
    if (!ValidKey(key)) return "Enter a valid application access key (app1_...).";
    if (challenge.magic != TITAN_SDK_MAGIC || challenge.version != TITAN_SDK_VERSION || challenge.permission != 0 || challenge.product < 7 || challenge.product > 10)
        return "The runtime returned an invalid activation challenge.";
    const char* const products[]={"cloud-files-sdk","remote-workstation-sdk","usb-over-ip-sdk","changed-block-tracking-sdk"};
    const char* sku=products[challenge.product-7];
    std::string nonce;
    constexpr char hex[] = "0123456789abcdef";
    for (unsigned char value : challenge.nonce) {
        nonce += hex[value >> 4]; nonce += hex[value & 15];
    }
    std::string body = "{\"key\":\"" + std::string(key) +
        "\",\"sku\":\"" + sku + "\",\"nonce\":\"" + nonce + "\"}";
    WipeString wipe{body};
    const ActivationDeadline deadline;
    auto timed = deadline.Check("HTTPS initialization"); if (!timed.empty()) return timed;
    Http session(WinHttpOpen(L"Titan SDK Activation/0.1",
        WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME,
        WINHTTP_NO_PROXY_BYPASS, 0));
    const DWORD openError = session.value ? ERROR_SUCCESS : GetLastError();
    timed = deadline.Check("HTTPS initialization"); if (!timed.empty()) return timed;
    if (!session.value) return Failure("HTTPS initialization", openError);
    if (!WinHttpSetTimeouts(session.value, 5000, 5000, 10000, 10000)) return Failure("Setting HTTPS session timeouts");
    timed = deadline.Check("HTTPS connection setup"); if (!timed.empty()) return timed;
    Http connection(WinHttpConnect(session.value, L"licences.configcsa.com",
        INTERNET_DEFAULT_HTTPS_PORT, 0));
    const DWORD connectError = connection.value ? ERROR_SUCCESS : GetLastError();
    timed = deadline.Check("HTTPS connection setup"); if (!timed.empty()) return timed;
    if (!connection.value) return Failure("HTTPS connection", connectError);
    timed = deadline.Check("HTTPS request setup"); if (!timed.empty()) return timed;
    Http request(WinHttpOpenRequest(connection.value, L"POST",
        L"/portfolio/api/v2/sdk-activate", nullptr, WINHTTP_NO_REFERER,
        WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE));
    const DWORD requestError = request.value ? ERROR_SUCCESS : GetLastError();
    timed = deadline.Check("HTTPS request setup"); if (!timed.empty()) return timed;
    if (!request.value) return Failure("Authorization request", requestError);
    timed = deadline.Check("HTTPS request policy"); if (!timed.empty()) return timed;
    DWORD disabled = WINHTTP_DISABLE_REDIRECTS | WINHTTP_DISABLE_COOKIES;
    if (!WinHttpSetOption(request.value, WINHTTP_OPTION_DISABLE_FEATURE,
        &disabled, sizeof(disabled))) {
        SecureZeroMemory(body.data(), body.size()); return Failure("HTTPS policy");
    }
    timed = deadline.Check("HTTPS request policy"); if (!timed.empty()) return timed;
    DWORD retries = 1;
    if (!WinHttpSetOption(request.value, WINHTTP_OPTION_CONNECT_RETRIES, &retries, sizeof(retries)))
        return Failure("Setting HTTPS connection retry policy");
    for (DWORD option : {WINHTTP_OPTION_RESOLVE_TIMEOUT, WINHTTP_OPTION_CONNECT_TIMEOUT,
                         WINHTTP_OPTION_SEND_TIMEOUT, WINHTTP_OPTION_RECEIVE_TIMEOUT}) {
        timed = deadline.Timeout(request.value, option,
            option == WINHTTP_OPTION_RESOLVE_TIMEOUT || option == WINHTTP_OPTION_CONNECT_TIMEOUT ? 5000u : 10000u);
        if (!timed.empty()) return timed;
    }
    timed = deadline.Check("sending the authorization request"); if (!timed.empty()) return timed;
    const BOOL sent = WinHttpSendRequest(request.value,
        L"Content-Type: application/json\r\n", static_cast<DWORD>(-1),
        body.data(), static_cast<DWORD>(body.size()),
        static_cast<DWORD>(body.size()), 0);
    const DWORD sendError = sent ? ERROR_SUCCESS : GetLastError();
    SecureZeroMemory(body.data(), body.size());
    timed = deadline.Check("sending the authorization request"); if (!timed.empty()) return timed;
    if (!sent) return "Authorization request failed (Windows error " + std::to_string(sendError) + ").";
    // WinHTTP's ordinary receive timeout covers partial socket data, not all
    // headers. Bound header completion separately so a header drip cannot keep
    // one synchronous receive alive indefinitely. OS DNS/proxy/Schannel work
    // retains WinHTTP's documented scheduling/timeout limits; this is not a
    // promise of a universal hard wall-clock bound for those components.
    timed = deadline.Timeout(request.value, WINHTTP_OPTION_RECEIVE_TIMEOUT, 10000); if (!timed.empty()) return timed;
    timed = deadline.Timeout(request.value, WINHTTP_OPTION_RECEIVE_RESPONSE_TIMEOUT, 30000); if (!timed.empty()) return timed;
    timed = deadline.Check("receiving authorization headers"); if (!timed.empty()) return timed;
    const BOOL received = WinHttpReceiveResponse(request.value, nullptr);
    const DWORD receiveError = received ? ERROR_SUCCESS : GetLastError();
    timed = deadline.Check("receiving authorization headers"); if (!timed.empty()) return timed;
    if (!received) return "Authorization response failed (Windows error " + std::to_string(receiveError) + ").";
    DWORD status = 0, length = sizeof(status);
    timed = deadline.Check("reading HTTP status"); if (!timed.empty()) return timed;
    const BOOL queried = WinHttpQueryHeaders(request.value, WINHTTP_QUERY_STATUS_CODE |
        WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &status,
        &length, WINHTTP_NO_HEADER_INDEX);
    const DWORD queryError = queried ? ERROR_SUCCESS : GetLastError();
    timed = deadline.Check("reading HTTP status"); if (!timed.empty()) return timed;
    if (!queried) return "HTTP status failed (Windows error " + std::to_string(queryError) + ").";
    std::string response;
    std::array<char, 1024> chunk{};
    for (;;) {
        DWORD count = 0;
        timed = deadline.Timeout(request.value, WINHTTP_OPTION_RECEIVE_TIMEOUT, 10000); if (!timed.empty()) return timed;
        timed = deadline.Check("reading authorization response"); if (!timed.empty()) return timed;
        const BOOL read = WinHttpReadData(request.value, chunk.data(), static_cast<DWORD>(chunk.size()), &count);
        const DWORD readError = read ? ERROR_SUCCESS : GetLastError();
        timed = deadline.Check("reading authorization response"); if (!timed.empty()) return timed;
        if (!read) return "Response read failed (Windows error " + std::to_string(readError) + ").";
        if (!count) break;
        if (response.size() + count > 8192) return "Authorization response is too large.";
        response.append(chunk.data(), count);
    }
    if (status != 200) {
        for (char& c : response) if (static_cast<unsigned char>(c) < 32) c = ' ';
        return "License server refused authorization (HTTP " +
            std::to_string(status) + "): " + response;
    }
    // Only extract a bounded base64 field. The kernel verifies every byte of
    // the decoded permission and its signature before enabling any producer.
    const auto field = response.find("\"grant_base64\"");
    if (field == std::string::npos) return "Missing authorization grant in server response.";
    const auto colon = response.find(':', field + 14);
    const auto start = colon == std::string::npos ? std::string::npos : response.find('"', colon + 1);
    const auto end = start == std::string::npos ? std::string::npos : response.find('"', start + 1);
    if (end == std::string::npos || end - start - 1 != 152)
        return "Invalid authorization grant encoding.";
    const std::string grant = response.substr(start + 1, end - start - 1);
    DWORD bytes = sizeof(token);
    if (!CryptStringToBinaryA(grant.c_str(), static_cast<DWORD>(grant.size()),
        CRYPT_STRING_BASE64 | CRYPT_STRING_STRICT,
        reinterpret_cast<BYTE*>(&token), &bytes, nullptr, nullptr) ||
        bytes != sizeof(token)) {
        token = {}; return "Invalid authorization grant length or encoding.";
    }
    if (token.grant.magic != challenge.magic ||
        token.grant.version != challenge.version ||
        token.grant.product != challenge.product ||
        token.grant.permission != TITAN_SDK_FULL_BOOT ||
        std::memcmp(token.grant.nonce, challenge.nonce, sizeof(challenge.nonce))) {
        token = {}; return "Authorization grant does not match this runtime activation challenge.";
    }
    return {};
}

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
 static HANDLE Open(){return CreateFileW(LR"(\\.\TitanSdkRuntime2)",GENERIC_READ|GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,0,nullptr);}
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
  stage="Opening the installed TitanSdkRuntime service";
  Service service(OpenServiceW(manager.value,L"TitanSdkRuntime",SERVICE_QUERY_STATUS|SERVICE_START));
  if(!service.value){error=GetLastError();return;}
  bool started=false;
  for(;;){
   if(GetTickCount64()>=deadline){stage="Waiting up to 10 seconds for the installed SDK runtime";error=ERROR_TIMEOUT;return;}
   SERVICE_STATUS_PROCESS state{};DWORD bytes=0;
   stage="Reading TitanSdkRuntime startup state";
   if(!QueryServiceStatusEx(service.value,SC_STATUS_PROCESS_INFO,reinterpret_cast<LPBYTE>(&state),sizeof(state),&bytes)){error=GetLastError();return;}
   if(state.dwServiceType!=SERVICE_KERNEL_DRIVER){stage="Validating the installed TitanSdkRuntime kernel service identity";error=ERROR_INVALID_DATA;return;}
   if(state.dwCurrentState==SERVICE_RUNNING){
    stage="Opening the started SDK runtime";value=Open();
    if(value!=INVALID_HANDLE_VALUE){error=ERROR_SUCCESS;return;}
    error=GetLastError();if(error!=ERROR_FILE_NOT_FOUND&&error!=ERROR_PATH_NOT_FOUND)return;
   }else if(state.dwCurrentState==SERVICE_STOPPED){
    if(started){stage="Starting the installed SDK runtime";error=state.dwWin32ExitCode?state.dwWin32ExitCode:ERROR_SERVICE_NOT_ACTIVE;return;}
    stage="Starting the installed TitanSdkRuntime service";
    if(!StartServiceW(service.value,0,nullptr)){
     const DWORD startError=GetLastError();if(startError!=ERROR_SERVICE_ALREADY_RUNNING){error=startError;return;}
    }
    started=true;continue;
   }else if(state.dwCurrentState!=SERVICE_START_PENDING&&state.dwCurrentState!=SERVICE_STOP_PENDING){
    stage="Waiting for a usable TitanSdkRuntime service state";error=ERROR_SERVICE_CANNOT_ACCEPT_CTRL;return;
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
 else if(error==ERROR_ACCESS_DENIED)message+=" Run the application as administrator. Activation also requires a valid, enabled product key.";
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
   if(DeviceIoControl(device.value,TS_IOCTL_STATUS,&input,sizeof(input),&state,sizeof(state),&count,nullptr)&&count==sizeof(state)&&state.mode==TITAN_SDK_MODE_EXPIRED){status=state;return "The one-hour demo has expired for this Windows boot. Call Verification(key) with a commercial or OEM key, or restart Windows to begin a new demo.";}
  }
  return DeviceFailure(operation==TS_IOCTL_BEGIN?"Starting SDK demo access":"Reading SDK access state",error);
 }
 if(returned!=sizeof(status)||status.version!=TITAN_SDK_VERSION||status.product!=product||status.mode>TITAN_SDK_MODE_FULL||status.reserved)return "The SDK runtime returned an incompatible or malformed access state.";
 return {};
}
}
namespace TitanSdk {
std::string Begin(unsigned product,TS_STATUS& status){return Query(product,TS_IOCTL_BEGIN,status);}
std::string Status(unsigned product,TS_STATUS& status){return Query(product,TS_IOCTL_STATUS,status);}
std::string Verification(unsigned product,std::string_view key){
 if(product<7||product>10)return "Unknown SDK product.";
 Device device;if(device.value==INVALID_HANDLE_VALUE)return DeviceFailure(device.stage,device.error);
 TS_PRODUCT input{product,0};TS_CHALLENGE challenge{};TS_TOKEN token{};DWORD returned=0;
 if(!DeviceIoControl(device.value,TS_IOCTL_CHALLENGE,&input,sizeof(input),&challenge,sizeof(challenge),&returned,nullptr))return DeviceFailure("Creating a fresh activation challenge",GetLastError());
 if(returned!=sizeof(challenge))return "The SDK runtime returned an incomplete activation challenge.";
 auto error=RequestActivation(key,challenge,token);if(!error.empty())return error;
 if(!DeviceIoControl(device.value,TS_IOCTL_ACTIVATE,&token,sizeof(token),nullptr,0,&returned,nullptr))return DeviceFailure("Verifying the server-signed activation in the kernel",GetLastError());
 TS_STATUS status{};error=Status(product,status);if(!error.empty())return error;
 if(status.mode!=TITAN_SDK_MODE_FULL)return "The runtime accepted the request but full access was not confirmed.";
 return {};
}
}
