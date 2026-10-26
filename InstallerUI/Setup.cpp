#include "Setup.h"
#include "RuntimeSetup.h"
#include <shlobj.h>
#include <shobjidl.h>
#include <sddl.h>
#include <filesystem>
#include <array>
#include <vector>
#include <stdexcept>
#include <utility>
namespace fs=std::filesystem;
namespace {
constexpr const wchar_t* Names[]{L"CloudFilesStudio.exe",L"CloudFilesCLI.exe",L"CloudFilesExample.exe"};
void Require(bool ok){if(!ok){const auto e=GetLastError();throw std::system_error(static_cast<int>(e?e:ERROR_GEN_FAILURE),std::system_category());}}
std::wstring Error(DWORD error){wchar_t* message=nullptr;FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER|FORMAT_MESSAGE_FROM_SYSTEM|FORMAT_MESSAGE_IGNORE_INSERTS,nullptr,error,MAKELANGID(LANG_ENGLISH,SUBLANG_ENGLISH_US),reinterpret_cast<LPWSTR>(&message),0,nullptr);std::wstring text=message?message:L"Operation failed";if(message)LocalFree(message);return text+L" (Windows error "+std::to_wstring(error)+L")";}
fs::path Known(REFKNOWNFOLDERID id){PWSTR value=nullptr;HRESULT hr=SHGetKnownFolderPath(id,0,nullptr,&value);if(FAILED(hr))throw std::runtime_error("Windows installation folder is unavailable.");fs::path result(value);CoTaskMemFree(value);return result;}
fs::path Directory(){return Known(FOLDERID_ProgramFiles)/L"TitanCSA"/L"CloudFilesSDK";}
void SafeDirectory(const fs::path& path,bool create){auto current=path.root_path();for(const auto& component:path.relative_path()){current/=component;DWORD attributes=GetFileAttributesW(current.c_str());if(attributes==INVALID_FILE_ATTRIBUTES){if(!create){if(GetLastError()==ERROR_FILE_NOT_FOUND||GetLastError()==ERROR_PATH_NOT_FOUND)return;Require(false);}Require(CreateDirectoryW(current.c_str(),nullptr)!=FALSE);attributes=GetFileAttributesW(current.c_str());}if(!(attributes&FILE_ATTRIBUTE_DIRECTORY)||(attributes&FILE_ATTRIBUTE_REPARSE_POINT))throw std::runtime_error("A redirected installation directory was refused.");}}
struct Temp {fs::path path;std::vector<fs::path> files;Temp()=default;Temp(const Temp&)=delete;Temp& operator=(const Temp&)=delete;Temp(Temp&& other)noexcept:path(std::move(other.path)),files(std::move(other.files)){other.path.clear();other.files.clear();}~Temp(){for(auto& file:files)DeleteFileW(file.c_str());if(!path.empty())RemoveDirectoryW(path.c_str());}};
std::wstring Unique(){GUID id{};Require(SUCCEEDED(CoCreateGuid(&id)));wchar_t value[40]{};StringFromGUID2(id,value,40);return value;}
Temp Stage(){
 Temp temporary;wchar_t windows[MAX_PATH]{};Require(GetWindowsDirectoryW(windows,MAX_PATH)!=0);temporary.path=fs::path(windows)/L"Temp"/(std::wstring(L"CloudFilesSDK-")+Unique());
 PSECURITY_DESCRIPTOR descriptor=nullptr;Require(ConvertStringSecurityDescriptorToSecurityDescriptorW(L"D:P(A;;FA;;;SY)(A;;FA;;;BA)",SDDL_REVISION_1,&descriptor,nullptr)!=FALSE);SECURITY_ATTRIBUTES security{sizeof(security),descriptor,FALSE};bool made=CreateDirectoryW(temporary.path.c_str(),&security)!=FALSE;DWORD error=GetLastError();LocalFree(descriptor);if(!made){SetLastError(error);Require(false);}
 for(int i=0;i<3;++i){HRSRC resource=FindResourceW(nullptr,MAKEINTRESOURCEW(201+i),RT_RCDATA);Require(resource!=nullptr);DWORD size=SizeofResource(nullptr,resource);const auto* data=static_cast<const BYTE*>(LockResource(LoadResource(nullptr,resource)));if(!data||size<4096||size>32*1024*1024||data[0]!='M'||data[1]!='Z')throw std::runtime_error("An embedded application payload is missing or malformed.");auto path=temporary.path/Names[i];temporary.files.push_back(path);HANDLE file=CreateFileW(path.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);Require(file!=INVALID_HANDLE_VALUE);DWORD written=0;bool ok=WriteFile(file,data,size,&written,nullptr)&&written==size;error=GetLastError();CloseHandle(file);if(!ok){SetLastError(error);Require(false);}}
 return temporary;
}
void Shortcut(const fs::path& directory,bool install){
 auto folder=Known(FOLDERID_CommonPrograms)/L"TitanCSA";if(!install&&GetFileAttributesW(folder.c_str())==INVALID_FILE_ATTRIBUTES)return;SafeDirectory(folder,install);auto shortcut=folder/L"Cloud Files SDK.lnk";IShellLinkW* link=nullptr;HRESULT hr=CoCreateInstance(CLSID_ShellLink,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&link));if(FAILED(hr))throw std::runtime_error("Windows shortcut creation is unavailable.");struct Close{IShellLinkW* link;~Close(){link->Release();}} close{link};IPersistFile* file=nullptr;hr=link->QueryInterface(IID_PPV_ARGS(&file));if(FAILED(hr))throw std::runtime_error("Windows shortcut persistence is unavailable.");struct Persist{IPersistFile* file;~Persist(){file->Release();}} persist{file};
 if(install){link->SetPath((directory/Names[0]).c_str());link->SetWorkingDirectory(directory.c_str());link->SetDescription(L"Cloud Files SDK - on-demand read-only Windows files");hr=file->Save(shortcut.c_str(),TRUE);if(FAILED(hr))throw std::runtime_error("The application was installed, but its Start menu shortcut could not be created.");}
 else if(GetFileAttributesW(shortcut.c_str())!=INVALID_FILE_ATTRIBUTES){hr=file->Load(shortcut.c_str(),STGM_READ);if(FAILED(hr))throw std::runtime_error("The Start menu shortcut could not be inspected.");wchar_t target[32768]{};WIN32_FIND_DATAW data{};hr=link->GetPath(target,32768,&data,SLGP_RAWPATH);if(FAILED(hr)||_wcsicmp(target,(directory/Names[0]).c_str()))throw std::runtime_error("A Start menu shortcut pointing to another application was retained.");Require(DeleteFileW(shortcut.c_str())!=FALSE);RemoveDirectoryW(folder.c_str());}
}
ProductSetup::Result Install(){
 // Validate all embedded application payloads before altering installation state.
 auto staged=Stage();auto runtime=RuntimeSetup::Install();if(runtime.error)return {runtime.error,runtime.reboot,runtime.detail};
 const auto directory=Directory();SafeDirectory(directory,true);const auto id=Unique();std::array<fs::path,3> next,previous;std::array<bool,3> moved{},had{};
 try{
  for(size_t i=0;i<3;++i){next[i]=directory/(std::wstring(Names[i])+L".new-"+id);previous[i]=directory/(std::wstring(Names[i])+L".old-"+id);Require(CopyFileW((staged.path/Names[i]).c_str(),next[i].c_str(),TRUE)!=FALSE);}
  for(size_t i=0;i<3;++i){const auto target=directory/Names[i];const auto attributes=GetFileAttributesW(target.c_str());had[i]=attributes!=INVALID_FILE_ATTRIBUTES;if(had[i]){if(attributes&(FILE_ATTRIBUTE_REPARSE_POINT|FILE_ATTRIBUTE_DIRECTORY))throw std::runtime_error("A redirected application file was refused.");Require(MoveFileExW(target.c_str(),previous[i].c_str(),MOVEFILE_WRITE_THROUGH)!=FALSE);}Require(MoveFileExW(next[i].c_str(),target.c_str(),MOVEFILE_WRITE_THROUGH)!=FALSE);moved[i]=true;}
 }catch(...){bool restored=true;for(size_t n=3;n>0;--n){const size_t i=n-1;const auto target=directory/Names[i];if(moved[i]&&!DeleteFileW(target.c_str()))restored=false;if(GetFileAttributesW(previous[i].c_str())!=INVALID_FILE_ATTRIBUTES&&!MoveFileExW(previous[i].c_str(),target.c_str(),MOVEFILE_WRITE_THROUGH))restored=false;if(!next[i].empty())DeleteFileW(next[i].c_str());}if(!restored)return {ERROR_GEN_FAILURE,true,L"Application installation failed and rollback could not finish. The shared runtime was retained. Close applications, restart Windows manually and reinstall."};throw;}
 for(auto& path:previous)DeleteFileW(path.c_str());Shortcut(directory,true);return {0,runtime.reboot,L"Cloud Files SDK and shared runtime installed. Choose a backing folder and a separate cloud folder in the application. Restart only if requested."};
}
ProductSetup::Result Uninstall(){
 const auto directory=Directory();if(GetFileAttributesW(directory.c_str())==INVALID_FILE_ATTRIBUTES)return {0,false,L"Cloud Files SDK is not installed. The shared runtime is retained."};SafeDirectory(directory,false);
 for(const auto* name:Names){const auto path=directory/name;const DWORD attributes=GetFileAttributesW(path.c_str());if(attributes==INVALID_FILE_ATTRIBUTES)continue;if(attributes&(FILE_ATTRIBUTE_REPARSE_POINT|FILE_ATTRIBUTE_DIRECTORY))return {ERROR_INVALID_DATA,false,L"A redirected application file was refused; no arbitrary directory is removed."};HANDLE check=CreateFileW(path.c_str(),DELETE,0,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);if(check==INVALID_HANDLE_VALUE)return {GetLastError(),false,L"Close Cloud Files SDK applications and retry Uninstall. "+Error(GetLastError())};CloseHandle(check);}
 Shortcut(directory,false);for(const auto* name:Names){const auto path=directory/name;if(GetFileAttributesW(path.c_str())!=INVALID_FILE_ATTRIBUTES&&!DeleteFileW(path.c_str()))return {GetLastError(),false,L"An application file could not be removed. "+Error(GetLastError())};}RemoveDirectoryW(directory.c_str());return {0,false,L"Cloud Files SDK applications removed. Shared runtime and user cloud folders are retained. Release disconnected roots explicitly before uninstalling when they are no longer needed."};
}
}
namespace ProductSetup {
std::wstring Directory(){return ::Directory().wstring();}
bool Installed()noexcept{try{const auto folder=::Directory();SafeDirectory(folder,false);for(auto* name:Names){const auto attributes=GetFileAttributesW((folder/name).c_str());if(attributes==INVALID_FILE_ATTRIBUTES||(attributes&(FILE_ATTRIBUTE_REPARSE_POINT|FILE_ATTRIBUTE_DIRECTORY)))return false;}return true;}catch(...){return false;}}
Result Execute(bool install)noexcept{
 HANDLE mutex=CreateMutexW(nullptr,FALSE,L"Global\\TitanCloudFilesSetup");if(!mutex)return {GetLastError(),false,L"Could not open the product installer lock."};DWORD wait=WaitForSingleObject(mutex,0);if(wait!=WAIT_OBJECT_0&&wait!=WAIT_ABANDONED){CloseHandle(mutex);return {ERROR_BUSY,false,L"Another Cloud Files SDK installation is running."};}struct Close{HANDLE h;~Close(){ReleaseMutex(h);CloseHandle(h);}} close{mutex};HRESULT com=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
 Result result;try{result=install?Install():Uninstall();}catch(const std::system_error& e){result={static_cast<DWORD>(e.code().value()),false,L"Operation failed. "+Error(static_cast<DWORD>(e.code().value()))};}catch(const std::exception& e){const std::string value=e.what();result={ERROR_GEN_FAILURE,false,std::wstring(value.begin(),value.end())};}catch(...){result={ERROR_GEN_FAILURE,false,L"Operation failed: not enough resources or unexpected system error."};}if(SUCCEEDED(com))CoUninitialize();return result;
}
}
