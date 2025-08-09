idf_component_register(
    SRCS
        main.cpp
        CyberClock.cpp
        webserver.cc
        settings.cc
        wifi_board.cc
    INCLUDE_DIRS "." "esp_idf/78__esp-wifi-connect/include" 

    REQUIRES freertos driver nvs_flash esp_wifi esp_netif esp_event esp_http_server json wifi_provisioning
    PRIV_REQUIRES app_update
)