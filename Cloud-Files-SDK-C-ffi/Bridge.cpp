#include "SDK/CloudFiles.h"
#include "WireTypes.h"
#include <map>
struct ProductSession{

std::unique_ptr<CloudFiles::Provider> provider;
CloudFiles::Provider& Get(){if(!provider)provider=std::make_unique<CloudFiles::Provider>();return *provider;}

Json Invoke(const std::string& op,const Json& a){

if(op=="check"){
 CloudFiles::Backend backend;backend.identity=Wide(a.at("identity").get<std::string>());backend.read=[](std::wstring_view,std::uint64_t,std::vector<std::uint8_t>&,HANDLE){throw std::runtime_error("Catalog-only validator has no storage reader");};
 auto array=a.value("entries",Json::array());Require(array.is_array() && array.size()<=CloudFiles::MaxEntries,"Too many entries");for(const auto& value:array){CloudFiles::Entry e;e.path=Wide(value.at("path").get<std::string>());e.size=value.contains("size")?Identifier(value,"size"):0;e.directory=value.value("directory",false);e.revision=Wide(value.value("revision",std::string()));if(value.contains("modified_100ns")){auto ticks=Identifier(value,"modified_100ns");e.modified.dwLowDateTime=static_cast<DWORD>(ticks);e.modified.dwHighDateTime=static_cast<DWORD>(ticks>>32);}backend.entries.push_back(std::move(e));}
 auto error=CloudFiles::Validate(backend);return {{"valid",error.empty()},{"message",error}};
}
if(op=="local_catalog"){auto backend=CloudFiles::LocalDirectory(Wide(a.at("directory").get<std::string>()));Json entries=Json::array();for(const auto& e:backend.entries)entries.push_back({{"path",Utf8(e.path)},{"size",std::to_string(e.size)},{"directory",e.directory},{"revision",Utf8(e.revision)}});return {{"identity",Utf8(backend.identity)},{"entries",entries}};}
if(op=="verification"){CheckMessage(Get().Verification(a.at("key").get<std::string>()));return Json::object();}
if(op=="start_local"){auto backend=CloudFiles::LocalDirectory(Wide(a.at("directory").get<std::string>()));CheckMessage(Get().Start(Wide(a.at("root").get<std::string>()),std::move(backend)));return Json::object();}
if(op=="stop"){if(provider)CheckMessage(provider->Stop());return Json::object();}
if(op=="release"){CheckMessage(CloudFiles::Provider::Release(Wide(a.at("root").get<std::string>())));return Json::object();}
if(op=="status"){auto s=Get().GetStatus();return {{"connected",s.connected},{"entries",s.entries},{"active",s.active},{"peak",s.peak},{"hydrated_bytes",std::to_string(s.hydrated_bytes)},{"fetches",std::to_string(s.fetches)},{"cancelled",std::to_string(s.cancelled)},{"failures",std::to_string(s.failures)},{"rename_denied",std::to_string(s.rename_denied)},{"delete_denied",std::to_string(s.delete_denied)},{"license",EncodeWire(s.license)},{"detail",s.detail}};}

throw SdkError(50,"Unknown operation");}
};
static Runtime<ProductSession> runtime;
extern "C" __declspec(dllexport) unsigned cloud_files_sdk_json_open(std::uint64_t* id)noexcept{return runtime.Open(id);}
extern "C" __declspec(dllexport) unsigned cloud_files_sdk_json_close(std::uint64_t id)noexcept{return runtime.Close(id);}
extern "C" __declspec(dllexport) char* cloud_files_sdk_json_call(std::uint64_t id,const char* request,std::uint32_t length)noexcept{return runtime.Call(id,request,length);}
extern "C" __declspec(dllexport) void cloud_files_sdk_json_free(char* response)noexcept{delete[] response;}
