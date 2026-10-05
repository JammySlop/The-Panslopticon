#include "display.h"

#include "config.h"

Display lcd;

Display::Display() {
    {
        auto cfg = bus_.config();
        cfg.spi_host = SPI2_HOST;  // the C3's only general-purpose SPI
        cfg.spi_mode = 0;
        cfg.freq_write = config::kLcdSpiHz;
        cfg.freq_read = 16000000;
        cfg.spi_3wire = false;
        cfg.use_lock = true;
        cfg.dma_channel = SPI_DMA_CH_AUTO;
        cfg.pin_sclk = config::kPinLcdSclk;
        cfg.pin_mosi = config::kPinLcdMosi;
        cfg.pin_miso = -1;  // write-only module
        cfg.pin_dc = config::kPinLcdDc;
        bus_.config(cfg);
        panel_.setBus(&bus_);
    }
    {
        auto cfg = panel_.config();
        cfg.pin_cs = config::kPinLcdCs;
        cfg.pin_rst = config::kPinLcdRst;
        cfg.pin_busy = -1;
        cfg.panel_width = 240;
        cfg.panel_height = 320;
        cfg.memory_width = 240;
        cfg.memory_height = 320;
        cfg.offset_x = 0;
        cfg.offset_y = 0;
        cfg.offset_rotation = 0;
        cfg.readable = false;
        cfg.invert = config::kLcdInvert;
        cfg.rgb_order = false;
        cfg.dlen_16bit = false;
        cfg.bus_shared = false;
        panel_.config(cfg);
    }
    {
        auto cfg = light_.config();
        cfg.pin_bl = config::kPinLcdBacklight;
        cfg.invert = false;
        cfg.freq = 12000;
        cfg.pwm_channel = 0;
        light_.config(cfg);
        panel_.setLight(&light_);
    }
    setPanel(&panel_);
}
