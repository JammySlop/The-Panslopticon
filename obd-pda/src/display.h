// LovyanGFX driver for the 3.5" 240x320 ILI9341 SPI module.
#pragma once

#define LGFX_USE_V1
#include <LovyanGFX.hpp>

class Display : public lgfx::LGFX_Device {
public:
    Display();

private:
    lgfx::Panel_ILI9341 panel_;
    lgfx::Bus_SPI bus_;
};

// One shared instance; the UI is the only thing that draws.
extern Display lcd;
