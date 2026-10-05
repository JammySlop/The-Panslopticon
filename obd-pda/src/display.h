// LovyanGFX driver for the 2" 240x320 ST7789 SPI module.
#pragma once

#define LGFX_USE_V1
#include <LovyanGFX.hpp>

class Display : public lgfx::LGFX_Device {
public:
    Display();

private:
    lgfx::Panel_ST7789 panel_;
    lgfx::Bus_SPI bus_;
    lgfx::Light_PWM light_;
};

// One shared instance; the UI is the only thing that draws.
extern Display lcd;
