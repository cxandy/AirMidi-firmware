# USB MIDI needs USB OTG: the S3 has it, the C6 does not (it gets functions that do nothing). In host role the
# port belongs to the host stack in usb_host_midi, so this registers the stubs: installing the device stack would
# take the USB PHY a second time.
if(IDF_TARGET STREQUAL "esp32s3" AND NOT CONFIG_AIRMIDI_USB_HOST)
    idf_component_register(SRCS "usb_midi.c" INCLUDE_DIRS "include"
                           REQUIRES airmidi_core
                           PRIV_REQUIRES bus esp_system esp_hw_support esp_timer espressif__esp_tinyusb)
else()
    idf_component_register(SRCS "usb_midi_none.c" INCLUDE_DIRS "include")
endif()
