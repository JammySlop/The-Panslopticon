#include "link.h"

#include "config.h"
#include "direct_can_link.h"
#include "elm327_link.h"

namespace obdlink {
namespace {

Elm327Link gElm;
DirectCanLink gDirect;
Link* gActive = &gElm;

}  // namespace

Link& active() { return *gActive; }

void select(settings::LinkKind kind) {
    // With digital nav wiring the switch sits on the CAN pins.
    if (!config::kDirectCanAvailable) kind = settings::LinkKind::Elm327Wifi;
    Link* next = kind == settings::LinkKind::DirectCan ? static_cast<Link*>(&gDirect) : &gElm;
    if (next == gActive) return;
    gActive->disconnect();
    gActive = next;
}

}  // namespace obdlink
