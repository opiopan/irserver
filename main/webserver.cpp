#include <esp_log.h>
#include <string.h>
#include <Task.h>
#include <lwip/sockets.h>
#include <lwip/netdb.h>
#include <mongoose.h>
#include "Mutex.h"
#include "webserver.h"
#include "htdigestfs.h"
#include "reboot.h"
#include "boardconfig.h"

#include "sdkconfig.h"

const char* tag = "webserver";

class ConnectionContext {
public:
    virtual ~ConnectionContext(){};
};

class HttpServerTask : public Task {
private:
  mg_mgr mgr;
  mg_connection *nc;
    
public:
    HttpServerTask();
    bool init(const char* port);

private:
    void run(void *data) override;

    static void defHandler(struct mg_connection *nc, int ev, void *p);
    static void downloadHandler(struct mg_connection *nc, int ev, void *p);
};

HttpServerTask::HttpServerTask(){
}

bool HttpServerTask::init(const char* port){
    mg_mgr_init(&mgr, NULL);
    nc = mg_bind(&mgr, port, defHandler);
    if (nc == NULL) {
	ESP_LOGE(tag, "Error setting up listener!");
	return false;
    }
    mg_set_protocol_http_websocket(nc);
    mg_register_http_endpoint(nc, "/download", downloadHandler);
    
    return true;
}

void HttpServerTask::run(void *data){
    while (1) {
	mg_mgr_poll(&mgr, 1000);
    }
}

void HttpServerTask::defHandler(struct mg_connection *nc,
					  int ev, void *p) {
    static const char *reply_fmt =
	"HTTP/1.0 200 OK\r\n"
	"Connection: close\r\n"
	"Content-Type: text/plain\r\n"
	"\r\n"
	"Hello %s\n";

    switch (ev) {
    case MG_EV_ACCEPT: {
	char addr[32];
	mg_sock_addr_to_str(&nc->sa, addr, sizeof(addr),
			    MG_SOCK_STRINGIFY_IP | MG_SOCK_STRINGIFY_PORT);
	ESP_LOGI(tag, "Connection %p from %s", nc, addr);
	break;
    }
    case MG_EV_HTTP_REQUEST: {
	char addr[32];
	struct http_message *hm = (struct http_message *) p;
	mg_sock_addr_to_str(&nc->sa, addr, sizeof(addr),
			    MG_SOCK_STRINGIFY_IP | MG_SOCK_STRINGIFY_PORT);
	ESP_LOGI(tag, "HTTP request from %s: %.*s %.*s",
		 addr, (int) hm->method.len,
		 hm->method.p, (int) hm->uri.len, hm->uri.p);
	mg_printf(nc, reply_fmt, addr);
	nc->flags |= MG_F_SEND_AND_CLOSE;
	break;
    }
    case MG_EV_CLOSE: {
	ESP_LOGI(tag, "Connection %p closed", nc);
	ConnectionContext* ctx = (ConnectionContext*)nc->user_data;
	delete ctx;
	break;
    }
    case MG_EV_HTTP_MULTIPART_REQUEST: {
	char addr[32];
	struct http_message *hm = (struct http_message *) p;
	mg_sock_addr_to_str(&nc->sa, addr, sizeof(addr),
			    MG_SOCK_STRINGIFY_IP | MG_SOCK_STRINGIFY_PORT);
	ESP_LOGI(tag, "HTTP mp req from %s: %.*s %.*s",
		 addr, (int) hm->method.len,
		 hm->method.p, (int) hm->uri.len, hm->uri.p);
	break;
    }
    case MG_EV_HTTP_MULTIPART_REQUEST_END:
    {
	ESP_LOGI(tag, "HTTP mp req end");
	mg_printf(nc,
		  "HTTP/1.1 200 OK\r\n"
		  "Content-Type: text/plain\r\n"
		  "Connection: close\r\n\r\n"
		  "Written POST data to a temp file\n\n");
	nc->flags |= MG_F_SEND_AND_CLOSE;
	break;
    }
    case MG_EV_HTTP_PART_BEGIN: {
	struct mg_http_multipart_part *mp =
	    (struct mg_http_multipart_part *) p;
	ESP_LOGI(tag, "part begin: key = %s", mp->var_name);
	break;
    }
    case MG_EV_HTTP_PART_DATA: {
	struct mg_http_multipart_part *mp =
	    (struct mg_http_multipart_part *) p;
	ESP_LOGI(tag, "part data: %d byte", mp->data.len);
	break;
    }
    case MG_EV_HTTP_PART_END: {
	ESP_LOGI(tag, "part end");
	break;
    }
    }
}

