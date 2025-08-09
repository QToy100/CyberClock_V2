#ifndef WIFI_BOARD_H
#define WIFI_BOARD_H

#include <string>

class WifiBoard {
protected:
    bool wifi_config_mode_ = false;

    WifiBoard();
    void EnterWifiConfigMode();
    //std::string GetBoardJson();

public:
    static WifiBoard& GetInstance();

    void Enabled_ConfigMode();
    std::string GetBoardType();
    void StartNetwork();
    const char* GetNetworkStateIcon();
    void SetPowerSaveMode(bool enabled);
    void ResetWifiConfiguration();
};

#endif // WIFI_BOARD_H
