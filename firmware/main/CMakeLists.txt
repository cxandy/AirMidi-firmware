set(requires board nvs_settings airmidi_core bus uart_midi ble_midi ui_io power ota net net_midi rtp_midi
            usb_midi espnow_midi webui ci esp_app_format esp_system esp_driver_gpio esp_timer)
# In the host role the port belongs to the host stack: usb_midi is the stub and main.c calls this instead.
if(CONFIG_AIRMIDI_USB_HOST)
    list(APPEND requires usb_host_midi)
endif()
idf_component_register(SRCS "main.c" INCLUDE_DIRS "." PRIV_REQUIRES ${requires})