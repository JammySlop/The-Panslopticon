// LovyanGFX driver for the 3.5" 240x320 ILI9341 SPI module.
#pragma once

#define LGFX_USE_V1
#include <LovyanGFX.hpp>

// This module's DC line is inverted: high selects a command, low selects data
// (the opposite of a stock ILI9341). LovyanGFX has no polarity option, so this
// bus inverts the DC pad in the GPIO matrix instead. LovyanGFX re-configures
// the DC pin in init() and, after a clock change, in beginTransaction(), which
// clears the inversion; both re-apply it.
class InvertedDcBus : public lgfx::Bus_SPI {
public:
    bool init() override;
    void beginTransaction() override;

    // Whether the pad really inverted when init() last checked it.
    bool dcInverted() const { return dcInverted_; }

private:
    void invertDc();
    bool dcInverted_ = false;
};

class Display : public lgfx::LGFX_Device {
public:
    Display();

    // False if the DC inversion is configured but the pad did not invert:
    // the panel would then read every command as data and stay blank.
    bool dcOk() const;

private:
    lgfx::Panel_ILI9341 panel_;
    InvertedDcBus bus_;
};

// One shared instance; the UI is the only thing that draws.
extern Display lcd;
