#pragma once
#include "LicenseProtocol.h"
#include <string>
#include <string_view>
namespace TitanSdk {
// Empty string means success. All errors are detailed English strings.
// Begin starts the product's ONE shared one-hour per-boot demo only once.
std::string Begin(unsigned product, TS_STATUS& status);
std::string Status(unsigned product, TS_STATUS& status);
// Activates this product in the same runtime until Windows restarts.
// Requires a fresh server-signed proof; never returns a private signing key.
std::string Verification(unsigned product,std::string_view key);
}
