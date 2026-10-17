# The S3's USB port in host role: it reads a USB MIDI device (a digital piano's USB TO HOST port) instead of
# presenting one, so a piano with no 5-pin DIN and no Bluetooth can feed the box and the BLE/Wi-Fi outputs stay
# wireless. The device stack (espressif/esp_tinyusb) has no host role, so this takes the raw TinyUSB component and
# calls tusb_init() with TUSB_ROLE_HOST itself.
#
# Registered unconditionally with requirements every target has, and TinyUSB's include path comes from the build's
# global include directories instead of a component requirement: a requirement that is only there in one build does
# not reach the components that need it. The source compiles to stubs unless CONFIG_AIRMIDI_USB_HOST is set, so
# the C6 never includes tusb.h and never needs the component.
if(CONFIG_AIRMIDI_USB_HOST)
    # TinyUSB's own sources include tusb_config.h, and this component is not one of theirs: the raw component
    # compiles them with only its own directories, so the config has to be on the build's global include path.
    idf_build_set_property(INCLUDE_DIRS "${CMAKE_CURRENT_LIST_DIR}/include" APPEND)

    # The managed component compiles the device stack only (usbd.c, tusb.c, the device class drivers); the host
    # stack and its drivers are these, from the same tree (managed_components sits two levels above this component,
    # beside firmware/). tusb.c and tusb_fifo.c are left out: the managed component already builds them.
    set(tusb ${CMAKE_CURRENT_LIST_DIR}/../../../managed_components/espressif__tinyusb)
    set(tuh_srcs
        ${tusb}/src/host/usbh.c
        ${tusb}/src/host/hub.c
        ${tusb}/src/class/midi/midi_host.c
        ${tusb}/src/portable/synopsys/dwc2/hcd_dwc2.c
        ${tusb}/src/portable/synopsys/dwc2/dwc2_common.c)
    idf_component_register(SRCS "usb_host_midi.c" ${tuh_srcs}
                           INCLUDE_DIRS "include" "${tusb}/src" "${tusb}/src/host" "${tusb}/src/class/midi"
                                           "${tusb}/src/portable/synopsys/dwc2"
                           REQUIRES airmidi_core
                           PRIV_REQUIRES bus espressif__tinyusb)
else()
    idf_component_register(SRCS "usb_host_midi.c" INCLUDE_DIRS "include"
                           REQUIRES airmidi_core
                           PRIV_REQUIRES bus)
endif()