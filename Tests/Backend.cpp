#include "../SDK/CloudFiles.h"
#include <objbase.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <random>
#include <stdexcept>
namespace fs=std::filesystem;
unsigned checks=0;
void Require(bool value,const char* message){++checks;if(!value)throw std::runtime_error(message);}
template<class F>void Refused(F fn){bool refused=false;try{fn();}catch(const std::exception&){refused=true;}Require(refused,"An invalid local backend request was accepted");}
int main(){try{
 CloudFiles::Backend b;b.identity=L"test snapshot";b.read=[](std::wstring_view,uint64_t,std::vector<uint8_t>&,HANDLE){};
 Require(CloudFiles::Validate(b).empty(),"Empty catalog with reader should be valid");
 for(const auto* name:{L"..",L"a\\..\\b",L".\\a",L"a\\.\\b",L"C:\\file",L"\\\\server\\x",L"a:stream",L"NUL",L"con.txt",L"lpt1.log",L"COM9",L"LPT\u00b2",L"a.",L"a ",L"a\\",L"a//b",L"a?b",L"a\\\\b",L"a\nb"}){b.entries={{name,0,{},false}};Require(!CloudFiles::Validate(b).empty(),"Unsafe catalog name accepted");}
 b.entries={{L"folder",0,{},true},{L"folder\\hello.txt",12,{},false}};Require(CloudFiles::Validate(b).empty(),"Valid nested manifest rejected");
 b.entries.push_back({L"FOLDER\\HELLO.TXT",0,{},false});Require(!CloudFiles::Validate(b).empty(),"Case collision accepted");b.entries.erase(b.entries.begin());Require(!CloudFiles::Validate(b).empty(),"Missing parent accepted");
 b.entries={{L"folder",1,{},true}};Require(!CloudFiles::Validate(b).empty(),"Directory with bytes accepted");b.entries={{L"large",CloudFiles::MaxFileBytes+1,{},false}};Require(!CloudFiles::Validate(b).empty(),"Oversize file accepted");
 b.entries.assign(CloudFiles::MaxEntries+1,{L"x",0,{},false});Require(!CloudFiles::Validate(b).empty(),"Oversize namespace accepted");b.entries.clear();b.read={};Require(!CloudFiles::Validate(b).empty(),"Absent reader accepted");
 wchar_t temporary[MAX_PATH]{};Require(GetTempPathW(MAX_PATH,temporary)>0,"Temporary path unavailable");GUID guid{};Require(SUCCEEDED(CoCreateGuid(&guid)),"Unique test identity unavailable");wchar_t id[40]{};StringFromGUID2(guid,id,40);const fs::path parent=fs::path(temporary)/L"TitanCloudFilesTests";fs::create_directory(parent);const fs::path own=parent/id;fs::create_directory(own);const fs::path source=own/L"source";fs::create_directory(source);fs::create_directory(source/L"nested");
 struct Cleanup{fs::path path,parent;~Cleanup(){std::error_code e;if(path.parent_path()==parent&&path.filename().wstring().front()==L'{')fs::remove_all(path,e);fs::remove(parent,e);}} cleanup{own,parent};
 std::vector<uint8_t> expected(1024*1024+817);for(size_t i=0;i<expected.size();++i)expected[i]=static_cast<uint8_t>((i*37+i/257)&255);{std::ofstream file(source/L"nested/data.bin",std::ios::binary);file.write(reinterpret_cast<const char*>(expected.data()),static_cast<std::streamsize>(expected.size()));Require(file.good(),"Fixture write failed");}{std::ofstream file(source/L"empty.txt",std::ios::binary);}
 HANDLE cancel=CreateEventW(nullptr,TRUE,FALSE,nullptr);Require(cancel!=nullptr,"Cancel event unavailable");struct Event{HANDLE h;~Event(){CloseHandle(h);}} event{cancel};
 {
  auto local=CloudFiles::LocalDirectory(source.wstring());Require(CloudFiles::Validate(local).empty(),"Real catalog rejected");Require(local.entries.size()==3,"Unexpected namespace size");std::mt19937_64 random(0x434c4f5544ull);
  for(unsigned i=0;i<1024;++i){const auto offset=random()%expected.size();const auto length=static_cast<size_t>(std::min<uint64_t>(CloudFiles::TransferBytes,expected.size()-offset));std::vector<uint8_t> read(length);local.read(L"nested\\data.bin",offset,read,cancel);Require(std::equal(read.begin(),read.end(),expected.begin()+static_cast<ptrdiff_t>(offset)),"Independent real-file range bytes differ");}
  std::vector<uint8_t> byte(1);Refused([&]{local.read(L"nested\\data.bin",UINT64_MAX,byte,cancel);});Refused([&]{local.read(L"nested\\data.bin",expected.size(),byte,cancel);});Refused([&]{local.read(L"..\\data.bin",0,byte,cancel);});Refused([&]{local.read(L"nested",0,byte,cancel);});
  SetEvent(cancel);Refused([&]{local.read(L"nested\\data.bin",0,byte,cancel);});ResetEvent(cancel);
  {std::ofstream changed(source/L"nested/data.bin",std::ios::binary|std::ios::app);changed.put('x');}
  Refused([&]{local.read(L"nested\\data.bin",0,byte,cancel);});
 }
 const auto alias=source/L"alias.bin";Require(CreateHardLinkW(alias.c_str(),(source/L"nested/data.bin").c_str(),nullptr)!=FALSE,"Hardlink fixture unavailable");Refused([&]{CloudFiles::LocalDirectory(source.wstring());});fs::remove(alias);
 Refused([&]{CloudFiles::LocalDirectory(L"C:\\");});Refused([&]{CloudFiles::LocalDirectory(L"\\\\server\\share");});Refused([&]{CloudFiles::LocalDirectory(source.wstring()+L". ");});Refused([&]{CloudFiles::LocalDirectory(source.wstring()+L"\\..\\source");});
 std::cout<<checks<<" catalog, path, immutable-file and cancellation checks passed. No Cloud Files root or driver was registered.\n";return 0;
}catch(const std::exception& error){std::cerr<<"FAILED: "<<error.what()<<"\n";return 1;}}
