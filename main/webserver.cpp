#include <esp_log.h>
#include <string.h>
#include <string>
#include <Task.h>
#include <lwip/sockets.h>
#include <lwip/netdb.h>
#include <mongoose.h>
#include "Mutex.h"
#include "webserver.h"
#include "boardconfig.h"

#include "sdkconfig.h"

const char* tag = "webserver";

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

    struct mg_http_multipart_part *mp = (struct mg_http_multipart_part *) p;

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
	break;
    }
    case MG_EV_HTTP_MULTIPART_REQUEST: {
	char addr[32];
	struct http_message *hm = (struct http_message *) p;
	mg_sock_addr_to_str(&nc->sa, addr, sizeof(addr),
			    MG_SOCK_STRINGIFY_IP | MG_SOCK_STRINGIFY_PORT);
	ESP_LOGI(tag, "HTTP multipart request from %s: %.*s %.*s",
		 addr, (int) hm->method.len,
		 hm->method.p, (int) hm->uri.len, hm->uri.p);
	break;
    }
    case MG_EV_HTTP_MULTIPART_REQUEST_END:
    {
	ESP_LOGI(tag, "HTTP multipart request end");
	mg_printf(nc,
		  "HTTP/1.1 200 OK\r\n"
		  "Content-Type: text/plain\r\n"
		  "Connection: close\r\n\r\n"
		  "Written POST data to a temp file\n\n");
	nc->flags |= MG_F_SEND_AND_CLOSE;
	break;
    }
    case MG_EV_HTTP_PART_BEGIN: {
	ESP_LOGI(tag, "HTTP PART BEGIN");
	break;
    }
    case MG_EV_HTTP_PART_DATA: {
	ESP_LOGI(tag, "HTTP PART DATA: %d byte", mp->data.len);
	break;
    }
    case MG_EV_HTTP_PART_END: {
	ESP_LOGI(tag, "HTTP PART END");
	break;
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
    }
    return true;
}
