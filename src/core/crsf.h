/*
 * CRSF frames exchanged with the ELRS module: channel frames to the module,
 * timing and link statistics from it. No hardware access.
 *
 * A frame is: address, length, type, payload, CRC. The length counts type,
 * payload and CRC; the CRC covers type and payload.
 */

#pragma once

#include <stddef.h>
#include <stdint.h>

namespace crsf {

constexpr size_t CHANNELS = 16;
constexpr size_t CHANNEL_FRAME_SIZE = 26;
constexpr size_t MAX_FRAME_SIZE = 64;

constexpr uint8_t ADDRESS_RADIO = 0xEA;
constexpr uint8_t ADDRESS_MODULE = 0xEE;

constexpr uint8_t TYPE_LINK_STATISTICS = 0x14;
constexpr uint8_t TYPE_CHANNELS = 0x16;
constexpr uint8_t TYPE_TIMING = 0x3A;
constexpr uint8_t TYPE_PING = 0x28;
constexpr uint8_t TYPE_DEVICE_INFO = 0x29;
constexpr uint8_t TYPE_PARAMETER_ENTRY = 0x2B;
constexpr uint8_t TYPE_PARAMETER_READ = 0x2C;
constexpr uint8_t TYPE_PARAMETER_WRITE = 0x2D;

/* Parameter type of an action such as "Bind"; writing 1 starts it, 4 starts it confirmed. */
constexpr uint8_t PARAMETER_TYPE_COMMAND = 13;
constexpr uint8_t COMMAND_START = 1;
constexpr uint8_t COMMAND_CONFIRM = 4;

/* Sizes of the frames built by the encode functions below. */
constexpr size_t PING_FRAME_SIZE = 6;
constexpr size_t PARAMETER_FRAME_SIZE = 8;

/* CRC-8 with polynomial 0xD5. */
uint8_t crc8(const uint8_t *data, size_t length);

/* Convert a channel value in microseconds (988..2012) to CRSF units (172..1811). */
uint16_t from_microseconds(uint16_t microseconds);

/* Build a channel frame from 16 values in CRSF units. */
void encode_channels(const uint16_t channels[CHANNELS], uint8_t frame[CHANNEL_FRAME_SIZE]);

/* Collects received bytes into frames and checks their CRC. */
class Parser {
public:
    /* Add one byte. Returns true when it completes a frame with a correct CRC. */
    bool feed(uint8_t byte);

    /* The completed frame, valid until the next call of feed(). */
    const uint8_t *frame() const { return buffer_; }
    size_t size() const { return length_; }
    uint8_t type() const { return buffer_[2]; }

    uint32_t crc_errors() const { return crc_errors_; }

private:
    uint8_t buffer_[MAX_FRAME_SIZE];
    size_t length_ = 0;
    bool complete_ = false;
    uint32_t crc_errors_ = 0;
};

/*
 * Read the frame interval from a timing frame. Returns false if the frame is
 * not a timing frame of the expected layout.
 */
bool decode_timing(const uint8_t *frame, size_t size, uint32_t &interval_us, int32_t &offset_us);

/*
 * Offset the frames are held at. The module reports how much earlier than it
 * needs a frame arrived (positive) or how late (negative), with only 100 us
 * of margin; aiming at 0 lets small timing variations push a frame past the
 * module's transmission, where it counts as almost a whole interval early.
 */
constexpr int32_t SYNC_TARGET_US = 500;

/*
 * Delay until the next channel frame after a timing frame: one frame period
 * stretched or shortened by half the difference between the offset and
 * SYNC_TARGET_US, by at most a quarter of the interval. Half, because the
 * module averages over several frames and sends a second timing frame right
 * after a late one, so the full difference would be corrected twice.
 */
uint32_t next_frame_delay(uint32_t interval_us, int32_t offset_us);

/* Ask every device on the link for its device information. */
void encode_ping(uint8_t frame[PING_FRAME_SIZE]);

/* Ask the module for the first part of one parameter entry. */
void encode_parameter_read(uint8_t index, uint8_t frame[PARAMETER_FRAME_SIZE]);

/* Set a one-byte parameter of the module. */
void encode_parameter_write(uint8_t index, uint8_t value, uint8_t frame[PARAMETER_FRAME_SIZE]);

/* Read the number of parameters from a device information frame. */
bool decode_device_info(const uint8_t *frame, size_t size, uint8_t &parameter_count);

/*
 * Read index, type and name from the first part of a parameter entry. The
 * name points into the frame and ends with a zero byte.
 */
bool decode_parameter_entry(const uint8_t *frame, size_t size, uint8_t &index, uint8_t &type,
                            const char *&name);

struct LinkStatistics {
    uint8_t uplink_rssi; /* dBm, sign removed: 80 means -80 dBm */
    uint8_t uplink_quality; /* percent of packets the receiver got */
};

bool decode_link_statistics(const uint8_t *frame, size_t size, LinkStatistics &statistics);

} // namespace crsf
