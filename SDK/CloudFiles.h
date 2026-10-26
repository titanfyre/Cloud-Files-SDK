#pragma once
#include <windows.h>
#include "License.h"
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace CloudFiles {
constexpr unsigned MaxEntries=4096, MaxDepth=16, TransferBytes=65536;
constexpr uint64_t MaxFileBytes=64ull*1024*1024*1024;
struct Entry {std::wstring path;uint64_t size=0;FILETIME modified{};bool directory=false;std::wstring revision;};
// Fill exactly buffer.size() bytes at offset, or throw std::runtime_error.
// The callback is concurrent (at most 16 requests), must honor cancel, and must
// return promptly. No callback can safely preempt arbitrary application code.
using Read=std::function<void(std::wstring_view path,uint64_t offset,
    std::vector<uint8_t>& buffer,HANDLE cancel)>;
struct Backend {
 std::wstring identity; // Stable identity for this snapshot, not a credential.
 std::vector<Entry> entries;
 Read read;
 // Optional canonical directory, used to reject sync-root/backing overlap.
 std::wstring local_directory;
};
// Snapshot a regular local directory. No upload, source changes or recursion
// through reparse points. Source changes after enumeration fail hydration.
Backend LocalDirectory(const std::wstring& directory);
// Pure catalog checks, also applied by Start. Empty string means valid.
std::string Validate(const Backend& backend);
struct Status {
 bool connected=false;unsigned entries=0,active=0,peak=0;
 uint64_t hydrated_bytes=0,fetches=0,cancelled=0,failures=0;
 uint64_t rename_denied=0,delete_denied=0;
 TS_STATUS license{};std::string detail;
};
class Provider {
 struct State;std::unique_ptr<State> state_;
public:
 Provider();~Provider();Provider(const Provider&)=delete;
 Provider& operator=(const Provider&)=delete;
 // Serialize Start/Stop/Verification calls. Status may be read concurrently.
 std::string Verification(std::string_view key);
 // Explicit new empty NTFS directory, or the same unchanged owned snapshot.
 std::string Start(const std::wstring& root,Backend backend);
 std::string Stop(); // Disconnect only. Cached content/registration remain.
 Status GetStatus() const;
 // Explicitly remove this SDK's disconnected registration only. Windows removes
 // dehydrated placeholders and keeps hydrated files as ordinary local files.
 static std::string Release(const std::wstring& root);
};
std::string Error(HRESULT code,std::string_view operation);
}
