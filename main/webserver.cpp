#include <esp_log.h>
#include <string.h>
#include <string>
#include <Task.h>
#include <lwip/sockets.h>
#include <lwip/netdb.h>
#include <mongoose.h>
#include "Mutex.h"
#include "webserver.h"
#include "htdigestfs.h"
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
    static void loginHandler(struct mg_connection *nc, int ev, void *p);
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
    mg_register_http_endpoint(nc, "/login", loginHandler);
    
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

void HttpServerTask::loginHandler(struct mg_connection *nc,
					  int ev, void *p) {
    if (ev == MG_EV_HTTP_REQUEST || ev == MG_EV_HTTP_MULTIPART_REQUEST){
	ESP_LOGI(tag, "HTTP login req");

	static const char * domain = "opiopan";
	struct http_message *hm = (struct http_message *) p;
	FILE* fp = htdigestfs_fp();
	fseek(fp, 0, SEEK_SET);
	if (mg_http_check_digest_auth(hm, domain, fp)){
	    mg_printf(nc,
		      "HTTP/1.1 200 OK\r\n"
		      "Content-Type: text/plain\r\n"
		      "Connection: close\r\n\r\n"
		      "Login request is authorized\n\n");
	}else{
	    mg_http_send_digest_auth_request(nc, domain);
	}
	nc->flags |= MG_F_SEND_AND_CLOSE;
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
