#pragma once
#include "LicenseProtocol.h"
#include <string>
#include <string_view>
namespace TitanSdk {
// Empty string means success. All errors are detailed English strings.
// Begin starts the product's ONE shared one-hour per-boot demo only once.
std::string Begin(unsigned product, TS_STATUS& status);
std::string Status(unsigned product, TS_STATUS& status);
// Preserved public ABI: demo-only variants always return an unsupported error.
std::string Verification(unsigned product,std::string_view key);
}
