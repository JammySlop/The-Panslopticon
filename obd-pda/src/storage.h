// Micro SD card: session folders, buffered log streams and one-shot reports.
//
// Layout (no clock on board, so sessions are numbered, not dated):
//   /obdpda/0007/events.log   connection events and errors, always on
//   /obdpda/0007/sniff.log    sniffer capture, candump format
//   /obdpda/0007/live.csv     live-data samples
//   /obdpda/0007/scan_01.txt  trouble-code scan reports
// A session folder is created the first time anything is written after
// power-up. Everything still works without a card; logging just stops.
#pragma once

#include <stddef.h>
#include <stdint.h>

namespace storage {

enum class Stream : uint8_t { Events, Sniff, Live, Count };

// Mounts the card. Call after the display is initialised (they share SPI).
bool begin();
// Unmounts and tries again, e.g. after swapping cards.
bool remount();
bool mounted();

// Call every loop: writes and flushes buffered log data.
void service();

// Opens a stream for appending (creating the session folder if needed),
// writing `header` first if the file is new. False without a card.
bool open(Stream s, const char* header = nullptr);
void close(Stream s);
bool isOpen(Stream s);
// Appends to the stream's RAM buffer; cheap, never blocks on the card unless
// the buffer is full.
void write(Stream s, const char* text, size_t len);
void printf(Stream s, const char* fmt, ...) __attribute__((format(printf, 2, 3)));

// Timestamped line in events.log (and on the serial console). Opens the
// stream on demand.
void event(const char* fmt, ...) __attribute__((format(printf, 1, 2)));

// Writes a whole file into the session folder as "<prefix>_NN.txt", picking
// the next free number. `outName` receives the file name if non-null.
bool writeReport(const char* prefix, const char* text, char* outName = nullptr, size_t outLen = 0);

// e.g. "/obdpda/0007", or "" before the first write.
const char* sessionDir();
uint64_t freeBytes();
uint64_t totalBytes();
// Bytes dropped because the card could not keep up or vanished.
uint32_t droppedBytes();

}  // namespace storage
