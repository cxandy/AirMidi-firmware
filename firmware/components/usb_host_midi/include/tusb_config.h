// TinyUSB's configuration for the USB host build. The raw component needs a tusb_config.h and does not ship one
// for ESP-IDF (the device stack has its own), and it has to be somewhere the whole build sees, because TinyUSB's
// own sources include it: ESP-IDF puts the main component's include directories on every compile, so this lives
// next to main.c. Comment out CFG_TUH_ENABLED and the rest still compiles for the device builds, which never call
// tusb_init() at all.
#pragma once

#include "esp_rom_sys.h"
#include "sdkconfig.h"

#define CFG_TUSB_OS OPT_OS_FREERTOS

// USB DMA can only reach DRAM, and TinyUSB's transfer buffers and FIFOs live there through these two.
#ifndef CFG_TUSB_MEM_SECTION
#define CFG_TUSB_MEM_SECTION TU_ATTR_ALIGNED(4) DRAM_ATTR
#endif
#ifndef CFG_TUSB_MEM_ALIGN
#define CFG_TUSB_MEM_ALIGN TU_ATTR_ALIGNED(4)
#endif

#define CFG_TUSB_DEBUG 0
#define CFG_TUSB_DEBUG_PRINTF esp_rom_printf  // TinyUSB logs from an ISR, where the RTOS is off limits

// Host: the S3's USB OTG PHY is full speed, and tinyusb's MIDI host class driver (src/class/midi) supports one
// device at a time, which is all a piano is.
#define CFG_TUH_ENABLED 1
#define CFG_TUH_MAX_SPEED OPT_MODE_FULL_SPEED
#define CFG_TUH_MIDI 1
#define CFG_TUH_MIDI_RX_BUFSIZE 128
#define CFG_TUH_MIDI_TX_BUFSIZE 128
#define CFG_TUH_MIDI_EPSIZE 64

#define CFG_TUH_ENUMERATION_BUFSIZE 256
#define CFG_TUH_HUB 1
#define CFG_TUH_DEVICE_MAX (3 * CFG_TUH_HUB + 1)