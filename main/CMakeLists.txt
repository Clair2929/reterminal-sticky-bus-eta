idf_component_register(
    SRCS
        # Application core
        "main.cpp"
        "app/app.cpp"
        "app/app_clock.cpp"
        "app/app_data.cpp"
        "app/app_event.cpp"
        "app/bus_eta.cpp"
        "app/stop_resolver.cpp"
        "app/user_config.cpp"
        "board/board.cpp"
        # Display basics and shared UI
        "devices/sticky_display.cpp"
        "ui/canvas.cpp"
        "ui/font.cpp"
        "ui/status_bar.cpp"
        # Page framework
        "pages/page.cpp"
        # Buttons, buzzer, and touch gestures
        "devices/sticky_buttons.cpp"
        "devices/sticky_buzzer.cpp"
        "devices/sticky_touch.cpp"
        # Sensor and status pages
        "devices/sticky_sht40.cpp"
        "devices/sticky_rtc.cpp"
        "devices/sticky_battery.cpp"
        "devices/sticky_charger.cpp"
        # Bus ETA page
        "pages/bus_page.cpp"
        "pages/wifi_config_page.cpp"
        "pages/bus_config_page.cpp"
        # Deep sleep
        "devices/sticky_power.cpp"
        "pages/sleep_page.cpp"
    INCLUDE_DIRS
        "."
        "app"
        "board"
        "devices"
        "pages"
        "ui"
    REQUIRES
        esp_driver_gpio esp_driver_i2c esp_driver_i2s esp_driver_ledc esp_driver_spi
        esp_hw_support esp_timer
        bq27220 button fatfs gt911 sdmmc seeed_epaper
        esp_wifi esp_netif esp_event nvs_flash esp_http_client esp-tls json mbedtls
)
