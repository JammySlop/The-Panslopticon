// Screens, navigation and drawing. Screens open the active link on demand and
// the sniffer switches it into monitor mode while it is showing.
#pragma once

namespace ui {

void begin();
void loop();
// Redraws the current screen, e.g. after settings changed over serial.
void refresh();
// Runs the four-corner touch calibration (blocks until done) and saves it.
// Call refresh() afterwards to redraw the screen.
void calibrateTouch();

}  // namespace ui
