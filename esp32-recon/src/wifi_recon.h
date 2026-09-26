#pragma once

#include <vector>

#include "scan_types.h"

namespace wifi_recon {

void begin();

// Runs one blocking sweep. Results are sorted strongest first.
std::vector<WifiNetwork> scan();

const char* authModeName(wifi_auth_mode_t mode);
const char* securityFlag(wifi_auth_mode_t mode);

}  // namespace wifi_recon
