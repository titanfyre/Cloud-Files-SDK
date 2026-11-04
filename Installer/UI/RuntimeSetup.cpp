#include "RuntimeSetup.h"
#include "LicenseProtocol.h"
#include <setupapi.h>
#include <newdev.h>
#include <devguid.h>
#include <wintrust.h>
#include <softpub.h>
#include <wincrypt.h>
#include <mscat.h>
#include <sddl.h>
#include <objbase.h>
#include <filesystem>
#include <stdexcept>
#include <vector>
#include <array>
#include <map>
#include <string_view>
namespace fs=std::filesystem;
namespace {
void Require(bool ok){if(!ok){DWORD error=GetLastError();throw std::system_error(static_cast<int>(error?error:ERROR_GEN_FAILURE),std::system_category());}}
std::wstring Message(DWORD error){wchar_t* text=nullptr;FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER|FORMAT_MESSAGE_FROM_SYSTEM|FORMAT_MESSAGE_IGNORE_INSERTS,nullptr,error,MAKELANGID(LANG_ENGLISH,SUBLANG_ENGLISH_US),reinterpret_cast<LPWSTR>(&text),0,nullptr);std::wstring message=text?text:L"Operation failed";if(text)LocalFree(text);return message+L" (Windows error "+std::to_wstring(error)+L")";}
bool MicrosoftCatalog(const fs::path& path){
    WINTRUST_FILE_INFO file{};file.cbStruct=sizeof(file);file.pcwszFilePath=path.c_str();
    WINTRUST_DATA data{};data.cbStruct=sizeof(data);data.dwUIChoice=WTD_UI_NONE;data.fdwRevocationChecks=WTD_REVOKE_NONE;data.dwUnionChoice=WTD_CHOICE_FILE;data.pFile=&file;data.dwStateAction=WTD_STATEACTION_VERIFY;data.dwProvFlags=WTD_CACHE_ONLY_URL_RETRIEVAL;
    GUID action=WINTRUST_ACTION_GENERIC_VERIFY_V2;LONG status=WinVerifyTrust(nullptr,&action,&data);bool accepted=false;
    if(status==ERROR_SUCCESS){
        auto provider=WTHelperProvDataFromStateData(data.hWVTStateData);
        auto signer=provider?WTHelperGetProvSignerFromChain(provider,0,FALSE,0):nullptr;
        if(signer&&signer->csCertChain&&signer->pChainContext){
            wchar_t name[256]{},organization[256]{};auto certificate=signer->pasCertChain[0].pCert;
            CertGetNameStringW(certificate,CERT_NAME_ATTR_TYPE,0,const_cast<char*>(szOID_COMMON_NAME),name,256);
            CertGetNameStringW(certificate,CERT_NAME_ATTR_TYPE,0,const_cast<char*>(szOID_ORGANIZATION_NAME),organization,256);
            CERT_CHAIN_POLICY_PARA policy{};policy.cbSize=sizeof(policy);
            CERT_CHAIN_POLICY_STATUS verdict{};verdict.cbSize=sizeof(verdict);
            bool microsoftRoot=CertVerifyCertificateChainPolicy(CERT_CHAIN_POLICY_MICROSOFT_ROOT,signer->pChainContext,&policy,&verdict)&&verdict.dwError==ERROR_SUCCESS;
            if(!microsoftRoot){policy.dwFlags=MICROSOFT_ROOT_CERT_CHAIN_POLICY_CHECK_APPLICATION_ROOT_FLAG;verdict.dwError=ERROR_SUCCESS;microsoftRoot=CertVerifyCertificateChainPolicy(CERT_CHAIN_POLICY_MICROSOFT_ROOT,signer->pChainContext,&policy,&verdict)&&verdict.dwError==ERROR_SUCCESS;}
            accepted=microsoftRoot&&!_wcsicmp(name,L"Microsoft Windows Hardware Compatibility Publisher")&&!_wcsicmp(organization,L"Microsoft Corporation");
        }
    }
    data.dwStateAction=WTD_STATEACTION_CLOSE;WinVerifyTrust(nullptr,&action,&data);return accepted;
}
bool CatalogMember(const fs::path& directory,const wchar_t* name){
    const auto path=directory/name;const auto catalog=directory/L"TitanSdkRuntime.cat";
    HANDLE file=CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(file==INVALID_HANDLE_VALUE)return false;
    struct File{HANDLE h;~File(){CloseHandle(h);}} close{file};
    HCATADMIN admin=nullptr;if(!CryptCATAdminAcquireContext2(&admin,nullptr,L"SHA256",nullptr,0))return false;
    struct Admin{HCATADMIN h;~Admin(){CryptCATAdminReleaseContext(h,0);}} release{admin};
    BYTE hash[64]{};DWORD bytes=sizeof(hash);if(!CryptCATAdminCalcHashFromFileHandle2(admin,file,&bytes,hash,0)||bytes>64)return false;
    wchar_t tag[129]{};const wchar_t* hex=L"0123456789ABCDEF";for(DWORD i=0;i<bytes;++i){tag[i*2]=hex[hash[i]>>4];tag[i*2+1]=hex[hash[i]&15];}
    WINTRUST_CATALOG_INFO member{};member.cbStruct=sizeof(member);member.pcwszCatalogFilePath=catalog.c_str();member.pcwszMemberTag=tag;member.pcwszMemberFilePath=path.c_str();member.hMemberFile=file;member.pbCalculatedFileHash=hash;member.cbCalculatedFileHash=bytes;member.hCatAdmin=admin;
    WINTRUST_DATA data{};data.cbStruct=sizeof(data);data.dwUIChoice=WTD_UI_NONE;data.dwUnionChoice=WTD_CHOICE_CATALOG;data.pCatalog=&member;data.dwStateAction=WTD_STATEACTION_VERIFY;data.dwProvFlags=WTD_CACHE_ONLY_URL_RETRIEVAL|WTD_REVOCATION_CHECK_NONE;
    GUID action=WINTRUST_ACTION_GENERIC_VERIFY_V2;LONG result=WinVerifyTrust(nullptr,&action,&data);data.dwStateAction=WTD_STATEACTION_CLOSE;WinVerifyTrust(nullptr,&action,&data);return result==ERROR_SUCCESS;
}
struct Temporary {
    fs::path directory;
    bool owned=false;
    ~Temporary(){if(!owned)return;for(auto name:{L"TitanSdkRuntime.inf",L"TitanSdkRuntime.cat",L"TitanSdkRuntime.sys"})DeleteFileW((directory/name).c_str());RemoveDirectoryW(directory.c_str());}
    void Create(){
        wchar_t windows[MAX_PATH]{};DWORD length=GetWindowsDirectoryW(windows,MAX_PATH);Require(length>0&&length<MAX_PATH);GUID guid{};Require(SUCCEEDED(CoCreateGuid(&guid)));wchar_t id[40]{};StringFromGUID2(guid,id,40);
        // Use a system-owned parent, not the initiating user's writable Temp tree.
        directory=fs::path(windows)/L"Temp"/(std::wstring(L"TitanSdkRuntime-")+id);
        PSECURITY_DESCRIPTOR descriptor=nullptr;Require(ConvertStringSecurityDescriptorToSecurityDescriptorW(L"D:P(A;;FA;;;SY)(A;;FA;;;BA)",SDDL_REVISION_1,&descriptor,nullptr)!=FALSE);
        SECURITY_ATTRIBUTES security{sizeof(security),descriptor,FALSE};bool made=CreateDirectoryW(directory.c_str(),&security)!=FALSE;DWORD error=GetLastError();LocalFree(descriptor);if(!made){SetLastError(error);Require(false);}
        owned=true;
        const wchar_t* names[]{L"TitanSdkRuntime.inf",L"TitanSdkRuntime.cat",L"TitanSdkRuntime.sys"};
        for(int i=0;i<3;++i){
            HRSRC resource=FindResourceW(nullptr,MAKEINTRESOURCEW(101+i),RT_RCDATA);Require(resource!=nullptr);HGLOBAL loaded=LoadResource(nullptr,resource);Require(loaded!=nullptr);DWORD size=SizeofResource(nullptr,resource);void* bytes=LockResource(loaded);Require(bytes&&size&&size<16*1024*1024);
            HANDLE f=CreateFileW((directory/names[i]).c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);Require(f!=INVALID_HANDLE_VALUE);DWORD written=0;bool ok=WriteFile(f,bytes,size,&written,nullptr)&&written==size;error=GetLastError();CloseHandle(f);if(!ok){SetLastError(error);Require(false);}
        }
    }
};

