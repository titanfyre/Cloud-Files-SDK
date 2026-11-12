#include "CloudFiles.h"
#include <winternl.h>
#include <cfapi.h>
#include <bcrypt.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <condition_variable>
#include <cstring>
#include <filesystem>
#include <map>
#include <mutex>
#include <set>
#include <stdexcept>
#include <thread>
#include <utility>

namespace fs=std::filesystem;
namespace CloudFiles {namespace {
constexpr ULONG RootMagic=0x31524643,FileMagic=0x31464643;
constexpr NTSTATUS CloudFailure=static_cast<NTSTATUS>(0xC000CF12u);
constexpr NTSTATUS CloudDenied=static_cast<NTSTATUS>(0xC000CF18u);
constexpr NTSTATUS CloudAborted=static_cast<NTSTATUS>(0xC000CF16u);
constexpr GUID ProviderId={0xb779cc49,0x9035,0x4cbd,{0x9b,0x29,0x5b,0x1d,0x57,0x73,0x42,0x09}};
struct Handle {
 HANDLE value=INVALID_HANDLE_VALUE;
 Handle()=default;explicit Handle(HANDLE h):value(h){}
 ~Handle(){if(value&&value!=INVALID_HANDLE_VALUE)CloseHandle(value);}
 Handle(Handle&& other)noexcept:value(std::exchange(other.value,INVALID_HANDLE_VALUE)){}
 Handle& operator=(Handle&& other)noexcept{if(this!=&other){if(value&&value!=INVALID_HANDLE_VALUE)CloseHandle(value);value=std::exchange(other.value,INVALID_HANDLE_VALUE);}return *this;}
 Handle(const Handle&)=delete;Handle& operator=(const Handle&)=delete;
};
struct RootIdentity {ULONG magic=RootMagic,version=1;std::array<uint8_t,32> digest{};};
struct FileIdentity {ULONG magic=FileMagic,version=1,index=0,reserved=0;uint64_t size=0;std::array<uint8_t,32> digest{};};
static_assert(sizeof(RootIdentity)==40&&sizeof(FileIdentity)==56);
std::string Message(DWORD code){wchar_t* value=nullptr;DWORD n=FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER|FORMAT_MESSAGE_FROM_SYSTEM|FORMAT_MESSAGE_IGNORE_INSERTS,nullptr,code,MAKELANGID(LANG_ENGLISH,SUBLANG_ENGLISH_US),reinterpret_cast<LPWSTR>(&value),0,nullptr);std::string text;for(DWORD i=0;i<n;++i)text+=value[i]<128?static_cast<char>(value[i]):'?';if(value)LocalFree(value);while(!text.empty()&&(text.back()=='\r'||text.back()=='\n'||text.back()==' '))text.pop_back();return text;}
[[noreturn]]void Fail(const char* stage,DWORD code=GetLastError()){throw std::runtime_error(std::string(stage)+" failed (Windows error "+std::to_string(code)+"): "+Message(code));}
void Check(bool ok,const char* stage){if(!ok)Fail(stage);}
bool Same(std::wstring_view a,std::wstring_view b){return a.size()==b.size()&&CompareStringOrdinal(a.data(),static_cast<int>(a.size()),b.data(),static_cast<int>(b.size()),TRUE)==CSTR_EQUAL;}
bool Under(std::wstring_view path,std::wstring_view directory){return path.size()>directory.size()&&path[directory.size()]==L'\\'&&Same(path.substr(0,directory.size()),directory);}
std::wstring Full(const std::wstring& input){
 if(input.empty()||input.size()>240||input.find(L'\0')!=input.npos||input.size()<3||input[1]!=L':'||input[2]!=L'\\'||!((input[0]>=L'A'&&input[0]<=L'Z')||(input[0]>=L'a'&&input[0]<=L'z'))||input.find(L'/',0)!=input.npos||input.find(L':',2)!=input.npos)throw std::runtime_error("Use an absolute local drive path of at most 240 characters; device, UNC and stream paths are refused.");
 for(const auto& part:fs::path(input).relative_path()){const auto value=part.wstring();if(value==L"."||value==L".."||(!value.empty()&&(value.back()==L'.'||value.back()==L' ')))throw std::runtime_error("Path aliases, parent components and trailing dots/spaces are refused.");}
 wchar_t buffer[32768]{};DWORD count=GetFullPathNameW(input.c_str(),32768,buffer,nullptr);Check(count&&count<32768,"Normalize path");std::wstring result(buffer,count);while(result.size()>3&&result.back()==L'\\')result.pop_back();return result;
}
std::wstring Final(HANDLE file){wchar_t buffer[32768]{};DWORD n=GetFinalPathNameByHandleW(file,buffer,32768,FILE_NAME_NORMALIZED|VOLUME_NAME_DOS);Check(n&&n<32768,"Resolve opened path");std::wstring result(buffer,n);if(result.rfind(L"\\\\?\\",0)==0)result.erase(0,4);while(result.size()>3&&result.back()==L'\\')result.pop_back();return result;}
std::vector<Handle> PinAncestors(const std::wstring& full,bool includeLast){
 std::vector<Handle> pins;fs::path p(full),current=p.root_path();std::vector<fs::path> parts;for(auto& item:p.relative_path())parts.push_back(item);
 const auto count=includeLast?parts.size():parts.empty()?0:parts.size()-1;
 for(size_t i=0;i<count;++i){current/=parts[i];Handle h(CreateFileW(current.c_str(),FILE_READ_ATTRIBUTES,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,FILE_FLAG_BACKUP_SEMANTICS|FILE_FLAG_OPEN_REPARSE_POINT,nullptr));Check(h.value!=INVALID_HANDLE_VALUE,"Open directory ancestor");FILE_ATTRIBUTE_TAG_INFO a{};Check(GetFileInformationByHandleEx(h.value,FileAttributeTagInfo,&a,sizeof(a))!=FALSE,"Inspect directory ancestor");if(!(a.FileAttributes&FILE_ATTRIBUTE_DIRECTORY)||(a.FileAttributes&FILE_ATTRIBUTE_REPARSE_POINT))throw std::runtime_error("Directory ancestors must be regular local directories, without reparse points.");pins.push_back(std::move(h));}
 return pins;
}
bool ValidName(std::wstring_view value){
 if(value.empty()||value.size()>240||value.front()==L'\\'||value.back()==L'\\'||value.find(L'\0')!=value.npos)return false;
 unsigned depth=0;size_t offset=0;
 while(offset<value.size()){
  auto end=value.find(L'\\',offset);if(end==value.npos)end=value.size();auto part=value.substr(offset,end-offset);
  if(part.empty()||part.size()>128||part==L"."||part==L".."||part.back()==L'.'||part.back()==L' '||++depth>MaxDepth)return false;
  for(auto ch:part)if(ch<32||wcschr(L"<>:\"/|?*",ch))return false;
  auto dot=part.find(L'.');auto base=part.substr(0,dot);if(Same(base,L"CON")||Same(base,L"PRN")||Same(base,L"AUX")||Same(base,L"NUL")||Same(base,L"CONIN$")||Same(base,L"CONOUT$"))return false;
  if(base.size()==4&&(Same(base.substr(0,3),L"COM")||Same(base.substr(0,3),L"LPT"))&&((base[3]>=L'1'&&base[3]<=L'9')||base[3]==L'\u00b9'||base[3]==L'\u00b2'||base[3]==L'\u00b3'))return false;
  offset=end+1;
 }
 return true;
}
struct Less {bool operator()(const std::wstring& a,const std::wstring& b)const{return CompareStringOrdinal(a.c_str(),static_cast<int>(a.size()),b.c_str(),static_cast<int>(b.size()),TRUE)==CSTR_LESS_THAN;}};
uint64_t FileId(const BY_HANDLE_FILE_INFORMATION& i){return(static_cast<uint64_t>(i.nFileIndexHigh)<<32)|i.nFileIndexLow;}
uint64_t Size(const BY_HANDLE_FILE_INFORMATION& i){return(static_cast<uint64_t>(i.nFileSizeHigh)<<32)|i.nFileSizeLow;}
bool EqualTime(FILETIME a,FILETIME b){return a.dwHighDateTime==b.dwHighDateTime&&a.dwLowDateTime==b.dwLowDateTime;}
struct LocalState {
 struct Item{Entry entry;BY_HANDLE_FILE_INFORMATION info{};};
 std::wstring directory;std::vector<Handle> pins;std::map<std::wstring,Item,Less> items;
 void Scan(const std::wstring& relative,unsigned depth){
  if(depth>MaxDepth)throw std::runtime_error("The backend exceeds the 16-directory depth limit.");
  WIN32_FIND_DATAW data{};const auto path=fs::path(directory)/relative;HANDLE find=FindFirstFileW((path/L"*").c_str(),&data);
  if(find==INVALID_HANDLE_VALUE){if(GetLastError()==ERROR_FILE_NOT_FOUND)return;Fail("Enumerate backend");}
  struct Close{HANDLE value;~Close(){FindClose(value);}} close{find};
  do{
   if(!wcscmp(data.cFileName,L".")||!wcscmp(data.cFileName,L".."))continue;
   const auto name=relative.empty()?std::wstring(data.cFileName):(fs::path(relative)/data.cFileName).wstring();
   if(!ValidName(name)||items.size()>=MaxEntries)throw std::runtime_error("Backend names or entry count exceed the supported bounds.");
   if(data.dwFileAttributes&FILE_ATTRIBUTE_REPARSE_POINT)throw std::runtime_error("Reparse points in the backend are refused.");
   Handle h(CreateFileW((fs::path(directory)/name).c_str(),FILE_READ_ATTRIBUTES,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,FILE_FLAG_BACKUP_SEMANTICS|FILE_FLAG_OPEN_REPARSE_POINT,nullptr));Check(h.value!=INVALID_HANDLE_VALUE,"Inspect backend entry");
   BY_HANDLE_FILE_INFORMATION info{};Check(GetFileInformationByHandle(h.value,&info)!=FALSE,"Read backend identity");
   if(info.dwFileAttributes&FILE_ATTRIBUTE_REPARSE_POINT||!Under(Final(h.value),directory))throw std::runtime_error("Backend entry resolved outside the selected directory.");
   const bool dir=(info.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY)!=0;
   if(!dir&&(info.nNumberOfLinks!=1||Size(info)>MaxFileBytes))throw std::runtime_error("Backend files must have one link and be at most 64 GiB.");
   Entry entry{name,dir?0:Size(info),info.ftLastWriteTime,dir,std::to_wstring(info.dwVolumeSerialNumber)+L":"+std::to_wstring(FileId(info))};if(!items.emplace(name,Item{entry,info}).second)throw std::runtime_error("Case-insensitive backend name collision.");
   if(dir){pins.push_back(std::move(h));Scan(name,depth+1);}
  }while(FindNextFileW(find,&data));
  if(GetLastError()!=ERROR_NO_MORE_FILES)Fail("Continue backend enumeration");
 }
 void ReadFileData(std::wstring_view relative,uint64_t offset,std::vector<uint8_t>& data,HANDLE cancel){
  auto it=items.find(std::wstring(relative));if(it==items.end()||it->second.entry.directory||offset>it->second.entry.size||data.size()>it->second.entry.size-offset)throw std::runtime_error("Backend read exceeds the immutable snapshot.");
  if(WaitForSingleObject(cancel,0)==WAIT_OBJECT_0)throw std::runtime_error("Hydration cancelled.");
  Handle h(CreateFileW((fs::path(directory)/std::wstring(relative)).c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT|FILE_FLAG_OVERLAPPED,nullptr));Check(h.value!=INVALID_HANDLE_VALUE,"Open source file");
  BY_HANDLE_FILE_INFORMATION info{};Check(GetFileInformationByHandle(h.value,&info)!=FALSE,"Recheck source identity");const auto& expected=it->second.info;
  if(info.dwFileAttributes&(FILE_ATTRIBUTE_REPARSE_POINT|FILE_ATTRIBUTE_DIRECTORY)||info.nNumberOfLinks!=1||info.dwVolumeSerialNumber!=expected.dwVolumeSerialNumber||FileId(info)!=FileId(expected)||Size(info)!=Size(expected)||!EqualTime(info.ftLastWriteTime,expected.ftLastWriteTime)||!Under(Final(h.value),directory))throw std::runtime_error("Source file changed since this snapshot; create a new snapshot instead of returning inconsistent bytes.");
  Handle done(CreateEventW(nullptr,TRUE,FALSE,nullptr));Check(done.value!=nullptr,"Create read event");OVERLAPPED over{};over.hEvent=done.value;over.Offset=static_cast<DWORD>(offset);over.OffsetHigh=static_cast<DWORD>(offset>>32);DWORD read=0;
  BOOL ok=ReadFile(h.value,data.data(),static_cast<DWORD>(data.size()),&read,&over);
  if(!ok&&GetLastError()!=ERROR_IO_PENDING)Fail("Read backing file");
  if(!ok){HANDLE waits[]{done.value,cancel};const DWORD result=WaitForMultipleObjects(2,waits,FALSE,5000);if(result!=WAIT_OBJECT_0){CancelIoEx(h.value,&over);GetOverlappedResult(h.value,&over,&read,TRUE);throw std::runtime_error(result==WAIT_OBJECT_0+1?"Hydration cancelled.":"Backing file read exceeded five seconds.");}Check(GetOverlappedResult(h.value,&over,&read,FALSE)!=FALSE,"Complete backing read");}
  if(read!=data.size())throw std::runtime_error("Backing file returned a short read.");
  BY_HANDLE_FILE_INFORMATION after{};Check(GetFileInformationByHandle(h.value,&after)!=FALSE,"Recheck completed read");if(Size(after)!=Size(expected)||!EqualTime(after.ftLastWriteTime,expected.ftLastWriteTime))throw std::runtime_error("Backing file changed during hydration.");
 }
};
std::array<uint8_t,32> Digest(const Backend& backend){
 BCRYPT_ALG_HANDLE algorithm=nullptr;BCRYPT_HASH_HANDLE hash=nullptr;NTSTATUS code=BCryptOpenAlgorithmProvider(&algorithm,BCRYPT_SHA256_ALGORITHM,nullptr,0);if(code<0)throw std::runtime_error("SHA-256 provider initialization failed.");
 struct Cleanup{BCRYPT_ALG_HANDLE* a;BCRYPT_HASH_HANDLE* h;~Cleanup(){if(*h)BCryptDestroyHash(*h);if(*a)BCryptCloseAlgorithmProvider(*a,0);}} close{&algorithm,&hash};
 code=BCryptCreateHash(algorithm,&hash,nullptr,0,nullptr,0,0);if(code<0)throw std::runtime_error("SHA-256 state initialization failed.");
 auto add=[&](const void* p,size_t n){if(n>UINT32_MAX||BCryptHashData(hash,static_cast<PUCHAR>(const_cast<void*>(p)),static_cast<ULONG>(n),0)<0)throw std::runtime_error("SHA-256 update failed.");};
 auto text=[&](const std::wstring& value){const uint64_t size=value.size();add(&size,sizeof(size));add(value.data(),value.size()*sizeof(wchar_t));};
 text(backend.identity);for(const auto& entry:backend.entries){text(entry.path);text(entry.revision);add(&entry.size,sizeof(entry.size));add(&entry.modified,sizeof(entry.modified));const uint8_t dir=entry.directory?1:0;add(&dir,1);}
 std::array<uint8_t,32> value{};if(BCryptFinishHash(hash,value.data(),static_cast<ULONG>(value.size()),0)<0)throw std::runtime_error("SHA-256 finalization failed.");return value;
}
struct OwnedRoot {bool present=false;RootIdentity identity{};};
OwnedRoot InspectRoot(const std::wstring& path){
 std::array<uint8_t,sizeof(CF_SYNC_ROOT_STANDARD_INFO)+sizeof(RootIdentity)+16> buffer{};DWORD returned=0;HRESULT hr=CfGetSyncRootInfoByPath(path.c_str(),CF_SYNC_ROOT_INFO_STANDARD,buffer.data(),static_cast<DWORD>(buffer.size()),&returned);
 if(FAILED(hr)){if(hr==HRESULT_FROM_WIN32(ERROR_CLOUD_FILE_NOT_UNDER_SYNC_ROOT)||hr==HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND)||hr==HRESULT_FROM_WIN32(ERROR_PATH_NOT_FOUND))return {};throw std::runtime_error(Error(hr,"Inspect existing sync root"));}
 if(returned<offsetof(CF_SYNC_ROOT_STANDARD_INFO,SyncRootIdentity)+sizeof(RootIdentity))throw std::runtime_error("Existing sync root belongs to another provider.");
 const auto* info=reinterpret_cast<const CF_SYNC_ROOT_STANDARD_INFO*>(buffer.data());
 if(info->SyncRootIdentityLength!=sizeof(RootIdentity)||wcscmp(info->ProviderName,L"Titan Cloud Files SDK"))throw std::runtime_error("Existing sync root belongs to another provider.");
 RootIdentity identity{};memcpy(&identity,info->SyncRootIdentity,sizeof(identity));if(identity.magic!=RootMagic||identity.version!=1)throw std::runtime_error("Existing sync root identity is not owned by this SDK.");
 Handle directory(CreateFileW(path.c_str(),FILE_READ_ATTRIBUTES,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,FILE_FLAG_BACKUP_SEMANTICS|FILE_FLAG_OPEN_REPARSE_POINT,nullptr));Check(directory.value!=INVALID_HANDLE_VALUE,"Open registered root");BY_HANDLE_FILE_INFORMATION file{};Check(GetFileInformationByHandle(directory.value,&file)!=FALSE,"Read root identity");
 if(FileId(file)!=static_cast<uint64_t>(info->SyncRootFileId.QuadPart))throw std::runtime_error("Select the exact sync-root directory, not one of its descendants.");
 return {true,identity};
}
}

