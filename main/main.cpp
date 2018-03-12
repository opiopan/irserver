#include <esp_log.h>
#include <string>
#include <Task.h>
#include <WiFi.h>
#include <WiFiEventHandler.h>
#include "esp_wifi.h"
#include "irserver.h"
#include "boardconfig.h"

#include "sdkconfig.h"

static const char tag[] = "main";

static WiFi* wifi;

class MyWiFiEventHandler: public WiFiEventHandler {

    esp_err_t staGotIp(system_event_sta_got_ip_t event_sta_got_ip) {
	//startIRServer();
	return ESP_OK;
    }

    esp_err_t staDisconnected(system_event_sta_disconnected_t info) {
	esp_wifi_connect();
	return ESP_OK;
    }
};

extern "C" void app_main() {
    initBoard();

    tcpip_adapter_init();
    startIRServer();
    
    MyWiFiEventHandler *eventHandler = new MyWiFiEventHandler();
	
    wifi = new WiFi();
    wifi->setWifiEventHandler(eventHandler);

    wifi->connectAP(CONFIG_WIFI_SSID, CONFIG_WIFI_PASSWORD);
}
