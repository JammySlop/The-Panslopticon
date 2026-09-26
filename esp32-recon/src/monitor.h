// Tier 2: monitor (promiscuous) mode. Receive-only. The radio listens to 802.11
// frame headers on a rotating set of channels and reports what it overhears:
// devices probing for networks, which clients are talking to which AP, channel
// activity, and deauth/disassoc bursts. Frame bodies are never inspected.
#pragma once

#include "scan_types.h"

namespace monitor {

void begin();

// Runs one promiscuous sweep across config::kMonitorChannels and returns the
// aggregated results. Leaves the radio back in normal station mode.
MonitorReport sweep();

}  // namespace monitor
