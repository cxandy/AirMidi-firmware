# The S3's USB port in host role: it reads a USB MIDI device (a digital piano's USB TO HOST port) instead of
# presenting one, so a piano with no 5-pin DIN and no Bluetooth can feed the box and the BLE/Wi-Fi outputs stay
# wireless. The device stack (espressif/esp_tinyusb) is not used here: it has no host role, so this takes the raw
# TinyUSB component and calls tusb_init() with TUSB_ROLE_HOST itself.
idf_component_register(SRCS "usb_host_midi.c" INCLUDE_DIRS "include"
                       REQUIRES airmidi_core
                       PRIV_REQUIRES bus esp_driver_gpio espressif__tinyusb)