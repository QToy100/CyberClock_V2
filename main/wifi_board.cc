#include "wifi_board.h"
#include "settings.h"
#include "wifi_configuration_ap.h"
#include "ssid_manager.h"
#include "wifi_station.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_log.h>
#include "webserver.h"
#include "esp_wifi.h"
#include "font_awesome_symbols.h"
#include "main.h"



static const char *TAG = "WifiBoard";

WifiBoard::WifiBoard() {
    Settings settings("wifi", true);
    wifi_config_mode_ = settings.GetInt("force_ap") == 1;
    if (wifi_config_mode_) {
        ESP_LOGI(TAG, "force_ap is set to 1, reset to 0");
        settings.SetInt("force_ap", 0);
    }
}

std::string WifiBoard::GetBoardType() {
    return "wifi";
}

void WifiBoard::EnterWifiConfigMode() {
    auto& wifi_ap = WifiConfigurationAp::GetInstance();
    //wifi_ap.SetLanguage(Lang::CODE);
    wifi_ap.SetSsidPrefix("CyberClock");
    wifi_ap.Start();

    // start webserver, make sure the webserver is started after the AP is set up
    // This is necessary to ensure the webserver can access the AP's server handle.
    // modify wifi_configuration_ap.h code
    // httpd_handle_t GetServerHandle() const {return server_;}
    StartWebServer(wifi_ap.GetServerHandle()); 

    // Wait forever until reset after configuration
    while (true) {
        int free_sram = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
        int min_free_sram = heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL);
        //ESP_LOGI(TAG, "Free internal: %u minimal internal: %u", free_sram, min_free_sram);
        vTaskDelay(pdMS_TO_TICKS(10000));
    }
}

void WifiBoard::StartNetwork() {
    if (wifi_config_mode_) {
        EnterWifiConfigMode();
        return;
    }

    // If no WiFi SSID is configured, enter WiFi configuration mode
    auto& ssid_manager = SsidManager::GetInstance();
    auto ssid_list = ssid_manager.GetSsidList();
    if (ssid_list.empty()) {
        wifi_config_mode_ = true;
        EnterWifiConfigMode();
        return;
    }



    auto& wifi_station = WifiStation::GetInstance();
    // wifi_station.OnScanBegin([this]() {
    //     auto display = Board::GetInstance().GetDisplay();
    //     display->ShowNotification(Lang::Strings::SCANNING_WIFI, 30000);
    // });
    // wifi_station.OnConnect([this](const std::string& ssid) {
    //     auto display = Board::GetInstance().GetDisplay();
    //     std::string notification = Lang::Strings::CONNECT_TO;
    //     notification += ssid;
    //     notification += "...";
    //     display->ShowNotification(notification.c_str(), 30000);
    // });
    wifi_station.OnConnected([this](const std::string& ssid) {
        ESP_LOGW(TAG, "Connected to WiFi: %s", ssid.c_str());
        SyncTime();
    });

    wifi_station.Start();

    // Try to connect to WiFi, if failed, launch the WiFi configuration AP
    if (!wifi_station.WaitForConnected(60 * 1000)) {
        wifi_station.Stop();
        wifi_config_mode_ = true;
        EnterWifiConfigMode();
        return;
    }
}

const char* WifiBoard::GetNetworkStateIcon() {
    if (wifi_config_mode_) {
        return FONT_AWESOME_WIFI;
    }
    auto& wifi_station = WifiStation::GetInstance();
    if (!wifi_station.IsConnected()) {
        return FONT_AWESOME_WIFI_OFF;
    }
    int8_t rssi = wifi_station.GetRssi();
    if (rssi >= -60) {
        return FONT_AWESOME_WIFI;
    } else if (rssi >= -70) {
        return FONT_AWESOME_WIFI_FAIR;
    } else {
        return FONT_AWESOME_WIFI_WEAK;
    }
}


void WifiBoard::SetPowerSaveMode(bool enabled) {
    auto& wifi_station = WifiStation::GetInstance();
    wifi_station.SetPowerSaveMode(enabled);
}

void WifiBoard::ResetWifiConfiguration() {
    Settings settings("wifi", true);
    settings.SetInt("force_ap", 1);

    vTaskDelay(pdMS_TO_TICKS(1000));
    esp_restart();
}



WifiBoard& WifiBoard::GetInstance() {
    static WifiBoard instance;
    return instance;
}
