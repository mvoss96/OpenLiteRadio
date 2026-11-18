/*
 * What the ELRS module sends: timing frames set the frame period, link
 * statistics are kept, and parameter answers drive the search for the bind
 * action.
 */

#pragma once

#include <stdint.h>

namespace module_link {

struct Status {
    uint32_t frames;      /* frames received with a correct checksum */
    uint32_t crc_errors;
    uint8_t link_quality; /* percent, from the receiver's link statistics */
    uint8_t rssi;         /* dBm without the sign */
    int32_t timing_offset_us;
    uint8_t bind_parameter; /* 0 until it was searched */
};

/* Switch the module on or off; a running bind search ends. */
void set_module_on(bool on);

/* Start binding, as holding BIND for 1 s does. */
void bind_start();

/* Print every frame from the module as "rx" plus hex bytes. */
void set_show_frames(bool show);

/* Call every loop: read the module's frames and repeat unanswered requests. */
void tick();

Status status();

} // namespace module_link
