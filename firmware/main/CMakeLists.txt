idf_component_register(SRCS "main.cpp" "settings.cpp" "portal.cpp" "network.cpp" "dns.cpp"
  INCLUDE_DIRS "."
  REQUIRES esp_wifi esp_netif esp_event esp_http_server nvs_flash esp_driver_gpio esp_timer esp_hw_support esp_system lwip)
target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_17)