#include "ota.h"
class DLContext : public ConnectionContext {
public:
    bool image;
    OTA* ota;
    bool reply;
    
    DLContext() : image(false), ota(NULL), reply(false){};
    virtual ~DLContext(){
	if (ota){
	    end(false);
	}
    };

    void start(OTA* in){
	ota = in;
	image = true;
    };
    OTARESULT end(bool needCommit){
	OTARESULT rc = ::endOTA(ota, needCommit);
	ota = NULL;
	return rc;
    }
};

static const struct mg_str getMethod = MG_MK_STR("GET");
static const struct mg_str postMethod = MG_MK_STR("POST");

static int isEqual(const struct mg_str *s1, const struct mg_str *s2) {
    return s1->len == s2->len && memcmp(s1->p, s2->p, s2->len) == 0;
}

void HttpServerTask::downloadHandler(struct mg_connection *nc,
					  int ev, void *p) {
    const char* domain = "opiopan";
    const char* invReqMsg =
	"HTTP/1.1 200 OK\r\n"
	"Content-Type: text/plain\r\n"
	"Connection: close\r\n\r\n"
	"need to spcecify valid params and request as post\r\n";
    
    if (ev == MG_EV_HTTP_REQUEST){
	ESP_LOGI(tag, "HTTP dowonload req");
	ESP_LOGE(tag, "invalid request");
	mg_printf(nc, invReqMsg);
	nc->flags |= MG_F_SEND_AND_CLOSE;
    }else if (ev == MG_EV_HTTP_MULTIPART_REQUEST){
	ESP_LOGI(tag, "HTTP download mpart req");
	struct http_message *hm = (struct http_message *) p;
	if (!isEqual(&hm->method, &postMethod)){
	    ESP_LOGE(tag, "invalid request");
	    mg_printf(nc, invReqMsg);
	    nc->flags |= MG_F_SEND_AND_CLOSE;
	    return;
	}

	static const char * domain = "opiopan";
	FILE* fp = htdigestfs_fp();
	fseek(fp, 0, SEEK_SET);
	if (!mg_http_check_digest_auth(hm, domain, fp)){
	    ESP_LOGE(tag, "authorize failed");
	    mg_http_send_digest_auth_request(nc, domain);
	    nc->flags |= MG_F_SEND_AND_CLOSE;;
	    return;
	}
	ESP_LOGI(tag, "user authorized");

	mg_str* sizeStr = mg_get_http_header(hm, "X-OTA-Image-Size");
	size_t imageSize = 0;
	if (sizeStr){
	    for (int i = 0; i < sizeStr->len; i++){
		int digit = sizeStr->p[i];
		if (digit >= '0' && digit <= '9'){
		    imageSize *= 10;
		    imageSize += digit - '0';
		}else{
		    imageSize = 0;
		    break;
		}
	    }
	    ESP_LOGI(tag, "image size: %d", imageSize);
	}
	
	DLContext* ctx = new DLContext();
	nc->user_data = ctx;
	OTA* ota = NULL;
	if (startOTA("/spiffs/public-key.pem", imageSize, &ota)
	    != OTA_SUCCEED){
	    ESP_LOGE(tag, "downloading in parallel");
	    mg_printf(
		nc,
		"HTTP/1.1 200 OK\r\n"
		"Content-Type: text/plain\r\n"
		"Connection: close\r\n\r\n"
		"firmware downloading is proceeding in parallel\r\n");
	    nc->flags |= MG_F_SEND_AND_CLOSE;
	    return;
	}
	ctx->start(ota);
	mg_str* value = mg_get_http_header(hm, "Expect");
	static const mg_str CONTINUE = MG_MK_STR("100-continue");
	if (value != NULL && isEqual(value, &CONTINUE)) {
	    ESP_LOGI(tag, "response 100 continue");
	    mg_printf(nc,"HTTP/1.1 100 continue\r\n\r\n");
	}
    }else if (ev == MG_EV_HTTP_MULTIPART_REQUEST_END){
	DLContext* ctx = (DLContext*)nc->user_data;
	if (ctx && !ctx->image){
	    ESP_LOGE(tag, "no image part");
	    mg_http_send_digest_auth_request(nc, domain);
	    nc->flags |= MG_F_SEND_AND_CLOSE;
	    return;
	}
	ESP_LOGI(tag, "end mpart req");
	nc->flags |= MG_F_SEND_AND_CLOSE;
    }else if (ev == MG_EV_HTTP_PART_BEGIN) {
	struct mg_http_multipart_part *mp =
	    (struct mg_http_multipart_part *) p;
	DLContext* ctx = (DLContext*)nc->user_data;
	if (ctx && ctx->ota && strcmp(mp->var_name, "image") == 0){
	    ESP_LOGI(tag, "begin update firmware");
	}
    }else if (ev == MG_EV_HTTP_PART_DATA){
	DLContext* ctx = (DLContext*)nc->user_data;
	if (ctx && ctx->ota){
	    struct mg_http_multipart_part *mp =
		(struct mg_http_multipart_part *) p;
	    //ESP_LOGI(tag, "update data: %d bytes", mp->data.len);
	    OTARESULT rc = ctx->ota->addDataFlagment(mp->data.p,
						     mp->data.len);
	    if (rc != OTA_SUCCEED){
		ESP_LOGE(tag, "update data failed");
		mg_printf(
		    nc,
		    "HTTP/1.1 200 OK\r\n"
		    "Content-Type: text/plain\r\n"
		    "Connection: close\r\n\r\n"
		    "update data failed: 0x%x\r\n", rc);
		//nc->flags |= MG_F_SEND_AND_CLOSE;
		ctx->reply = true;
		ctx->end(false);
		return;
	    }
	}
    }else if (ev == MG_EV_HTTP_PART_END){
	DLContext* ctx = (DLContext*)nc->user_data;
	if (ctx && ctx->ota){
    	    ESP_LOGI(tag, "end update firmware");
	    OTARESULT rc = ctx->end(true);
	    if (rc != OTA_SUCCEED){
		ESP_LOGE(tag, "commit failed");
		mg_printf(
		    nc,
		    "HTTP/1.1 200 OK\r\n"
		    "Content-Type: text/plain\r\n"
		    "Connection: close\r\n\r\n"
		    "update data failed: 0x%x\r\n", rc);
		nc->flags |= MG_F_SEND_AND_CLOSE;
		return;
	    }
	    mg_printf(nc,
		      "HTTP/1.1 200 OK\r\n"
		      "Content-Type: text/plain\r\n"
		      "Connection: close\r\n\r\n"
		      "firmware updating finished\r\n");
	    nc->flags |= MG_F_SEND_AND_CLOSE;
	    rebootIn(2000);
	}
    }
}

HttpServerTask* server;

bool startHttpServer(){
    if (server == NULL){
	server = new HttpServerTask();
	server->init(HTTP_SERVER_PORT);
	server->start();
	ESP_LOGI(tag, "HTTP server has been started. port: %s",
		 HTTP_SERVER_PORT);

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

	FILE* fp = htdigestfs_fp();
	char buf[64];
	int rc = fread(buf, 1, sizeof(buf), fp);
	if (rc > 0){
	    buf[rc] = 0;
	    ESP_LOGI(tag, "htdigest: %s", buf);
	}
    }
    return true;
}
