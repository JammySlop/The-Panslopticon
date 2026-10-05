// Screens, navigation and drawing. Owns the bus mode: each screen asks for the
// mode it needs (the sniffer listen-only, everything else normal).
#pragma once

namespace ui {

void begin();
void loop();

}  // namespace ui
