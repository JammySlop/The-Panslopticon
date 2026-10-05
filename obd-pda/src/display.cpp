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
        cfg.pin_sclk = config::kPinSpiSclk;
        cfg.pin_mosi = config::kPinSpiMosi;
        // The display never reads, but the SD card on the same bus does.
        cfg.pin_miso = config::kPinSpiMiso;
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
        // Shared with the SD card: release the bus after every draw call.
        cfg.bus_shared = true;
        panel_.config(cfg);
    }
    // No backlight control: BLK is wired to 3V3.
    setPanel(&panel_);
}
