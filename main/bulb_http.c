#include "bulb_http.h"
#include "esp_http_server.h"
#include "freertos/queue.h"
#include "lwip/sockets.h"
#include "esp_netif_ip_addr.h"
#include "esp_timer.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
struct Bulb_Http {httpd_handle_t server;QueueHandle_t queue;esp_netif_t* sta;uint8_t token[32];bool enabled,online;unsigned handlers;uint64_t retry_at;};
static portMUX_TYPE job_lock=portMUX_INITIALIZER_UNLOCKED;
static void release(Bulb_Http_Job* j) {
    portENTER_CRITICAL(&job_lock);bool last=!--j->references;portEXIT_CRITICAL(&job_lock);
    if(last){if(j->data)bulb_wipe(j->data,j->size);free(j->data);vSemaphoreDelete(j->done);bulb_wipe(j,sizeof(*j));free(j);}
}
static bool authorized(httpd_req_t* request) {
    Bulb_Http h=request->user_ctx;bool enabled;
    portENTER_CRITICAL(&job_lock);enabled=h->online&&(h->enabled||request->method==HTTP_GET);portEXIT_CRITICAL(&job_lock);
    if(!enabled)return false;
    struct sockaddr_storage addr;socklen_t len=sizeof(addr);
    if(getsockname(httpd_req_to_sockfd(request),(struct sockaddr*)&addr,&len))return false;
    bool local=false;esp_netif_ip_info_t v4;
    if(esp_netif_get_ip_info(h->sta,&v4)==ESP_OK&&v4.ip.addr){
        if(addr.ss_family==AF_INET)local=((struct sockaddr_in*)&addr)->sin_addr.s_addr==v4.ip.addr;
        else if(addr.ss_family==AF_INET6){const uint8_t* ip=((struct sockaddr_in6*)&addr)->sin6_addr.s6_addr;
            static const uint8_t mapped[]={0,0,0,0,0,0,0,0,0,0,255,255};local=!memcmp(ip,mapped,12)&&!memcmp(ip+12,&v4.ip.addr,4);}
    }
    if(addr.ss_family==AF_INET6&&!local){esp_ip6_addr_t addresses[CONFIG_LWIP_IPV6_NUM_ADDRESSES];int n=esp_netif_get_all_ip6(h->sta,addresses);
        for(int i=0;i<n&&i<CONFIG_LWIP_IPV6_NUM_ADDRESSES;++i)local|=!memcmp(&((struct sockaddr_in6*)&addr)->sin6_addr,addresses[i].addr,16);}
    if(!local)return false;
    char bearer[72];uint8_t token[32];memset(token,0,sizeof(token));
    if(httpd_req_get_hdr_value_len(request,"Authorization")!=71||httpd_req_get_hdr_value_str(request,"Authorization",bearer,sizeof(bearer))!=ESP_OK||memcmp(bearer,"Bearer ",7))return false;
    bool valid=true;
    for(unsigned i=0;i<64;++i){char c=bearer[i+7];unsigned n=c>='0'&&c<='9'?(unsigned)(c-'0'):c>='a'&&c<='f'?(unsigned)(c-'a'+10):16;if(n==16)valid=false;else token[i/2]|=(uint8_t)(n<<((i&1)?0:4));}
    valid=bulb_secret_equal(token,h->token,32)&&valid;bulb_wipe(token,sizeof(token));bulb_wipe(bearer,sizeof(bearer));return valid;
}
static const char* status(int code){return code==200?"200 OK":code==202?"202 Accepted":code==400?"400 Bad Request":code==403?"403 Forbidden":code==409?"409 Conflict":code==413?"413 Content Too Large":code==503?"503 Service Unavailable":"500 Internal Server Error";}
static esp_err_t reply(httpd_req_t* r,int code,const char* json){httpd_resp_set_status(r,status(code));httpd_resp_set_type(r,"application/json");httpd_resp_set_hdr(r,"Cache-Control","no-store");return httpd_resp_sendstr(r,json);}
static esp_err_t handle_request(httpd_req_t* request) {
    if(!authorized(request))return reply(request,403,"{\"error\":\"factory authorization required\"}");
    Bulb_Http h=request->user_ctx;Bulb_Http_Kind kind=BULB_HTTP_STATUS;
    if(!strcmp(request->uri,"/api/v1/factory/identity"))kind=BULB_HTTP_IDENTITY;
    else if(!strcmp(request->uri,"/api/v1/wifi"))kind=BULB_HTTP_WIFI;
    else if(!strcmp(request->uri,"/api/v1/reset"))kind=BULB_HTTP_RESET;
    else if(!strcmp(request->uri,"/api/v1/ota/query"))kind=BULB_HTTP_QUERY;
    size_t size=request->content_len;
    if(size>BULB_FACTORY_MAX)return reply(request,413,"{\"error\":\"body limit\"}");
    if((kind==BULB_HTTP_IDENTITY||kind==BULB_HTTP_WIFI)?!size:size!=0)return reply(request,400,"{\"error\":\"invalid body\"}");
    Bulb_Http_Job* j=calloc(1,sizeof(*j));if(!j)return reply(request,503,"{\"error\":\"memory\"}");
    j->done=xSemaphoreCreateBinary();j->data=size?calloc(1,size):NULL;j->size=size;j->kind=kind;j->references=1;
    if(!j->done||(size&&!j->data)){if(j->done)vSemaphoreDelete(j->done);free(j->data);free(j);return reply(request,503,"{\"error\":\"memory\"}");}
    size_t used=0;unsigned timeouts=0;
    while(used<size){int n=httpd_req_recv(request,(char*)j->data+used,size-used);if(n==HTTPD_SOCK_ERR_TIMEOUT&&timeouts++<2)continue;if(n<=0){release(j);return ESP_FAIL;}used+=(size_t)n;}
    portENTER_CRITICAL(&job_lock);++j->references;portEXIT_CRITICAL(&job_lock);
    if(xQueueSend(h->queue,&j,0)!=pdTRUE){release(j);release(j);return reply(request,503,"{\"error\":\"busy\"}");}
    esp_err_t result=xSemaphoreTake(j->done,pdMS_TO_TICKS(15000))==pdTRUE?reply(request,j->code,j->response):reply(request,202,"{\"status\":\"accepted; completion may follow disconnect\"}");
    release(j);return result;
}
static esp_err_t handle(httpd_req_t* request){
    Bulb_Http h=request->user_ctx;portENTER_CRITICAL(&job_lock);++h->handlers;portEXIT_CRITICAL(&job_lock);
    esp_err_t r=handle_request(request);
    portENTER_CRITICAL(&job_lock);--h->handlers;portEXIT_CRITICAL(&job_lock);return r;
}
static bool start_server(Bulb_Http h){
    httpd_config_t config=HTTPD_DEFAULT_CONFIG();config.stack_size=5120;config.max_open_sockets=2;config.max_uri_handlers=5;config.recv_wait_timeout=2;config.send_wait_timeout=2;config.lru_purge_enable=true;
    if(httpd_start(&h->server,&config)!=ESP_OK)goto fail;
    static const struct {const char* uri;httpd_method_t method;} routes[]={{"/api/v1/status",HTTP_GET},{"/api/v1/factory/identity",HTTP_POST},{"/api/v1/wifi",HTTP_POST},{"/api/v1/reset",HTTP_POST},{"/api/v1/ota/query",HTTP_POST}};
    for(unsigned i=0;i<5;++i){httpd_uri_t route={.uri=routes[i].uri,.method=routes[i].method,.handler=handle,.user_ctx=h};if(httpd_register_uri_handler(h->server,&route)!=ESP_OK)goto fail;}
    return true;
fail:
    if(h->server)(void)httpd_stop(h->server);
    h->server=NULL;return false;
}
Bulb_Http bulb_http_new(const Bulb_Bootstrap* b,esp_netif_t* sta){
    if(!b||!sta)return NULL;
    Bulb_Http h=calloc(1,sizeof(*h));if(!h)return NULL;
    h->sta=sta;memcpy(h->token,b->token,32);h->queue=xQueueCreate(1,sizeof(Bulb_Http_Job*));
    if(!h->queue){bulb_wipe(h,sizeof(*h));free(h);return NULL;}return h;
}
void bulb_http_process(Bulb_Http h,bool connected,bool window){
    if(!h)return;
    portENTER_CRITICAL(&job_lock);h->online=connected;h->enabled=connected&&window;unsigned handlers=h->handlers;portEXIT_CRITICAL(&job_lock);
    uint64_t now=(uint64_t)esp_timer_get_time()/1000;
    if(connected&&!h->server&&now>=h->retry_at){bool ready=start_server(h);h->retry_at=now+1000;
#if CONFIG_BULB_DEVELOP
        if(ready)printf("BULB_HTTP online=1\n");
#else
        (void)ready;
#endif
    }else if(!connected&&h->server&&!handlers){(void)httpd_stop(h->server);h->server=NULL;
#if CONFIG_BULB_DEVELOP
        printf("BULB_HTTP online=0\n");
#endif
    }
}
Bulb_Http_Job* bulb_http_take(Bulb_Http h){Bulb_Http_Job* j=NULL;if(h)(void)xQueueReceive(h->queue,&j,0);return j;}
void bulb_http_finish(Bulb_Http_Job* j,int code,const char* text){j->code=code;strlcpy(j->response,text,sizeof(j->response));xSemaphoreGive(j->done);release(j);}
