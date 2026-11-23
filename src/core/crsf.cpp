#include "core/crsf.h"

namespace crsf {

uint8_t crc8(const uint8_t *data, size_t length)
{
    uint8_t crc = 0;

    for (size_t i = 0; i < length; i++) {
        crc ^= data[i];
        for (int bit = 0; bit < 8; bit++) {
            crc = (crc & 0x80u) ? (uint8_t)((crc << 1) ^ 0xD5u) : (uint8_t)(crc << 1);
        }
    }
    return crc;
}

uint16_t from_microseconds(uint16_t microseconds)
{
    /*
     * The stock firmware's conversion, (us - 1000) * 1.6 + 192, in integers
     * and rounded down: 1500 becomes 992.
     */
    int32_t value = ((int32_t)microseconds * 8 - 7040) / 5;

    if (value < 0) {
        value = 0;
    }
    if (value > 2047) {
        value = 2047;
    }
    return (uint16_t)value;
}

void encode_channels(const uint16_t channels[CHANNELS], uint8_t frame[CHANNEL_FRAME_SIZE])
{
    frame[0] = ADDRESS_MODULE;
    frame[1] = CHANNEL_FRAME_SIZE - 2;
    frame[2] = TYPE_CHANNELS;

    /* 16 values of 11 bits each, packed least significant bit first. */
    uint32_t bits = 0;
    int bit_count = 0;
    size_t position = 3;

    for (size_t channel = 0; channel < CHANNELS; channel++) {
        bits |= (uint32_t)(channels[channel] & 0x7FFu) << bit_count;
        bit_count += 11;
        while (bit_count >= 8) {
            frame[position++] = (uint8_t)bits;
            bits >>= 8;
            bit_count -= 8;
        }
    }
    frame[CHANNEL_FRAME_SIZE - 1] = crc8(&frame[2], CHANNEL_FRAME_SIZE - 3);
}

bool Parser::feed(uint8_t byte)
{
    if (complete_) {
        complete_ = false;
        length_ = 0;
    }

    if (length_ == 0) {
        /* Frames from the module start with the radio's address or the sync byte. */
        if (byte != ADDRESS_RADIO && byte != SYNC_BYTE) {
            return false;
        }
    } else if (length_ == 1) {
        if (byte < 2 || byte > MAX_FRAME_SIZE - 2) {
            length_ = 0;
            return false;
        }
    }
    buffer_[length_++] = byte;

    if (length_ < 2 || length_ < (size_t)buffer_[1] + 2) {
        return false;
    }
    if (crc8(&buffer_[2], length_ - 3) != buffer_[length_ - 1]) {
        crc_errors_++;
        length_ = 0;
        return false;
    }
    complete_ = true;
    return true;
}

bool decode_timing(const uint8_t *frame, size_t size, uint32_t &interval_us, int32_t &offset_us)
{
    /* address, length, type, destination, origin, subtype 0x10, interval, offset, CRC */
    if (size != 15 || frame[2] != TYPE_TIMING || frame[5] != 0x10) {
        return false;
    }

    /* The interval is big-endian in units of 0.1 us. */
    uint32_t interval = ((uint32_t)frame[6] << 24) | ((uint32_t)frame[7] << 16) |
                        ((uint32_t)frame[8] << 8) | frame[9];

    uint32_t offset = ((uint32_t)frame[10] << 24) | ((uint32_t)frame[11] << 16) |
                      ((uint32_t)frame[12] << 8) | frame[13];

    interval_us = interval / 10;
    offset_us = (int32_t)offset / 10;
    return true;
}

uint32_t next_frame_delay(uint32_t interval_us, int32_t offset_us)
{
    int32_t limit = (int32_t)(interval_us / 4);
    int32_t correction = (offset_us - SYNC_TARGET_US) / 2;

    if (correction > limit) {
        correction = limit;
    }
    if (correction < -limit) {
        correction = -limit;
    }
    return (uint32_t)((int32_t)interval_us + correction);
}

namespace {

/* Fill in an extended frame's header and CRC; the payload starts at frame[5]. */
void finish_extended(uint8_t *frame, size_t size, uint8_t type, uint8_t destination)
{
    frame[0] = ADDRESS_MODULE;
    frame[1] = (uint8_t)(size - 2);
    frame[2] = type;
    frame[3] = destination;
    frame[4] = ADDRESS_RADIO;
    frame[size - 1] = crc8(&frame[2], size - 3);
}

} // namespace

void encode_ping(uint8_t frame[PING_FRAME_SIZE])
{
    finish_extended(frame, PING_FRAME_SIZE, TYPE_PING, 0x00); /* destination 0: all devices */
}

void encode_parameter_read(uint8_t index, uint8_t frame[PARAMETER_FRAME_SIZE])
{
    frame[5] = index;
    frame[6] = 0; /* first part of the entry */
    finish_extended(frame, PARAMETER_FRAME_SIZE, TYPE_PARAMETER_READ, ADDRESS_MODULE);
}

void encode_parameter_write(uint8_t index, uint8_t value, uint8_t frame[PARAMETER_FRAME_SIZE])
{
    frame[5] = index;
    frame[6] = value;
    finish_extended(frame, PARAMETER_FRAME_SIZE, TYPE_PARAMETER_WRITE, ADDRESS_MODULE);
}

bool decode_device_info(const uint8_t *frame, size_t size, uint8_t &parameter_count)
{
    /* header (5), name with zero byte, serial, hardware and software version (12), count, protocol, CRC */
    if (size < 5 + 1 + 12 + 3 || frame[2] != TYPE_DEVICE_INFO) {
        return false;
    }
    parameter_count = frame[size - 3];
    return true;
}

bool decode_parameter_entry(const uint8_t *frame, size_t size, uint8_t &index, uint8_t &type,
                            const char *&name)
{
    /* header (5), index, parts remaining, parent, type, name with zero byte, ..., CRC */
    if (size < 11 || frame[2] != TYPE_PARAMETER_ENTRY) {
        return false;
    }
    /* The name has to end inside the frame. */
    size_t end = 9;

    while (end < size - 1 && frame[end] != 0) {
        end++;
    }
    if (end >= size - 1) {
        return false;
    }
    index = frame[5];
    type = frame[8] & 0x7Fu; /* the top bit marks a hidden entry */
    name = (const char *)&frame[9];
    return true;
}

bool decode_link_statistics(const uint8_t *frame, size_t size, LinkStatistics &statistics)
{
    if (size != 14 || frame[2] != TYPE_LINK_STATISTICS) {
        return false;
    }
    /* The RSSI byte holds the negative dBm value as a signed byte. */
    statistics.uplink_rssi = (uint8_t)(-(int8_t)frame[3]);
    statistics.uplink_quality = frame[5];
    return true;
}

} // namespace crsf
