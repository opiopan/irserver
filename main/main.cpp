#include <esp_log.h>
#include <esp_spiffs.h>
#include <esp_wifi.h>
#include <string>
#include <iostream>
#include <map>
#include <time.h>
#include <sys/time.h>
#include <Task.h>
#include <WiFi.h>
#include <WiFiEventHandler.h>
#include "irserver.h"
#include "mywebserver.h"
#include "Time.h"
#include "htdigestfs.h"
#include "webserver.h"

#include "boardconfig.h"

#include "sdkconfig.h"

static const char tag[] = "main";

static WiFi* wifi;

class MyWiFiEventHandler: public WiFiEventHandler {

    esp_err_t staGotIp(system_event_sta_got_ip_t event_sta_got_ip) {
	if (Time::shouldAdjust()){
	    ESP_LOGI("main", "start SNTP & wait for finish adjustment");
	    Time::startSNTP();
	    if (!Time::waitForFinishAdjustment(10)){
		ESP_LOGE("main", "fail to adjust time by SNTP");
	    }
	}

	Time::setTZ("JST-9");
	Time now;
	printf("%s\n", now.format(Time::SIMPLE_DATETIME));
	
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


#include "bme280.h"
class BME280Task : public Task {
public:
    BME280Task() : tag("BME280Task"){};
protected:
    const char* tag;
    void run(void *data) override{
	vTaskDelay(7000 / portTICK_PERIOD_MS);

	BME280_I2C* dev;
	dev = new BME280_I2C(0x77, GPIO_NUM_25, GPIO_NUM_26,
			     I2C::DEFAULT_CLK_SPEED,
			     I2C_NUM_0, false);
	dev->init();
	
	vTaskDelay(4000 / portTICK_PERIOD_MS);
	while (true){
	    dev->start(true);
	    vTaskDelay(1000 / portTICK_PERIOD_MS);
	    dev->measure();
	    Time now;
	    printf("%s Temp[%.1f dig] Hum[%.1f %%] Press[%.1f hPa]\n",
		   now.format(Time::SIMPLE_DATETIME),
		   dev->getTemperatureFloat(),
		   dev->getHumidityFloat(),
		   dev->getPressureFloat());
	    vTaskDelay(60000 / portTICK_PERIOD_MS);
	}
    };
};

#include <driver/gpio.h>

class LedTask : public Task {
protected:
    int mode;

    static void IRAM_ATTR isrHandler(void* arg){
	auto self = (LedTask*)arg;
	self->mode = (self->mode == 0 ? 1 : 0);
    };
    
    void run(void *data) override{
	mode = 0;
	
	#define  LEDR 32
	#define LEDG 33
	#define LEDB 27
	const uint64_t mask =
	    ((uint64_t)1 << LEDR) |
	    ((uint64_t)1 << LEDG) |
	    ((uint64_t)1 << LEDB);
	    
	gpio_config_t config;
	config.intr_type = (gpio_int_type_t)GPIO_PIN_INTR_DISABLE;
	config.pin_bit_mask = mask;
	config.mode = GPIO_MODE_OUTPUT;
	config.pull_down_en = (gpio_pulldown_t)0;
	config.pull_up_en = (gpio_pullup_t)0;
	gpio_config(&config);

	#define BTN 35
	gpio_config_t oconfig;
	oconfig.intr_type = (gpio_int_type_t)GPIO_PIN_INTR_NEGEDGE;
	oconfig.pin_bit_mask = ((uint64_t)1 << BTN);
	oconfig.mode = GPIO_MODE_INPUT;
	oconfig.pull_down_en = (gpio_pulldown_t)0;
	oconfig.pull_up_en = (gpio_pullup_t)0;
	gpio_config(&oconfig);
	gpio_install_isr_service(0);
	gpio_isr_handler_add((gpio_num_t)BTN, isrHandler, (void*)this);

	for (int i = 0; i < 3; i++){
	    gpio_set_level((gpio_num_t)LEDR, 1);
	    gpio_set_level((gpio_num_t)LEDG, 1);
	    gpio_set_level((gpio_num_t)LEDB, 1);
	    vTaskDelay(500 / portTICK_PERIOD_MS);
	    gpio_set_level((gpio_num_t)LEDR, 0);
	    gpio_set_level((gpio_num_t)LEDG, 0);
	    gpio_set_level((gpio_num_t)LEDB, 0);
	    vTaskDelay(500 / portTICK_PERIOD_MS);
	}
	
	while (true){
	    gpio_set_level((gpio_num_t)LEDR, 0 * mode);
	    gpio_set_level((gpio_num_t)LEDG, 0 * mode);
	    gpio_set_level((gpio_num_t)LEDB, 1 * mode);
	    vTaskDelay(500 / portTICK_PERIOD_MS);
	    gpio_set_level((gpio_num_t)LEDR, 0 * mode);
	    gpio_set_level((gpio_num_t)LEDG, 1 * mode);
	    gpio_set_level((gpio_num_t)LEDB, 0 * mode);
	    vTaskDelay(500 / portTICK_PERIOD_MS);
	    gpio_set_level((gpio_num_t)LEDR, 1 * mode);
	    gpio_set_level((gpio_num_t)LEDG, 0 * mode);
	    gpio_set_level((gpio_num_t)LEDB, 0 * mode);
	    vTaskDelay(500 / portTICK_PERIOD_MS);
	}
	gpio_set_level((gpio_num_t)LEDR, 0);
	gpio_set_level((gpio_num_t)LEDG, 0);
	gpio_set_level((gpio_num_t)LEDB, 0);
    }
};

class MyWebHandler : public WebServerHandler {
public:
    void recieveRequest(WebServerConnection& connection) override{
	auto req = connection.request();
	std::cout << "Method: " << req->methodString() << std::endl;
	std::cout << "URI: " << req->uri() << std::endl;
	std::cout << "Header:" << std::endl;
	for (auto i = req->header().begin();
	     i != req->header().end();
	     i++){
	    std::cout << "    " << i->first << ": "
		      << i->second << std::endl;
	}
	std::cout << "Parameters:" << std::endl;
	for (auto i = req->parameters().begin();
	     i != req->parameters().end();
	     i++){
	    std::cout << "    " << i->first << " = "
		      << i->second << std::endl;
	}
	connection.response()->setHttpStatus(HttpResponse::RESP_200_OK);
	connection.response()->close();
    };
};

class MyWebHandler2 : public WebServerHandler {
public:
    bool needDigestAuthentication() override{
	return true;
    };

    void recieveRequest(WebServerConnection& connection) override{
	connection.response()->setHttpStatus(HttpResponse::RESP_200_OK);
	connection.response()->close();
    };
};

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
    // study BME280
    //----------------------------------------------------
    static BME280Task* bmeTask = NULL;
    bmeTask = new BME280Task();
    bmeTask->start();

    //---------------------------------------------------
    // LED
    //---------------------------------------------------
    static LedTask* ledTask = NULL;
    ledTask = new LedTask();
    ledTask->start();

    //---------------------------------------------------
    // GPIO for luminosity sensor / IR reciever
    //---------------------------------------------------
    {
	gpio_config_t config;
	config.intr_type = (gpio_int_type_t)GPIO_PIN_INTR_DISABLE;
	config.pin_bit_mask = (uint64_t)1 << 4 || (uint64_t)1 << 17;
	config.mode = GPIO_MODE_INPUT;
	config.pull_down_en = (gpio_pulldown_t)0;
	config.pull_up_en = (gpio_pullup_t)0;
	gpio_config(&config);
    }

    //----------------------------------------------------
    // htdigest file preparation
    //----------------------------------------------------
    cs_md5_ctx c;
    cs_md5_init(&c);
    const char* str = "foo:opiopan:bar";
    cs_md5_update(&c, (const unsigned char*)str, strlen(str));
    unsigned char hash[16];
    cs_md5_final(hash, &c);
    unsigned char txt[33];
    for (int i = 0; i < 32; i++){
	int data = (hash[i/2] >> (i & 1 ? 0 : 4)) & 0xf;
	static const unsigned char dic[] = "0123456789abcdef";
	txt[i] = dic[data];
    }
    txt[32] = 0;
    ESP_LOGI(tag, "hash: %s", txt);

    htdigestfs_init("/auth");
    htdigestfs_register("foo", "opiopan", hash);
    
    //----------------------------------------------------
    // irserver main logic
    //----------------------------------------------------
    start_mdns();
    tcpip_adapter_init();
    
    auto webserver = new WebServer();
    webserver->setHandler(new MyWebHandler, "/tool/", false);
    webserver->setHandler(new MyWebHandler2, "/manage/", false);
    webserver->setHtdigest(htdigestfs_fp(), "irserver");
    webserver->startServer("8080");
    startIRServer();
    startHttpServer();
    
    MyWiFiEventHandler *eventHandler = new MyWiFiEventHandler();
    wifi = new WiFi();
    wifi->setWifiEventHandler(eventHandler);

    wifi->connectAP(CONFIG_WIFI_SSID, CONFIG_WIFI_PASSWORD);
    esp_wifi_set_ps(WIFI_PS_MAX_MODEM);
}
