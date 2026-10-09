#include "can_bus.h"

#include <Arduino.h>
#include <driver/twai.h>

#include "config.h"

namespace can_bus {
namespace {

bool gRunning = false;
uint32_t gBitrate = 0;
Mode gMode = Mode::ListenOnly;
uint32_t gRxFrames = 0;
uint32_t gTxFrames = 0;

bool timingFor(uint32_t rate, twai_timing_config_t& out) {
    switch (rate) {
        case 1000000: out = TWAI_TIMING_CONFIG_1MBITS(); return true;
        case 500000: out = TWAI_TIMING_CONFIG_500KBITS(); return true;
        case 250000: out = TWAI_TIMING_CONFIG_250KBITS(); return true;
        case 125000: out = TWAI_TIMING_CONFIG_125KBITS(); return true;
        default: return false;
    }
}

// True if an OBD ECU answers a "PIDs 01-20 supported" request.
bool probeObd() {
    Frame req;
    req.id = config::kObdBroadcastId;
    req.dlc = 8;
    req.data[0] = 0x02;  // single frame, 2 bytes
    req.data[1] = 0x01;  // service 01: current data
    req.data[2] = 0x00;  // PID 00: supported PIDs
    flush();
    if (!send(req, 50)) return false;
    const uint32_t start = millis();
    Frame rx;
    while (millis() - start < config::kObdResponseTimeoutMs) {
        if (!receive(rx, 10)) continue;
        if (!rx.extended && rx.id >= config::kObdReplyIdFirst &&
            rx.id <= config::kObdReplyIdLast && rx.data[1] == 0x41) {
            return true;
        }
    }
    return false;
}

}  // namespace

bool begin(uint32_t rate, Mode m) {
    end();
    twai_timing_config_t timing;
    if (!timingFor(rate, timing)) return false;

    twai_general_config_t general = TWAI_GENERAL_CONFIG_DEFAULT(
        static_cast<gpio_num_t>(config::kPinCanTx), static_cast<gpio_num_t>(config::kPinCanRx),
        m == Mode::ListenOnly ? TWAI_MODE_LISTEN_ONLY : TWAI_MODE_NORMAL);
    general.rx_queue_len = config::kCanRxQueueLen;
    general.tx_queue_len = config::kCanTxQueueLen;
    general.alerts_enabled = TWAI_ALERT_NONE;
    const twai_filter_config_t filter = TWAI_FILTER_CONFIG_ACCEPT_ALL();

    if (twai_driver_install(&general, &timing, &filter) != ESP_OK) return false;
    if (twai_start() != ESP_OK) {
        twai_driver_uninstall();
        return false;
    }
    gRunning = true;
    gBitrate = rate;
    gMode = m;
    return true;
}

void end() {
    if (!gRunning) return;
    twai_stop();
    twai_driver_uninstall();
    gRunning = false;
}

bool running() { return gRunning; }
uint32_t bitrate() { return gBitrate; }
Mode mode() { return gMode; }

bool ensure(uint32_t rate, Mode m) {
    if (gRunning && gBitrate == rate && gMode == m) return true;
    return begin(rate, m);
}

bool receive(Frame& frame, uint32_t timeoutMs) {
    if (!gRunning) return false;
    twai_message_t msg;
    if (twai_receive(&msg, pdMS_TO_TICKS(timeoutMs)) != ESP_OK) return false;
    frame.id = msg.identifier;
    frame.extended = msg.extd;
    frame.rtr = msg.rtr;
    frame.dlc = msg.data_length_code > 8 ? 8 : msg.data_length_code;
    memset(frame.data, 0, sizeof(frame.data));
    if (!frame.rtr) memcpy(frame.data, msg.data, frame.dlc);
    ++gRxFrames;
    return true;
}

bool send(const Frame& frame, uint32_t timeoutMs) {
    if (!gRunning || gMode != Mode::Normal) return false;
    twai_message_t msg = {};
    msg.identifier = frame.id;
    msg.extd = frame.extended;
    msg.rtr = frame.rtr;
    msg.data_length_code = frame.dlc > 8 ? 8 : frame.dlc;
    memcpy(msg.data, frame.data, msg.data_length_code);
    if (twai_transmit(&msg, pdMS_TO_TICKS(timeoutMs)) != ESP_OK) return false;
    ++gTxFrames;
    return true;
}

void flush() {
    if (gRunning) twai_clear_receive_queue();
}

void service() {
    if (!gRunning) return;
    twai_status_info_t s;
    if (twai_get_status_info(&s) != ESP_OK) return;
    if (s.state == TWAI_STATE_BUS_OFF) {
        twai_initiate_recovery();
    } else if (s.state == TWAI_STATE_STOPPED) {
        // Recovery finished; the controller waits in STOPPED until restarted.
        twai_start();
    }
}

Stats stats() {
    Stats st;
    st.rxFrames = gRxFrames;
    st.txFrames = gTxFrames;
    if (!gRunning) return st;
    twai_status_info_t s;
    if (twai_get_status_info(&s) != ESP_OK) return st;
    st.rxMissed = s.rx_missed_count + s.rx_overrun_count;
    st.busErrors = s.bus_error_count;
    st.txErrorCounter = s.tx_error_counter;
    st.rxErrorCounter = s.rx_error_counter;
    switch (s.state) {
        case TWAI_STATE_RUNNING:
            st.state = (s.tx_error_counter >= 128 || s.rx_error_counter >= 128) ? "passive" : "ok";
            break;
        case TWAI_STATE_BUS_OFF: st.state = "bus-off"; break;
        case TWAI_STATE_RECOVERING: st.state = "recover"; break;
        default: st.state = "stopped"; break;
    }
    return st;
}

uint32_t autodetect() {
    // Passive pass: a correctly timed bus delivers frames without errors.
    for (uint32_t rate : config::kCanBitrates) {
        if (!begin(rate, Mode::ListenOnly)) continue;
        const uint32_t before = gRxFrames;
        const uint32_t start = millis();
        Frame f;
        while (millis() - start < config::kAutodetectListenMs) receive(f, 10);
        const bool heard = gRxFrames - before >= 2;
        const bool clean = stats().busErrors == 0;
        end();
        if (heard && clean) return rate;
    }
    // Many cars gate the OBD port behind a silent gateway, so ask instead.
    // At the wrong rate this causes a brief burst of error frames, the same
    // thing any ELM327-style scanner does while searching protocols.
    for (uint32_t rate : config::kCanBitrates) {
        if (!begin(rate, Mode::Normal)) continue;
        const bool answered = probeObd();
        end();
        if (answered) return rate;
    }
    return 0;
}

}  // namespace can_bus