std::string Error(HRESULT code,std::string_view operation){const DWORD value=HRESULT_FACILITY(code)==FACILITY_WIN32?HRESULT_CODE(code):static_cast<DWORD>(code);return std::string(operation)+" failed (HRESULT "+std::to_string(static_cast<uint32_t>(code))+"): "+Message(value);}
std::string Validate(const Backend& backend){
 if(backend.identity.empty()||backend.identity.size()>1024||backend.identity.find(L'\0')!=backend.identity.npos)return "Backend identity must contain 1..1024 non-null characters.";
 if(!backend.read||backend.entries.size()>MaxEntries)return "A backend reader and at most 4096 entries are required.";
 std::map<std::wstring,bool,Less> known;
 for(const auto& entry:backend.entries){
  if(!ValidName(entry.path)||entry.size>MaxFileBytes||(entry.directory&&entry.size)||entry.revision.size()>1024||entry.revision.find(L'\0')!=entry.revision.npos)return "Invalid backend path, revision, directory size or file size.";
  if(!known.emplace(entry.path,entry.directory).second)return "Case-insensitive duplicate backend paths are refused.";
 }
 for(const auto& entry:backend.entries){auto parent=fs::path(entry.path).parent_path().wstring();if(!parent.empty()){auto it=known.find(parent);if(it==known.end()||!it->second)return "Every parent directory must appear explicitly in the catalog.";}}
 return {};
}
Backend LocalDirectory(const std::wstring& directory){
 auto state=std::make_shared<LocalState>();state->directory=Full(directory);if(state->directory.size()==3)throw std::runtime_error("Select a regular backing folder, not an entire drive.");state->pins=PinAncestors(state->directory,true);
 wchar_t volume[MAX_PATH]{},filesystem[MAX_PATH]{};Check(GetVolumePathNameW(state->directory.c_str(),volume,MAX_PATH)!=FALSE,"Find backend volume");Check(GetDriveTypeW(volume)==DRIVE_FIXED,"Require local fixed volume");Check(GetVolumeInformationW(volume,nullptr,0,nullptr,nullptr,nullptr,filesystem,MAX_PATH)!=FALSE,"Read backend volume");
 state->Scan(L"",0);Backend backend;backend.identity=state->directory;backend.local_directory=state->directory;for(const auto& item:state->items)backend.entries.push_back(item.second.entry);backend.read=[state](std::wstring_view path,uint64_t offset,std::vector<uint8_t>& data,HANDLE cancel){state->ReadFileData(path,offset,data,cancel);};return backend;
}

