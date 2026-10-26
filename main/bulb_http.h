#ifndef BULB_HTTP_H_INCLUDED
#define BULB_HTTP_H_INCLUDED 1
#include "factory_codec.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_netif.h"
typedef enum {BULB_HTTP_STATUS,BULB_HTTP_IDENTITY,BULB_HTTP_WIFI,BULB_HTTP_RESET,BULB_HTTP_QUERY} Bulb_Http_Kind;
typedef struct {
    Bulb_Http_Kind kind;uint8_t* data;size_t size;
    int code;char response[256];
    SemaphoreHandle_t done;unsigned references;
} Bulb_Http_Job;
typedef struct Bulb_Http* Bulb_Http;
/* Connected STA only, unique per-device Bearer token, bounded body, one
 * queued job plus one executing job. The owner never retains an HTTP request/socket pointer.
 * A client timeout releases only its reference: accepted work remains owned.
 * Changes require an open maintenance window; online status still requires
 * the Bearer token. No secret read endpoint. */
Bulb_Http bulb_http_new(const Bulb_Bootstrap*,esp_netif_t* sta);
/* Owner poll: no server task/listener before IPv6-ready STA association. Stop
 * admission immediately offline, then stop the server after its handlers exit.
 * Accepted jobs keep their original reference/atomic completion ownership. */
void bulb_http_process(Bulb_Http,bool connected,bool administrative_window);
Bulb_Http_Job* bulb_http_take(Bulb_Http);
void bulb_http_finish(Bulb_Http_Job*,int code,const char* response);
#endif
