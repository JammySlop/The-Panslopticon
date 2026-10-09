// One CAN frame, as seen by every link (direct TWAI or an ELM327).
#pragma once

#include <stdint.h>

struct CanFrame {
    uint32_t id = 0;
    bool extended = false;
    bool rtr = false;
    uint8_t dlc = 0;
    uint8_t data[8] = {};
};