struct Provider::State {
 struct Job{LONGLONG transfer=0,request=0,start=0,end=0;Handle cancel;Job():cancel(CreateEventW(nullptr,TRUE,FALSE,nullptr)){Check(cancel.value!=nullptr,"Create cancellation event");}};
 mutable std::mutex mutex;std::condition_variable drained;Status status;Backend backend;RootIdentity identity;std::wstring root;std::vector<Handle> pins;Handle directory;CF_CONNECTION_KEY connection{};bool connected=false;std::atomic_bool stopping{false},allowed{false};Handle finish{CreateEventW(nullptr,TRUE,FALSE,nullptr)};std::thread monitor;std::vector<std::shared_ptr<Job>> jobs;
 ~State(){if(finish.value&&finish.value!=INVALID_HANDLE_VALUE)SetEvent(finish.value);if(monitor.joinable())monitor.join();}
 void Detail(const std::string& text){std::lock_guard<std::mutex> lock(mutex);status.detail=text;}
 void FetchError(const char* text)noexcept{try{std::lock_guard<std::mutex> lock(mutex);++status.failures;status.detail=text;}catch(...){}}
 static CF_OPERATION_INFO Operation(const CF_CALLBACK_INFO* info,CF_OPERATION_TYPE type){CF_OPERATION_INFO op{};op.StructSize=sizeof(op);op.Type=type;op.ConnectionKey=info->ConnectionKey;op.TransferKey=info->TransferKey;op.RequestKey=info->RequestKey;op.CorrelationVector=info->CorrelationVector;return op;}
 static HRESULT Transfer(const CF_CALLBACK_INFO* info,NTSTATUS completion,LONGLONG offset,LONGLONG length,const void* data=nullptr){auto op=Operation(info,CF_OPERATION_TYPE_TRANSFER_DATA);CF_OPERATION_PARAMETERS p{};p.ParamSize=static_cast<ULONG>(offsetof(CF_OPERATION_PARAMETERS,TransferData)+sizeof(p.TransferData));p.TransferData.CompletionStatus=completion;p.TransferData.Buffer=data;p.TransferData.Offset.QuadPart=offset;p.TransferData.Length.QuadPart=length;return CfExecute(&op,&p);}
 static void CALLBACK Fetch(const CF_CALLBACK_INFO* info,const CF_CALLBACK_PARAMETERS* parameters)noexcept{
  auto* self=static_cast<State*>(info->CallbackContext);std::shared_ptr<Job> job;
  const LONGLONG offset=parameters->FetchData.RequiredFileOffset.QuadPart;const LONGLONG requested=parameters->FetchData.RequiredLength.QuadPart;
  try{
   if(!self->allowed.load()||self->stopping.load())throw std::runtime_error("Hydration is unavailable: demo expired, runtime unavailable, or provider stopping.");
   if(info->FileIdentityLength!=sizeof(FileIdentity)||!info->FileIdentity)throw std::runtime_error("Unknown placeholder identity.");FileIdentity file{};memcpy(&file,info->FileIdentity,sizeof(file));
   if(file.magic!=FileMagic||file.version!=1||file.reserved||file.digest!=self->identity.digest||file.index>=self->backend.entries.size())throw std::runtime_error("Placeholder identity is outside this snapshot.");const auto& entry=self->backend.entries[file.index];
   if(entry.directory||file.size!=entry.size||info->FileSize.QuadPart<0||static_cast<uint64_t>(info->FileSize.QuadPart)!=entry.size||offset<0||static_cast<uint64_t>(offset)>entry.size||requested<-1)throw std::runtime_error("Invalid placeholder hydration range.");
   const uint64_t end=requested==-1?entry.size:(static_cast<uint64_t>(requested)>entry.size-static_cast<uint64_t>(offset)?entry.size:static_cast<uint64_t>(offset)+static_cast<uint64_t>(requested));
   const uint64_t begin=static_cast<uint64_t>(offset)&~uint64_t(4095);const uint64_t alignedEnd=std::min(entry.size,(end+4095)&~uint64_t(4095));
   job=std::make_shared<Job>();job->transfer=info->TransferKey.QuadPart;job->request=info->RequestKey.QuadPart;job->start=offset;job->end=static_cast<LONGLONG>(end);
   {std::lock_guard<std::mutex> lock(self->mutex);if(self->jobs.size()>=16||self->stopping.load())throw std::runtime_error("The bounded hydration queue is full or stopping.");self->jobs.push_back(job);self->status.active=static_cast<unsigned>(self->jobs.size());self->status.peak=std::max(self->status.peak,self->status.active);++self->status.fetches;}
   for(uint64_t position=begin;position<alignedEnd;){
    if(!self->allowed.load()||self->stopping.load()||WaitForSingleObject(job->cancel.value,0)==WAIT_OBJECT_0)throw std::runtime_error("Hydration cancelled or permission expired.");
    const auto count=static_cast<size_t>(std::min<uint64_t>(TransferBytes,alignedEnd-position));std::vector<uint8_t> data(count);self->backend.read(entry.path,position,data,job->cancel.value);
    if(data.size()!=count)throw std::runtime_error("Backend reader changed the requested buffer length.");if(!self->allowed.load()||self->stopping.load()||WaitForSingleObject(job->cancel.value,0)==WAIT_OBJECT_0)throw std::runtime_error("Hydration cancelled before publication.");
    HRESULT hr=Transfer(info,0,static_cast<LONGLONG>(position),static_cast<LONGLONG>(count),data.data());if(FAILED(hr))throw std::runtime_error(Error(hr,"Publish hydration data"));
    {std::lock_guard<std::mutex> lock(self->mutex);self->status.hydrated_bytes+=count;}position+=count;
   }
  }catch(const std::exception& error){Transfer(info,self->stopping.load()?CloudAborted:CloudFailure,offset,requested);self->FetchError(error.what());}
  catch(...){Transfer(info,CloudFailure,offset,requested);self->FetchError("Hydration failed: backend exception or resource exhaustion.");}
  if(job){try{std::lock_guard<std::mutex> lock(self->mutex);auto it=std::find(self->jobs.begin(),self->jobs.end(),job);if(it!=self->jobs.end())self->jobs.erase(it);self->status.active=static_cast<unsigned>(self->jobs.size());self->drained.notify_all();}catch(...){}}
 }
 static void CALLBACK Cancel(const CF_CALLBACK_INFO* info,const CF_CALLBACK_PARAMETERS* p)noexcept{
  auto* self=static_cast<State*>(info->CallbackContext);std::lock_guard<std::mutex> lock(self->mutex);const auto start=p->Cancel.FetchData.FileOffset.QuadPart;const auto length=p->Cancel.FetchData.Length.QuadPart;if(start<0||length<-1)return;
  for(const auto& job:self->jobs)if(job->transfer==info->TransferKey.QuadPart&&job->request==info->RequestKey.QuadPart&&start<=job->start&&(length==-1||(length>=0&&static_cast<uint64_t>(length)>=static_cast<uint64_t>(job->end-start)))){SetEvent(job->cancel.value);++self->status.cancelled;}
 }
 static void Deny(const CF_CALLBACK_INFO* info,bool rename)noexcept{
  auto op=Operation(info,rename?CF_OPERATION_TYPE_ACK_RENAME:CF_OPERATION_TYPE_ACK_DELETE);CF_OPERATION_PARAMETERS p{};
  if(rename){p.ParamSize=static_cast<ULONG>(offsetof(CF_OPERATION_PARAMETERS,AckRename)+sizeof(p.AckRename));p.AckRename.CompletionStatus=CloudDenied;}
  else{p.ParamSize=static_cast<ULONG>(offsetof(CF_OPERATION_PARAMETERS,AckDelete)+sizeof(p.AckDelete));p.AckDelete.CompletionStatus=CloudDenied;}
  const HRESULT hr=CfExecute(&op,&p);auto* self=static_cast<State*>(info->CallbackContext);
  try{std::lock_guard<std::mutex> lock(self->mutex);if(SUCCEEDED(hr)){if(rename)++self->status.rename_denied;else ++self->status.delete_denied;}else{++self->status.failures;self->status.detail=Error(hr,rename?"Refuse placeholder rename":"Refuse placeholder deletion");}}catch(...){}
 }
 static void CALLBACK DenyDelete(const CF_CALLBACK_INFO* info,const CF_CALLBACK_PARAMETERS*)noexcept{Deny(info,false);}
 static void CALLBACK DenyRename(const CF_CALLBACK_INFO* info,const CF_CALLBACK_PARAMETERS*)noexcept{Deny(info,true);}
 void Monitor()noexcept{try{while(WaitForSingleObject(finish.value,500)==WAIT_TIMEOUT){TS_STATUS license{};auto error=TitanSdk::Status(TITAN_SDK_CLOUD_FILES,license);const bool active=error.empty()&&(license.mode==TITAN_SDK_MODE_DEMO||license.mode==TITAN_SDK_MODE_FULL);allowed=active;std::lock_guard<std::mutex> lock(mutex);status.license=license;if(!active){status.detail=error.empty()?"The one-hour demo expired. Verification(key) can unlock this product for the current boot.":error;for(auto& job:jobs)SetEvent(job->cancel.value);}}}catch(...){allowed=false;FetchError("License monitoring failed; further hydration was disabled.");}}
};

