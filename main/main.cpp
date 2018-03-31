#include <esp_log.h>
#include <esp_spiffs.h>
#include <esp_wifi.h>
#include <string>
#include <Task.h>
#include <WiFi.h>
#include <WiFiEventHandler.h>
#include "irserver.h"
#include "webserver.h"

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

#include <mbedtls/error.h>
#include <mbedtls/md.h>
#include <mbedtls/pk.h>
#include <mbedtls/sha256.h>

#include "signature.h"

class VerifySignatureTask : public Task {
public:
    VerifySignatureTask() : tag("VerifySignature"){};
protected:
    const char* tag;
    void run(void *data) override;
};

void VerifySignatureTask::run(void* ctxdata) {
    const char* keypath = "/spiffs/public-key.pem";
    const char* datapath = "/spiffs/test";
    const char* signpath = "/spiffs/test.sign";

    PK pk;

    /* load public key file */
    int ret;
    ret = pk.parsePublicKeyfile(keypath);
    if (ret != 0){
	ESP_LOGE(tag,
		 "failed: mbedtls_pk_parse_public_keyfile "
		 "returned -0x%04x",
		 -ret);
	return;
    }
	
    /* load signature */
    uint8_t sign[MBEDTLS_MPI_MAX_SIZE];
    FILE* fp = fopen(signpath, "rb");
    if (fp == NULL){
	ESP_LOGE(tag, "cannot open signature");
	return;
    }
    int sign_len = fread(sign, 1, sizeof(sign), fp);
    if (sign_len < 0){
	ESP_LOGE(tag, "an error occured during reading signature");
	fclose (fp);
	return;
    }
    fclose (fp);
    
    /* compute sha-256 hash */
    SHA256 hash;
    
    size_t bufsize = 1024;
    uint8_t* buf = (uint8_t*)malloc(bufsize);
    fp = fopen(datapath, "rb");
    if (fp == NULL){
	ESP_LOGE(tag, "cannot open data file");
	free(buf);
	return;
    }
    while ((ret = fread(buf, 1, bufsize, fp)) != 0){
	if (ret < 0){
	    ESP_LOGE(tag, "an error occured during reading data file");
	    free(buf);
	    fclose(fp);
	    return;
	}
	hash.update(buf, ret);
    }
    fclose(fp);
    free(buf);

    hash.finish();

    uint8_t txt[hash.getHashLength() * 2 + 1];
    for (int i = 0; i < sizeof(txt) - 1; i++){
	int data = (hash.getHash()[i/2] >> (i & 1 ? 0 : 4)) & 0xf;
	static const unsigned char dic[] = "0123456789abcdef";
	txt[i] = dic[data];
    }
    txt[sizeof(txt) - 1] = 0;
    ESP_LOGI(tag, "hash: %s", txt);
    

    /* verify signature */
    ret = pk.verify(&hash, sign, sign_len);
    if (ret == 0) {
	ESP_LOGI(tag, "succeed verification of signature");
    } else {
	ESP_LOGE(tag, "failed: mbedtls_pk_verify returned -0x%04x", -ret);
    }
}

#include <mdns.h>
#include "irserverProtocol.h"
static void start_mdns() {
    ESP_ERROR_CHECK(mdns_init());
    ESP_ERROR_CHECK(mdns_hostname_set("esp32ir"));
    ESP_ERROR_CHECK(mdns_instance_name_set("esp32"));
    
    ESP_ERROR_CHECK( mdns_service_add("irserver", "_irserveer", "_tcp",
				      IRSERVER_PORT, NULL, 0) );
    ESP_ERROR_CHECK( mdns_service_add("World Wide Web", "_http", "_tcp",
				      80, NULL, 0) );

    ESP_LOGI(tag, "mdns service registered");
}

extern "C" void app_main() {
    initBoard();

    //----------------------------------------------------
    // SPIFFS study
    //----------------------------------------------------
    esp_vfs_spiffs_conf_t conf = {
      .base_path = "/spiffs",
      .partition_label = "spiffs",
      .max_files = 5,
      .format_if_mount_failed = false
    };
    esp_err_t ret = esp_vfs_spiffs_register(&conf);
    if (ret != ESP_OK) {
        if (ret == ESP_FAIL) {
            ESP_LOGE(tag, "Failed to mount or format filesystem");
        } else if (ret == ESP_ERR_NOT_FOUND) {
            ESP_LOGE(tag, "Failed to find SPIFFS partition");
        } else {
            ESP_LOGE(tag, "Failed to initialize SPIFFS (%d)", ret);
        }
    }
    size_t total = 0, used = 0;
    ret = esp_spiffs_info("spiffs", &total, &used);
    if (ret != ESP_OK) {
        ESP_LOGE(tag, "Failed to get SPIFFS partition information");
    } else {
        ESP_LOGI(tag, "Partition size: total: %d, used: %d", total, used);
    }
    FILE* fp = fopen("/spiffs/public-key.pem", "rb");
    if (fp == NULL){
        ESP_LOGE(tag, "Failed to open public-key.pem");
    }else{
	int size = 512;
	void* buf = malloc(size);
	int rc = fread(buf, 1, size, fp);
	ESP_LOGI(tag, "via FILE*: %d bytes read from public-key.pem", rc);
	free(buf);
    }
    fclose(fp);
    int fd = open("/spiffs/public-key.pem", O_RDONLY);
    if (fd < 0){
        ESP_LOGE(tag, "Failed to open public-key.pem");
    }else{
	int size = 512;
	void* buf = malloc(size);
	esp_log_level_set("*", ESP_LOG_DEBUG);
	int rc = read(fd, buf, size);
	esp_log_level_set("*", ESP_LOG_INFO);
	ESP_LOGI(tag, "via fd: %d bytes read from public-key.pem", rc);
	free(buf);
    }
    close(fd);

    //----------------------------------------------------
    // study RSA signature verification
    //----------------------------------------------------
    static VerifySignatureTask* vstask = NULL;
    vstask = new VerifySignatureTask();
    vstask->start();

    //----------------------------------------------------
    // irserver main logic
    //----------------------------------------------------
    start_mdns();
    tcpip_adapter_init();
    startIRServer();
    startHttpServer();
    
    MyWiFiEventHandler *eventHandler = new MyWiFiEventHandler();
	
    wifi = new WiFi();
    wifi->setWifiEventHandler(eventHandler);

    wifi->connectAP(CONFIG_WIFI_SSID, CONFIG_WIFI_PASSWORD);
}
