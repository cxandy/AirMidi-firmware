# USB MIDI needs USB OTG: the S3 has it, the C6 does not (it gets functions that do nothing).
if(IDF_TARGET STREQUAL "esp32s3")
    # esp_private/usb_phy.h, for handing the PHY back to the USB-Serial-JTAG on restart: the `usb` component up to
    # IDF 5.x, esp_hw_support from IDF 6 on (as esp_tinyusb itself does it).
    if(IDF_VERSION_MAJOR LESS 6)
        set(usb_phy_req usb)
    else()
        set(usb_phy_req "")
    endif()
    idf_component_register(SRCS "usb_midi.c" INCLUDE_DIRS "include"
                           REQUIRES airmidi_core
                           PRIV_REQUIRES bus esp_system esp_hw_support esp_timer espressif__esp_tinyusb ${usb_phy_req})
else()
    idf_component_register(SRCS "usb_midi_none.c" INCLUDE_DIRS "include")
endif()