struct InfLess {bool operator()(const std::wstring& a,const std::wstring& b)const{return _wcsicmp(a.c_str(),b.c_str())<0;}};
using InfLines=std::map<std::wstring,std::wstring,InfLess>;
using InfSections=std::map<std::wstring,InfLines,InfLess>;
std::wstring_view Trim(std::wstring_view value){const auto start=value.find_first_not_of(L" \t\r");if(start==value.npos)return {};return value.substr(start,value.find_last_not_of(L" \t\r")-start+1);}
bool Equal(std::wstring_view a,std::wstring_view b){return a.size()==b.size()&&_wcsnicmp(a.data(),b.data(),a.size())==0;}
bool ReadInf(const fs::path& path,std::wstring& text){
 HANDLE file=CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,nullptr);if(file==INVALID_HANDLE_VALUE)return false;
 struct File{HANDLE h;~File(){CloseHandle(h);}} close{file};FILE_ATTRIBUTE_TAG_INFO attributes{};LARGE_INTEGER size{};
 if(!GetFileInformationByHandleEx(file,FileAttributeTagInfo,&attributes,sizeof(attributes))||(attributes.FileAttributes&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT))||!GetFileSizeEx(file,&size)||size.QuadPart<=0||size.QuadPart>262144)return false;
 std::vector<BYTE> bytes(static_cast<size_t>(size.QuadPart));DWORD count=0;if(!ReadFile(file,bytes.data(),static_cast<DWORD>(bytes.size()),&count,nullptr)||count!=bytes.size())return false;
 const bool unicode=bytes.size()>=2&&bytes[0]==0xff&&bytes[1]==0xfe;if(unicode&&bytes.size()%2)return false;
 // Only the generated ASCII vocabulary is accepted, either ANSI or UTF-16LE.
 // Do not silently reinterpret UTF-8/UTF-16BE, NULs or a continuation grammar.
 text.reserve(bytes.size());for(size_t i=unicode?2:0;i<bytes.size();i+=unicode?2:1){const unsigned c=unicode?bytes[i]|(static_cast<unsigned>(bytes[i+1])<<8):bytes[i];if(c>126||(c<32&&c!='\r'&&c!='\n'&&c!='\t'))return false;text.push_back(static_cast<wchar_t>(c));}return true;
}
bool ExactShape(const std::wstring& text){
 InfSections sections;InfLines* current=nullptr;
 for(size_t start=0;start<text.size();){
  const auto end=text.find(L'\n',start);auto line=Trim(std::wstring_view(text).substr(start,end==text.npos?text.size()-start:end-start));start=end==text.npos?text.size():end+1;
  bool quoted=false;size_t comment=line.npos;for(size_t i=0;i<line.size();++i){if(line[i]==L'"')quoted=!quoted;else if(line[i]==L';'&&!quoted){comment=i;break;}}if(quoted)return false;if(comment!=line.npos)line=Trim(line.substr(0,comment));if(line.empty())continue;
  if(line.back()==L'\\')return false;
  if(line.front()==L'['){if(line.size()<3||line.back()!=L']')return false;const auto name=line.substr(1,line.size()-2);auto entry=sections.emplace(std::wstring(name),InfLines{});if(!entry.second||sections.size()>9)return false;current=&entry.first->second;continue;}
  if(!current)return false;const auto separator=line.find(L'=');auto key=Trim(line.substr(0,separator));auto value=separator==line.npos?std::wstring_view{}:Trim(line.substr(separator+1));if(key.empty()||!current->emplace(std::wstring(key),std::wstring(value)).second||current->size()>7)return false;
 }
 struct Shape{const wchar_t* name;std::vector<const wchar_t*> keys;};
 const Shape shapes[]={
  {L"Version",{L"Signature",L"Class",L"ClassGuid",L"Provider",L"CatalogFile",L"DriverVer",L"PnpLockdown"}},
  {L"DestinationDirs",{L"DriverFiles"}}, {L"SourceDisksNames",{L"1"}}, {L"SourceDisksFiles",{L"TitanSdkRuntime.sys"}},
  {L"DefaultInstall.NTamd64",{L"CopyFiles"}}, {L"DriverFiles",{L"TitanSdkRuntime.sys"}},
  {L"DefaultInstall.NTamd64.Services",{L"AddService"}},
  {L"Service",{L"DisplayName",L"ServiceType",L"StartType",L"ErrorControl",L"ServiceBinary"}},
  {L"Strings",{L"Provider",L"Name",L"Media"}}};
 if(sections.size()!=std::size(shapes))return false;
 for(const auto& shape:shapes){const auto found=sections.find(shape.name);if(found==sections.end()||found->second.size()!=shape.keys.size())return false;for(auto key:shape.keys)if(found->second.find(key)==found->second.end())return false;}
 // SetupAPI expands DIRID13 to paths such as system32\unknown before staging.
 // Check its raw canonical directive, never that transient expanded path.
 return Equal(sections.find(L"Service")->second.find(L"ServiceBinary")->second,L"%13%\\TitanSdkRuntime.sys")&&sections.find(L"DriverFiles")->second.find(L"TitanSdkRuntime.sys")->second.empty();
}
bool NumericParts(std::wstring_view value,wchar_t separator,unsigned count,std::array<unsigned,4>& parts){
 if(value.empty()||count>parts.size())return false;unsigned index=0,number=0,digits=0;
 for(size_t i=0;i<=value.size();++i){const wchar_t c=i==value.size()?separator:value[i];if(c==separator){if(!digits||index>=count)return false;parts[index++]=number;number=digits=0;}else{if(c<L'0'||c>L'9'||++digits>5)return false;number=number*10+static_cast<unsigned>(c-L'0');if(number>65535)return false;}}
 return index==count;
}
bool DriverVersion(INFCONTEXT& line){
 wchar_t date[64]{},version[64]{};if(!SetupGetStringFieldW(&line,1,date,64,nullptr)||!SetupGetStringFieldW(&line,2,version,64,nullptr))return false;
 std::array<unsigned,4> d{},v{};if(!NumericParts(date,L'/',3,d)||!NumericParts(version,L'.',4,v)||d[0]<1||d[0]>12||d[2]<1601||d[2]>9999)return false;
 const unsigned days[]={31,28,31,30,31,30,31,31,30,31,30,31};const bool leap=d[2]%4==0&&(d[2]%100!=0||d[2]%400==0);return d[1]>=1&&d[1]<=days[d[0]-1]+(d[0]==2&&leap?1u:0u);
}
bool OwnPackage(const fs::path& path){
 std::wstring text;if(!ReadInf(path,text)||!ExactShape(text))return false;
 GUID cls{};wchar_t name[128]{};if(!SetupDiGetINFClassW(path.c_str(),&cls,name,128,nullptr)||!IsEqualGUID(cls,GUID_DEVCLASS_SYSTEM))return false;
 HINF inf=SetupOpenInfFileW(path.c_str(),nullptr,INF_STYLE_WIN4,nullptr);if(inf==INVALID_HANDLE_VALUE)return false;
 struct Cleanup{HINF inf;~Cleanup(){SetupCloseInfFile(inf);}} cleanup{inf};
 auto line=[&](const wchar_t* section,const wchar_t* key,DWORD count,INFCONTEXT& context){return SetupFindFirstLineW(inf,section,key,&context)&&SetupGetFieldCount(&context)==count;};
 auto field=[&](const wchar_t* section,const wchar_t* key,DWORD index,DWORD count,const wchar_t* expected){INFCONTEXT context{};wchar_t value[512]{};return line(section,key,count,context)&&SetupGetStringFieldW(&context,index,value,512,nullptr)&&!_wcsicmp(value,expected);};
 auto number=[&](const wchar_t* section,const wchar_t* key,DWORD index,DWORD count,int expected){INFCONTEXT context{};int value=0;return line(section,key,count,context)&&SetupGetIntField(&context,index,&value)&&value==expected;};
 INFCONTEXT binary{},version{};
 return field(L"Version",L"Signature",1,1,L"$Windows NT$")&&field(L"Version",L"Class",1,1,L"System")&&field(L"Version",L"ClassGuid",1,1,L"{4D36E97D-E325-11CE-BFC1-08002BE10318}")&&
  field(L"Version",L"Provider",1,1,L"Titanfyre")&&field(L"Version",L"CatalogFile",1,1,L"TitanSdkRuntime.cat")&&number(L"Version",L"PnpLockdown",1,1,1)&&line(L"Version",L"DriverVer",2,version)&&DriverVersion(version)&&
  number(L"DestinationDirs",L"DriverFiles",1,1,13)&&field(L"SourceDisksNames",L"1",1,1,L"Titan SDK Runtime installation media")&&number(L"SourceDisksFiles",L"TitanSdkRuntime.sys",1,1,1)&&
  field(L"DefaultInstall.NTamd64",L"CopyFiles",1,1,L"DriverFiles")&&field(L"DriverFiles",nullptr,1,1,L"TitanSdkRuntime.sys")&&
  field(L"DefaultInstall.NTamd64.Services",L"AddService",1,3,L"TitanSdkRuntime")&&(field(L"DefaultInstall.NTamd64.Services",L"AddService",2,3,L"")||number(L"DefaultInstall.NTamd64.Services",L"AddService",2,3,0))&&field(L"DefaultInstall.NTamd64.Services",L"AddService",3,3,L"Service")&&
  field(L"Service",L"DisplayName",1,1,L"Titan SDK Runtime")&&number(L"Service",L"ServiceType",1,1,1)&&number(L"Service",L"StartType",1,1,3)&&number(L"Service",L"ErrorControl",1,1,1)&&line(L"Service",L"ServiceBinary",1,binary)&&
  field(L"Strings",L"Provider",1,1,L"Titanfyre")&&field(L"Strings",L"Name",1,1,L"Titan SDK Runtime")&&field(L"Strings",L"Media",1,1,L"Titan SDK Runtime installation media");
}
}
namespace RuntimeSetup {
bool Installed() noexcept{
 HANDLE handle=CreateFileW(LR"(\\.\TitanSdkRuntime2)",GENERIC_READ|GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,0,nullptr);
 if(handle==INVALID_HANDLE_VALUE)return false;
 TS_PRODUCT input{TITAN_SDK_CLOUD_FILES,0};TS_STATUS status{};DWORD bytes=0;
 bool ok=DeviceIoControl(handle,TS_IOCTL_STATUS,&input,sizeof(input),&status,sizeof(status),&bytes,nullptr)&&bytes==sizeof(status)&&status.version==TITAN_SDK_VERSION;
 CloseHandle(handle);return ok;
}
Result Install() noexcept{
 HANDLE mutex=nullptr;
 try{
  mutex=CreateMutexW(nullptr,FALSE,L"Global\\TitanSdkRuntimeSetup");Require(mutex!=nullptr);
  DWORD wait=WaitForSingleObject(mutex,0);if(wait!=WAIT_OBJECT_0&&wait!=WAIT_ABANDONED){CloseHandle(mutex);return {ERROR_BUSY,false,L"Another Titan SDK runtime installation is running."};}
  struct Lock{HANDLE mutex;~Lock(){ReleaseMutex(mutex);CloseHandle(mutex);}} lock{mutex};mutex=nullptr;
  for(int id=101;id<=103;++id)if(!FindResourceW(nullptr,MAKEINTRESOURCEW(id),RT_RCDATA))return {ERROR_RESOURCE_DATA_NOT_FOUND,false,L"This source build does not include a Microsoft-signed runtime package. Use the complete signed evaluation installer."};
  Temporary temporary;temporary.Create();auto inf=temporary.directory/L"TitanSdkRuntime.inf";
  if(!OwnPackage(inf))return {ERROR_INVALID_DATA,false,L"The embedded package does not match Titan SDK Runtime."};
  if(!MicrosoftCatalog(temporary.directory/L"TitanSdkRuntime.cat")||!CatalogMember(temporary.directory,L"TitanSdkRuntime.inf")||!CatalogMember(temporary.directory,L"TitanSdkRuntime.sys"))return {ERROR_INVALID_IMAGE_HASH,false,L"A trusted Microsoft hardware-publisher catalog and exact INF/SYS catalog membership are required."};
  BOOL restart=FALSE;if(!DiInstallDriverW(nullptr,inf.c_str(),0,&restart)){DWORD error=GetLastError();return {error,false,L"Runtime installation failed. "+Message(error)};}
  SC_HANDLE manager=OpenSCManagerW(nullptr,nullptr,SC_MANAGER_CONNECT);Require(manager!=nullptr);
  struct Sc{SC_HANDLE handle;~Sc(){CloseServiceHandle(handle);}} managerClose{manager};
  SC_HANDLE service=OpenServiceW(manager,L"TitanSdkRuntime",SERVICE_START|SERVICE_QUERY_STATUS);Require(service!=nullptr);Sc serviceClose{service};
  if(!StartServiceW(service,0,nullptr)&&GetLastError()!=ERROR_SERVICE_ALREADY_RUNNING){DWORD error=GetLastError();return {error,restart!=FALSE,L"Runtime was staged but could not start. "+Message(error)};}
  if(!Installed())return {ERROR_REVISION_MISMATCH,true,L"Windows must restart to load the matching Titan SDK Runtime. Close applications and restart Windows manually."};
  return {ERROR_SUCCESS,restart!=FALSE,L"Titan SDK Runtime installed. Each product has its own one-hour demo and Verification(key) activation."};
 }catch(const std::system_error& error){if(mutex)CloseHandle(mutex);return {static_cast<DWORD>(error.code().value()),false,L"Runtime setup failed. "+Message(static_cast<DWORD>(error.code().value()))};}
 catch(...){if(mutex)CloseHandle(mutex);return {ERROR_GEN_FAILURE,false,L"Runtime setup could not complete. No automatic restart was performed."};}
}
}
