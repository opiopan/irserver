#include <esp_log.h>
#include <string>
#include <Task.h>
#include <WiFi.h>
#include <WiFiEventHandler.h>
#include "irserver.h"
#include "boardconfig.h"

#include "sdkconfig.h"


static const char tag[] = "main";


static WiFi* wifi;

class MyWiFiEventHandler: public WiFiEventHandler {

    esp_err_t staGotIp(system_event_sta_got_ip_t event_sta_got_ip) {
	startIRServer();
	return ESP_OK;
    }
};

extern "C" void app_main() {
    initBoard();
    
    MyWiFiEventHandler *eventHandler = new MyWiFiEventHandler();
	
    wifi = new WiFi();
    wifi->setWifiEventHandler(eventHandler);

    wifi->connectAP(CONFIG_WIFI_SSID, CONFIG_WIFI_PASSWORD);
}