Provider::Provider():state_(std::make_unique<State>()){if(!state_->finish.value)Fail("Create provider event");}
Provider::~Provider(){try{if(!Stop().empty())state_.release();}catch(...){state_.release();}} // Retain callback storage if Windows refuses disconnection.
std::string Provider::Verification(std::string_view key){auto error=TitanSdk::Verification(TITAN_SDK_CLOUD_FILES,key);if(!error.empty())return error;TS_STATUS status{};error=TitanSdk::Status(TITAN_SDK_CLOUD_FILES,status);if(error.empty()){state_->allowed=status.mode==TITAN_SDK_MODE_FULL;std::lock_guard<std::mutex> lock(state_->mutex);state_->status.license=status;state_->status.detail="Full access activated for this Windows boot.";}return error;}
std::string Provider::Start(const std::wstring& path,Backend backend){
 auto& s=*state_;if(s.connected)return "Stop the current provider before starting another snapshot.";
 bool registered=false,created=false;
 try{
  auto error=Validate(backend);if(!error.empty())return error;std::stable_sort(backend.entries.begin(),backend.entries.end(),[](const Entry& a,const Entry& b){const auto da=std::count(a.path.begin(),a.path.end(),L'\\'),db=std::count(b.path.begin(),b.path.end(),L'\\');return da!=db?da<db:Less{}(a.path,b.path);});
  s.root=Full(path);if(s.root.size()==3)throw std::runtime_error("A drive root cannot be used as a sync root.");s.pins=PinAncestors(s.root,false);
  if(!backend.local_directory.empty()){const auto backing=Full(backend.local_directory);if(Same(s.root,backing)||Under(s.root,backing)||Under(backing,s.root))throw std::runtime_error("The backing directory and sync root must be separate, non-overlapping trees.");}
  wchar_t volume[MAX_PATH]{},filesystem[MAX_PATH]{};Check(GetVolumePathNameW(fs::path(s.root).parent_path().c_str(),volume,MAX_PATH)!=FALSE,"Find sync volume");Check(GetDriveTypeW(volume)==DRIVE_FIXED,"Require a fixed local sync volume");Check(GetVolumeInformationW(volume,nullptr,0,nullptr,nullptr,nullptr,filesystem,MAX_PATH)!=FALSE,"Read sync filesystem");if(_wcsicmp(filesystem,L"NTFS"))throw std::runtime_error("The initial sync-root implementation requires a local NTFS volume.");
  s.identity.digest=Digest(backend);auto owned=InspectRoot(s.root);if(owned.present&&memcmp(&owned.identity,&s.identity,sizeof(s.identity)))throw std::runtime_error("This root belongs to a different snapshot. Stop and explicitly Release it before selecting a new empty directory.");
  DWORD attributes=GetFileAttributesW(s.root.c_str());if(attributes==INVALID_FILE_ATTRIBUTES){Check(GetLastError()==ERROR_FILE_NOT_FOUND||GetLastError()==ERROR_PATH_NOT_FOUND,"Inspect sync root");Check(CreateDirectoryW(s.root.c_str(),nullptr)!=FALSE,"Create sync root");created=true;}else if(!(attributes&FILE_ATTRIBUTE_DIRECTORY)||((attributes&FILE_ATTRIBUTE_REPARSE_POINT)&&!owned.present))throw std::runtime_error("The sync root must be a regular directory or this SDK's existing registered root.");
  s.directory=Handle(CreateFileW(s.root.c_str(),FILE_READ_ATTRIBUTES,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,FILE_FLAG_BACKUP_SEMANTICS|FILE_FLAG_OPEN_REPARSE_POINT,nullptr));Check(s.directory.value!=INVALID_HANDLE_VALUE,"Pin sync-root directory");if(!Same(Final(s.directory.value),s.root))throw std::runtime_error("Sync root resolved to a different path.");
  if(!owned.present&&!fs::is_empty(s.root))throw std::runtime_error("An unregistered sync-root directory must be completely empty.");
  for(const auto& entry:backend.entries)if(s.root.size()+1+entry.path.size()>240)throw std::runtime_error("A placeholder path exceeds the 240-character initial bound.");
  TS_STATUS license{};error=TitanSdk::Begin(TITAN_SDK_CLOUD_FILES,license);if(!error.empty())throw std::runtime_error(error);if(license.mode!=TITAN_SDK_MODE_DEMO&&license.mode!=TITAN_SDK_MODE_FULL)throw std::runtime_error("The Cloud Files demo expired. Activate a purchased key with Verification(key).");
  if(!owned.present){CF_SYNC_REGISTRATION registration{};registration.StructSize=sizeof(registration);registration.ProviderName=L"Titan Cloud Files SDK";registration.ProviderVersion=L"0.1.0";registration.ProviderId=ProviderId;registration.SyncRootIdentity=&s.identity;registration.SyncRootIdentityLength=sizeof(s.identity);
   CF_SYNC_POLICIES policies{};policies.StructSize=sizeof(policies);policies.Hydration.Primary=CF_HYDRATION_POLICY_PARTIAL;policies.Population.Primary=CF_POPULATION_POLICY_ALWAYS_FULL;policies.InSync=CF_INSYNC_POLICY_NONE;policies.HardLink=CF_HARDLINK_POLICY_NONE;
   HRESULT hr=CfRegisterSyncRoot(s.root.c_str(),&registration,&policies,CF_REGISTER_FLAG_MARK_IN_SYNC_ON_ROOT);if(FAILED(hr))throw std::runtime_error(Error(hr,"Register sync root"));registered=true;
   for(size_t i=0;i<backend.entries.size();++i){const auto& entry=backend.entries[i];FileIdentity id{};id.index=static_cast<ULONG>(i);id.size=entry.size;id.digest=s.identity.digest;CF_PLACEHOLDER_CREATE_INFO info{};const auto name=fs::path(entry.path).filename().wstring();const auto parent=(fs::path(s.root)/fs::path(entry.path).parent_path()).wstring();info.RelativeFileName=name.c_str();info.FileIdentity=&id;info.FileIdentityLength=sizeof(id);info.FsMetadata.FileSize.QuadPart=static_cast<LONGLONG>(entry.size);memcpy(&info.FsMetadata.BasicInfo.LastWriteTime,&entry.modified,sizeof(FILETIME));info.FsMetadata.BasicInfo.CreationTime=info.FsMetadata.BasicInfo.LastWriteTime;info.FsMetadata.BasicInfo.LastAccessTime=info.FsMetadata.BasicInfo.LastWriteTime;info.FsMetadata.BasicInfo.ChangeTime=info.FsMetadata.BasicInfo.LastWriteTime;info.FsMetadata.BasicInfo.FileAttributes=entry.directory?FILE_ATTRIBUTE_DIRECTORY:FILE_ATTRIBUTE_READONLY;info.Flags=CF_PLACEHOLDER_CREATE_FLAG_MARK_IN_SYNC;if(entry.directory)info.Flags|=CF_PLACEHOLDER_CREATE_FLAG_DISABLE_ON_DEMAND_POPULATION;DWORD processed=0;hr=CfCreatePlaceholders(parent.c_str(),&info,1,CF_CREATE_FLAG_STOP_ON_ERROR,&processed);if(FAILED(hr)||processed!=1||FAILED(info.Result))throw std::runtime_error(Error(FAILED(hr)?hr:FAILED(info.Result)?info.Result:E_FAIL,"Create catalog placeholder"));}
  }
  s.backend=std::move(backend);s.stopping=false;s.allowed=true;ResetEvent(s.finish.value);CF_CALLBACK_REGISTRATION callbacks[]={{CF_CALLBACK_TYPE_FETCH_DATA,State::Fetch},{CF_CALLBACK_TYPE_CANCEL_FETCH_DATA,State::Cancel},{CF_CALLBACK_TYPE_NOTIFY_DELETE,State::DenyDelete},{CF_CALLBACK_TYPE_NOTIFY_RENAME,State::DenyRename},CF_CALLBACK_REGISTRATION_END};
  HRESULT hr=CfConnectSyncRoot(s.root.c_str(),callbacks,&s,CF_CONNECT_FLAG_NONE,&s.connection);if(FAILED(hr))throw std::runtime_error(Error(hr,"Connect provider"));s.connected=true;
  {std::lock_guard<std::mutex> lock(s.mutex);s.status={};s.status.connected=true;s.status.entries=static_cast<unsigned>(s.backend.entries.size());s.status.license=license;s.status.detail="Connected: read-only snapshot. Cached files remain local when stopped.";}
  s.monitor=std::thread([&s]{s.Monitor();});return {};
 }catch(const std::exception& e){const std::string result=e.what();if(s.connected){const auto stop=Stop();if(!stop.empty())return result+"; "+stop;}s.allowed=false;s.backend={};s.directory=Handle();s.pins.clear();if(registered){const HRESULT hr=CfUnregisterSyncRoot(s.root.c_str());if(FAILED(hr))return result+"; rollback: "+Error(hr,"Unregister newly created root");}if(created)RemoveDirectoryW(s.root.c_str());s.Detail(result);return result;}
 catch(...){Stop();return "Start failed: resource exhaustion or unexpected backend exception.";}
}
std::string Provider::Stop(){auto& s=*state_;s.stopping=true;s.allowed=false;if(s.finish.value)SetEvent(s.finish.value);{std::lock_guard<std::mutex> lock(s.mutex);for(auto& job:s.jobs)SetEvent(job->cancel.value);}if(s.monitor.joinable())s.monitor.join();if(s.connected){HRESULT hr=CfDisconnectSyncRoot(s.connection);if(FAILED(hr)){const auto error=Error(hr,"Disconnect provider");s.Detail(error);return error;}s.connected=false;}{std::unique_lock<std::mutex> lock(s.mutex);s.drained.wait(lock,[&]{return s.jobs.empty();});s.status.connected=false;s.status.detail="Disconnected. Cached content and registration remain; Release is explicit.";}s.backend={};s.directory=Handle();s.pins.clear();return {};}
Status Provider::GetStatus()const{std::lock_guard<std::mutex> lock(state_->mutex);return state_->status;}
std::string Provider::Release(const std::wstring& input){try{const auto root=Full(input);auto pins=PinAncestors(root,false);if(!InspectRoot(root).present)return "This directory is not a registered Cloud Files SDK root.";HRESULT hr=CfUnregisterSyncRoot(root.c_str());return FAILED(hr)?Error(hr,"Release owned sync root"):std::string();}catch(const std::exception& e){return e.what();}}
}
