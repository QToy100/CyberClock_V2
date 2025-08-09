idf_component_register(
    SRCS
        main.cpp
        CyberClock.cpp
        webserver.cc
        settings.cc
        wifi_board.cc
        audio_codecs/audio_codec.cc
        audio_codecs/es8311_audio_codec.cc
    INCLUDE_DIRS "." "audio_codecs" "esp_idf/78__esp-wifi-connect/include" 

    REQUIRES freertos driver nvs_flash esp_wifi esp_netif esp_event esp_http_server json espressif__esp_codec_dev wifi_provisioning
    PRIV_REQUIRES app_update
)