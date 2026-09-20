idf_component_register(SRCS "main.cpp" "settings.cpp" "portal.cpp" "network.cpp" "dns.cpp" "display.cpp" "face.cpp" "xiaozhi.cpp" "audio.cpp" "voice.cpp" "camera.cpp"
  INCLUDE_DIRS "."
  REQUIRES esp_wifi esp_netif esp_event esp_http_server nvs_flash esp_driver_gpio esp_driver_spi esp_lcd esp_timer esp_hw_support esp_system lwip esp_http_client esp-tls mbedtls esp_app_format esp_driver_i2s)
target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_17)
