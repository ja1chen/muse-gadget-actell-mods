# ESP-IDF expands dependencies before sdkconfig; only source selection is conditional.
set(srcs "")
if(CONFIG_ACTELL_BRIDGE)
    list(APPEND srcs "actell_bridge.c" "actell_commands.c" "actell_buffer.c" "actell_guard.c" "actell_http.c" "actell_clock.c" "actell_provision.c" "actell_activity.c" "actell_slan.c" "actell_pairing.c" "actell_lan_transport.c" "actell_discovery.c" "actell_leaf.c")
    if(CONFIG_ACTELL_BRIDGE_USB_SETUP)
        list(APPEND srcs "actell_usb_setup.c")
    endif()
endif()
idf_component_register(SRCS ${srcs} INCLUDE_DIRS "include" PRIV_INCLUDE_DIRS "."
    REQUIRES espressif__cjson esp_http_client nvs_flash mbedtls esp_timer esp_hw_support mdns lwip esp_netif
    esp_driver_usb_serial_jtag esp_driver_gpio)
