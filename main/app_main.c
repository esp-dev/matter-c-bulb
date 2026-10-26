#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_event.h"
#include "esp_wifi.h"
#include "esp_timer.h"
#include "esp_system.h"
#include "esp_random.h"
#include "esp_heap_caps.h"
#include "esp_netif_ip_addr.h"
#include "esp_app_desc.h"
#include "esp_ota_ops.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "mdns.h"
#include "psa/crypto.h"
#include "crypto/matter_crypto_mbedtls.h"
#include "crypto/matter_attestation_cert.h"
#include "runtime/matter_runtime.h"
#include "runtime/matter_host_limits.h"
#include "matter_discovery.h"
#include "matter_idf_wifi.h"
#include "matter_idf_multicast.h"
#include "matter_idf_network.h"
#include "matter_idf_store.h"
#include "matter_idf_discovery.h"
#include "matter_idf_resolver.h"
#include "matter_idf_ota.h"
#include "bulb_store.h"
#include "bulb_http.h"
#include "bulb_led.h"
#include "bulb_clock.h"
#include "bulb_country.h"

typedef struct {Matter_T owner;Matter_Discovery_T record;Matter_Idf_Discovery_T backend;bool stopping;uint64_t retry;} Ad;
typedef struct {
    esp_netif_t *sta;uint8_t mac[6];Bulb_Bootstrap bootstrap;
    Bulb_Factory factory;uint8_t* identity;size_t identity_size;bool has_identity;
    Bulb_Store appstore;Bulb_Http http;Bulb_Led led;Bulb_Http_Job* job;
    Matter_Idf_Wifi_T wifi;Matter_Idf_Multicast_T multicast;
    Matter_Idf_Store_T store;Matter_Idf_Ota_T ota;Matter_Idf_Resolver_T resolver;
    Matter_Crypto_Mbedtls_T crypto;Matter_Runtime_T runtime;Matter_T owner;
    Matter_Light_T light;Matter_Light_Groups_T groups;Matter_Idf_Network_T udp;Matter_Events_T events;
    Matter_Runtime_Config_T config;Matter_Root_Config_T root;Matter_Wifi_Settings_T initial;
    Ad ads[6];uint32_t light_versions[4];uint8_t cm;uint16_t disc;bool started,draining,erase,reboot,ota_ready,boot_counter;
    uint64_t window,button,boot,log_at,reboot_at;bool button_used;
    esp_ip6_addr_t addresses[CONFIG_LWIP_IPV6_NUM_ADDRESSES];
    Bulb_Clock clock;uint64_t clock_retry;
    Bulb_Country country;uint64_t country_retry;
} App;
static uint64_t now(void){return (uint64_t)esp_timer_get_time()/1000;}
static void park(unsigned line) {
#if CONFIG_BULB_DEVELOP
    printf("BULB_FAULT line=%u; persistent state retained\n",line);
#else
    (void)line;
#endif
    /* No automatic NVS erase or reboot loop after corruption. */
    for(;;)vTaskDelay(pdMS_TO_TICKS(1000));
}
#define REQUIRE(x) do{if(!(x))park(__LINE__);}while(0)
#if CONFIG_BULB_DEVELOP
static void core_log(void* u,const char* s){(void)u;
    printf("MATTER %s",s);
}
static void heap_log(App* a,const char* phase){
    const Matter_Fabric_View_T* v=matter_runtime_fabrics(a->runtime);esp_ip6_addr_t ip;
    printf("BULB_HEAP free=%u min=%u largest=%u stack_free=%u fabrics=%u online=%u phase=%s\n",(unsigned)heap_caps_get_free_size(MALLOC_CAP_8BIT),(unsigned)heap_caps_get_minimum_free_size(MALLOC_CAP_8BIT),(unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT),(unsigned)uxTaskGetStackHighWaterMark(NULL),v?(unsigned)v->count:0,esp_netif_get_ip6_linklocal(a->sta,&ip)==ESP_OK,phase);
}
#endif
static void text(Matter_Root_Text_T* out,const char* value){out->size=strlen(value);REQUIRE(out->size<=sizeof(out->data));memcpy(out->data,value,out->size);}
static Matter_Result_T entropy(void* u,uint8_t* p,size_t n){(void)u;esp_fill_random(p,n);return MATTER_OK;}
/* No trusted RTC/time synchronization is configured. Certificate UTC is the
 * durable LKG floor, not the running build-time clock presented as real UTC. */
static Matter_Result_T utc(void* u,Matter_Cert_Time_T* out){App* a=u;time_t t=(time_t)a->clock.floor;struct tm v;if(!gmtime_r(&t,&v)||v.tm_year<120)return MATTER_OFFLINE;*out=(Matter_Cert_Time_T){v.tm_year+1900,v.tm_mon+1,v.tm_mday,v.tm_hour,v.tm_min,v.tm_sec};return MATTER_OK;}
static bool store_clock(void* u,uint64_t floor){
    (void)u;nvs_handle_t h;if(nvs_open("app",NVS_READWRITE,&h)!=ESP_OK)return false;
    esp_err_t e=nvs_set_u64(h,"cert_lkg",floor);if(e==ESP_OK)e=nvs_commit(h);nvs_close(h);return e==ESP_OK;
}
static void load_clock(App* a){
    nvs_handle_t h;REQUIRE(nvs_open("app",NVS_READWRITE,&h)==ESP_OK);uint64_t stored=0;
    esp_err_t e=nvs_get_u64(h,"cert_lkg",&stored);nvs_close(h);REQUIRE(e==ESP_OK||e==ESP_ERR_NVS_NOT_FOUND);
    /* Matter operational timestamps are uint32 seconds since 2000. */
    REQUIRE(!stored||(stored>=946684800&&stored<=UINT64_C(946684800)+UINT32_MAX));
    a->clock.floor=stored>BULB_BUILD_UTC?stored:BULB_BUILD_UTC;
    if(a->clock.floor!=stored)REQUIRE(store_clock(a,a->clock.floor));
    struct timeval clock={.tv_sec=(time_t)a->clock.floor};REQUIRE(settimeofday(&clock,NULL)==0);
}
static Matter_Result_T timestamp(void* u,uint32_t* out){(void)u;time_t t=time(NULL);if(t<946684800||(uint64_t)t-946684800>UINT32_MAX)return MATTER_OFFLINE;*out=(uint32_t)(t-946684800);return MATTER_OK;}
static Matter_Result_T regulatory(void* u,const Matter_Root_Settings_T* s,uint8_t candidate,bool* supported){
    (void)u;(void)candidate;
    /* Validation only: apply the durably published Location outside IM. */
    *supported=bulb_country_supported(s->location);
    return MATTER_OK;
}
static bool radio_country(void* user,const char code[3]){
    /* 802.11d AUTO passively discovers channels12..14 and adopts AP country
     * information. A commissioner's US fallback must not hide a channel13 AP. */
    (void)user;esp_err_t e=esp_wifi_set_country_code(code,true);
#if CONFIG_BULB_DEVELOP
    printf("BULB_RADIO country=%s ieee80211d=1 result=%d\n",code,(int)e);
#endif
    return e==ESP_OK;
}
static void country_sync(App* a){
    if(now()<a->country_retry)return;
    Matter_Im_Value_T value;const Matter_Im_Path_T path={0,0x28,6};
    REQUIRE(matter_root_read_fabric(matter_runtime_root(a->runtime),&path,0,false,&value)==MATTER_IM_SUCCESS&&value.type==MATTER_TLV_UTF8&&value.size==2);
    if(!bulb_country_apply(&a->country,value.data,radio_country,a))a->country_retry=now()+1000;
}
static Matter_Result_T network(void* u,Matter_Diagnostics_Interface_T* out,size_t capacity,size_t* count){
    App* a=u;*count=1;if(!capacity)return MATTER_LIMIT_REACHED;memset(out,0,sizeof(*out));memcpy(out->name,"wifi",4);out->name_size=4;out->type=1;out->reachable4=out->reachable6=-1;memcpy(out->hardware+2,a->mac,6);out->operational=esp_netif_is_netif_up(a->sta);
    esp_netif_ip_info_t ip;if(esp_netif_get_ip_info(a->sta,&ip)==ESP_OK&&ip.ip.addr){out->ipv4_count=1;memcpy(out->ipv4[0],&ip.ip.addr,4);}
    int n=esp_netif_get_all_ip6(a->sta,a->addresses);if(n<0||n>8)return MATTER_PORT_ERROR;out->ipv6_count=n;for(int i=0;i<n;++i)memcpy(out->ipv6[i],a->addresses[i].addr,16);return MATTER_OK;
}
static Matter_Result_T event_load(void* u,uint64_t* high){return matter_store_host_event_load(matter_runtime_store(((App*)u)->runtime),high);}
static Matter_Result_T event_store(void* u,uint64_t high){return matter_store_host_event_store(matter_runtime_store(((App*)u)->runtime),high);}
static void received(void* u,const Matter_Udp_Datagram_T* d){App* a=u;if(a->runtime)matter_runtime_received(a->runtime,d);}
static void sent(void* u,uint64_t id,Matter_Operation_Result_T result){App* a=u;if(a->runtime)matter_runtime_sent(a->runtime,id,result);}
static bool validate_identity(App* a,const uint8_t* p,size_t n,Bulb_Factory* f){
    if(!bulb_factory_decode(p,n,f))return false;
    Matter_Crypto_Port_T crypto;Matter_Credentials_Port_T credentials;Matter_Attestation_Identity_T identity;uint8_t public_key[65];
    if(matter_crypto_mbedtls_port(a->crypto,&crypto)!=MATTER_OK||matter_crypto_mbedtls_credentials(a->crypto,&credentials)!=MATTER_OK)return false;
    bool valid=matter_attestation_cert_verify(&credentials,(Matter_Der_T){f->dac.data,f->dac.size},(Matter_Der_T){f->pai.data,f->pai.size},(Matter_Der_T){f->paa.data,f->paa.size},f->vendor,f->product,&identity)==MATTER_OK&&crypto.p256_base(crypto.user,f->private_key.data,32,public_key)==MATTER_OK&&crypto.equal(public_key,identity.public_key,65);
    bulb_wipe(public_key,sizeof(public_key));return valid;
}
static void from_network(Matter_Wifi_Settings_T* s,const Bulb_Network* n){memset(s,0,sizeof(*s));s->enabled=n->enabled;s->count=n->ssid_size?1:0;if(s->count){s->networks[0].id.size=n->ssid_size;memcpy(s->networks[0].id.bytes,n->ssid,n->ssid_size);s->networks[0].credential_size=n->password_size;memcpy(s->networks[0].credentials,n->password,n->password_size);}}
static void boot_wifi(App* a){
    REQUIRE(esp_netif_init()==ESP_OK&&esp_event_loop_create_default()==ESP_OK);a->sta=esp_netif_create_default_wifi_sta();REQUIRE(a->sta);
    wifi_init_config_t init=WIFI_INIT_CONFIG_DEFAULT();REQUIRE(esp_wifi_init(&init)==ESP_OK&&esp_wifi_set_storage(WIFI_STORAGE_RAM)==ESP_OK&&esp_wifi_set_mode(WIFI_MODE_STA)==ESP_OK&&esp_wifi_set_country_code("01",true)==ESP_OK);
    REQUIRE(esp_wifi_get_mac(WIFI_IF_STA,a->mac)==ESP_OK&&bulb_secret_equal(a->mac,a->bootstrap.mac,6));
    REQUIRE(esp_wifi_start()==ESP_OK);
    uint64_t end=now()+3000;while(esp_netif_get_netif_impl_index(a->sta)<=0&&now()<end)vTaskDelay(1);REQUIRE(esp_netif_get_netif_impl_index(a->sta)>0);
    a->wifi=matter_idf_wifi_new(a->sta,&a->initial,esp_random(),now());REQUIRE(a->wifi);REQUIRE(mdns_init()==ESP_OK);
    /* Match the immutable hostname generated by matter_discovery exactly. A
     * service for an unregistered delegated hostname has no A/AAAA answers. */
    char host[13];for(unsigned i=0;i<6;++i)snprintf(host+2*i,3,"%02X",a->mac[i]);REQUIRE(mdns_hostname_set(host)==ESP_OK);
}
static void configure(App* a){
    Bulb_Factory* f=&a->factory;Matter_Root_Config_T* r=&a->root;Matter_Runtime_Config_T* c=&a->config;
    text(&r->vendor_name,f->vendor_name);text(&r->product_name,f->product_name);text(&r->hardware_string,"ESP32-C6 GPIO8");text(&r->software_string,esp_app_get_description()->version);
    uint8_t* unique=NULL;size_t size=0;esp_err_t e=bulb_nvs_read("nvs","app","unique",&unique,&size,32);
    if(e==ESP_ERR_NVS_NOT_FOUND){unique=calloc(1,32);REQUIRE(unique);uint8_t random[16];esp_fill_random(random,sizeof(random));static const char hex[]="0123456789ABCDEF";for(unsigned i=0;i<16;++i){unique[2*i]=hex[random[i]>>4];unique[2*i+1]=hex[random[i]&15];}size=32;REQUIRE(bulb_nvs_write("nvs","app","unique",unique,size)==ESP_OK);}
    else REQUIRE(e==ESP_OK&&size==32);
    r->unique_id.size=size;memcpy(r->unique_id.data,unique,size);free(unique);
    r->vendor_id=f->vendor;r->product_id=f->product;r->hardware_version=f->hardware;r->software_version=matter_idf_ota_version(a->ota);r->configuration_version=1;r->basic_version=esp_random();r->descriptor_version=esp_random();r->capabilities=(Matter_Root_Capabilities_T)MATTER_HOST_CAPABILITIES;r->settings.location[0]=r->settings.location[1]='X';
    c->identity=r;c->user=a;c->utc=utc;c->timestamp=timestamp;c->regulatory=regulatory;c->ble_commissioning_only=true;
#if CONFIG_BULB_DEVELOP
    c->log=core_log;
#endif
    c->wifi=matter_idf_wifi_port(a->wifi);c->wifi_config=(Matter_Wifi_Config_T){4,10,20,1,esp_random(),1};c->storage=matter_idf_store_port(a->store);c->boot_reason=esp_reset_reason()==ESP_RST_POWERON?1:esp_reset_reason()==ESP_RST_SW?4:0;c->reboot=true;
    c->iface=(Matter_Diagnostics_Interface_T){.name="wifi",.name_size=4,.type=1,.reachable4=-1,.reachable6=-1};c->network=(Matter_Diagnostics_Network_Port_T){a,network};
    REQUIRE(matter_crypto_mbedtls_port(a->crypto,&c->crypto)==MATTER_OK&&matter_crypto_mbedtls_credentials(a->crypto,&c->credentials)==MATTER_OK);
    c->attestation.identity=(Matter_Opcreds_Config_T){.dac={f->dac.data,f->dac.size},.pai={f->pai.data,f->pai.size},.certification_declaration={f->cd.data,f->cd.size}};c->attestation.trusted_paa=(Matter_Der_T){f->paa.data,f->paa.size};c->attestation.dac_private=f->private_key.data;
    c->groupcast.model=a->groups;c->groupcast.privacy=matter_crypto_mbedtls_privacy;
}
static bool ad_stop(Ad* ad){
    ad->stopping=true;if(ad->backend){Matter_Mdns_T m=matter_idf_discovery_module(ad->backend);(void)matter_mdns_stop(m);(void)matter_mdns_process(m);(void)matter_idf_discovery_process(ad->backend);if(matter_idf_discovery_delete(ad->backend)!=MATTER_OK)return false;ad->backend=NULL;}
    if(ad->owner){REQUIRE(matter_stop(ad->owner)==MATTER_OK&&matter_delete(ad->owner)==MATTER_OK);ad->owner=NULL;}matter_discovery_delete(ad->record);ad->record=NULL;ad->stopping=false;return true;
}
static bool ad_start(Ad* ad,Matter_Discovery_T record){ad->record=record;if(!record)return false;ad->owner=matter_new(NULL);if(ad->owner&&matter_start(ad->owner)==MATTER_OK){ad->backend=matter_idf_discovery_new(ad->owner,matter_discovery_record(record));if(ad->backend&&matter_mdns_start(matter_idf_discovery_module(ad->backend))==MATTER_OK)return true;}REQUIRE(ad_stop(ad));return false;}
static bool advertisements(App* a,bool stop){
    uint16_t disc=a->factory.discriminator;uint8_t cm=stop?0:matter_runtime_commissioning_mode(a->runtime,&disc);bool drained=true;
    matter_idf_network_window(a->udp,cm!=0,disc);
    Ad* ad=&a->ads[0];if(ad->record&&(stop||cm!=a->cm||disc!=a->disc||ad->stopping)){if(!ad_stop(ad))drained=false;}
    /* Commissionable discovery is BLE only. Operational DNS-SD remains below. */
    const Matter_Fabric_View_T* fabrics=matter_runtime_fabrics(a->runtime);
    for(unsigned i=0;i<5;++i){ad=&a->ads[i+1];const Matter_Fabric_Record_T* f=fabrics?&fabrics->records[i]:NULL;bool wanted=!stop&&f&&f->index;
        if(ad->record&&(ad->stopping||!wanted||strcmp(matter_discovery_record(ad->record)->instance,f->discovery_instance)))if(!ad_stop(ad))drained=false;
        if(wanted&&!ad->record&&now()>=ad->retry&&!ad_start(ad,matter_discovery_operational(a->mac,f->compressed_id,f->node_id,5540)))ad->retry=now()+1000;}
    for(unsigned i=0;i<6;++i)if(a->ads[i].backend){ad=&a->ads[i];if(now()<ad->retry)continue;Matter_Mdns_T m=matter_idf_discovery_module(ad->backend);Matter_Result_T r=matter_mdns_process(m);(void)matter_idf_discovery_process(ad->backend);if(r==MATTER_PORT_ERROR){(void)matter_mdns_retry(m);ad->retry=now()+1000;}}
    return drained;
}
static void start_matter(App* a){
    REQUIRE(bulb_store_init(&a->appstore));Matter_Light_Port_T lp={&a->appstore,bulb_store_light_load,bulb_store_light};esp_ota_img_states_t state;
    bool ota=esp_ota_get_state_partition(esp_ota_get_running_partition(),&state)==ESP_OK&&state==ESP_OTA_IMG_PENDING_VERIFY;
    a->light=matter_light_new(&lp,esp_random(),now(),ota);REQUIRE(a->light);a->multicast=matter_idf_multicast_new(a->sta);REQUIRE(a->multicast);
    Matter_Light_Groups_Config_T gc={.light=a->light,.endpoint=1,.version=esp_random(),.storage={&a->appstore,bulb_store_groups_load,bulb_store_groups},.store_light=bulb_store_joint,.multicast_user=a->multicast,.join=matter_idf_multicast_join,.leave=matter_idf_multicast_leave};a->groups=matter_light_groups_new(&gc);REQUIRE(a->groups);
    a->store=matter_idf_store_new("matter","runtime");a->ota=matter_idf_ota_new("matter","ota",a->factory.vendor,a->factory.product);a->resolver=matter_idf_resolver_new(a->sta);REQUIRE(a->store&&a->ota&&a->resolver);configure(a);
    a->runtime=matter_runtime_new(&a->config,a->factory.passcode,now());REQUIRE(a->runtime);bulb_wipe((void*)a->factory.private_key.data,32);a->config.attestation.dac_private=NULL;
    REQUIRE(matter_light_attach(a->light,matter_runtime_root(a->runtime),1)==MATTER_OK&&matter_light_groups_attach(a->groups,matter_runtime_root(a->runtime))==MATTER_OK);
    REQUIRE(matter_runtime_set_resumption(a->runtime,MATTER_CASE_RESUMPTION_CHIP)==MATTER_OK);
    REQUIRE(matter_runtime_set_certificate_time_mode(a->runtime,MATTER_CERT_TIME_LAST_KNOWN_GOOD)==MATTER_OK);
    Matter_Runtime_Ota_Port_T op=matter_idf_ota_port(a->ota);Matter_Resolver_Port_T rp=matter_idf_resolver_port(a->resolver);REQUIRE(matter_runtime_set_ota(a->runtime,&op)==MATTER_OK&&matter_runtime_set_resolver(a->runtime,&rp)==MATTER_OK);
    a->owner=matter_new(NULL);REQUIRE(a->owner&&matter_start(a->owner)==MATTER_OK);Matter_Udp_Listener_T listener={a,received,sent};a->udp=matter_idf_network_new(a->owner,a->sta,5540,a->factory.vendor,a->factory.product,&listener);REQUIRE(a->udp);
    Matter_Events_Port_T ep={a,event_load,event_store};a->events=matter_events_new(&ep);REQUIRE(a->events&&matter_runtime_start(a->runtime,a->owner,matter_idf_network_module(a->udp),a->events)==MATTER_OK);a->started=true;country_sync(a);
}
static void status(App* a,Bulb_Http_Job* job){char response[256];const Matter_Fabric_View_T* v=matter_runtime_fabrics(a->runtime);esp_ip6_addr_t ip;snprintf(response,sizeof(response),"{\"identity\":%s,\"fabrics\":%u,\"wifi\":%s,\"version\":%lu,\"free_heap\":%u,\"minimum_heap\":%u,\"largest_block\":%u}",a->has_identity?"true":"false",v?(unsigned)v->count:0,esp_netif_get_ip6_linklocal(a->sta,&ip)==ESP_OK?"true":"false",(unsigned long)(a->ota?matter_idf_ota_version(a->ota):0),(unsigned)heap_caps_get_free_size(MALLOC_CAP_8BIT),(unsigned)heap_caps_get_minimum_free_size(MALLOC_CAP_8BIT),(unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));bulb_http_finish(job,200,response);}
static void jobs(App* a){
    if(!a->job)a->job=bulb_http_take(a->http);
    Bulb_Http_Job* j=a->job;if(!j)return;
    if(j->kind==BULB_HTTP_STATUS){a->job=NULL;status(a,j);return;}
    if(now()>a->window||a->reboot){a->job=NULL;bulb_http_finish(j,409,"{\"error\":\"window closed or restarting\"}");return;}
    if(j->kind==BULB_HTTP_IDENTITY){
        const Matter_Fabric_View_T* v=matter_runtime_fabrics(a->runtime);Bulb_Factory factory;
        int code=v&&v->count?409:validate_identity(a,j->data,j->size,&factory)?200:400;
        if(a->runtime){Matter_Root_Fail_Safe_State_T fs;if(matter_root_fail_safe_state(matter_runtime_root(a->runtime),&fs)!=MATTER_OK||fs!=MATTER_ROOT_FAIL_SAFE_IDLE)code=409;}
        if(code==200){if(bulb_nvs_write("factory_data","identity","record",j->data,j->size)!=ESP_OK)code=500;else {a->reboot=true;a->draining=true;a->reboot_at=now()+1000;}}
        a->job=NULL;bulb_http_finish(j,code,code==200?"{\"status\":\"stored; restarting\"}":"{\"error\":\"identity rejected\"}");return;
    }
    if(j->kind==BULB_HTTP_QUERY){Matter_Result_T r=a->runtime?matter_runtime_ota_request_query(a->runtime):MATTER_INVALID_STATE;a->job=NULL;bulb_http_finish(j,r==MATTER_OK?202:409,r==MATTER_OK?"{\"status\":\"query scheduled\"}":"{\"error\":\"OTA unavailable or busy\"}");return;}
    if(j->kind==BULB_HTTP_WIFI){Bulb_Network network;if(!bulb_network_decode(j->data,j->size,&network)){a->job=NULL;bulb_http_finish(j,400,"{\"error\":\"invalid network record\"}");return;}
        if(a->runtime){Matter_Root_Fail_Safe_State_T state;if(matter_root_fail_safe_state(matter_runtime_root(a->runtime),&state)!=MATTER_OK||state!=MATTER_ROOT_FAIL_SAFE_IDLE){a->job=NULL;bulb_http_finish(j,409,"{\"error\":\"fail-safe active\"}");bulb_wipe(&network,sizeof(network));return;}}
        bulb_wipe(&network,sizeof(network));
    }
    a->draining=true; /* Stop/drain runs outside the HTTP task. */
}
static void complete_drain(App* a){
    if(!a->draining)return;
    Matter_Result_T r=a->runtime?matter_runtime_stop(a->runtime,now()):MATTER_OK;
    if(r!=MATTER_OK){if(r==MATTER_PENDING)(void)matter_runtime_process(a->runtime,now());return;}
    if(!advertisements(a,true))return;
    if(a->job){Bulb_Http_Job* j=a->job;a->job=NULL;
        if(j->kind==BULB_HTTP_RESET)a->erase=true;
        if(j->kind==BULB_HTTP_WIFI){Bulb_Network net;REQUIRE(bulb_network_decode(j->data,j->size,&net));
            if(a->runtime){Matter_Wifi_Settings_T* settings=calloc(1,sizeof(*settings));if(!settings)r=MATTER_LIMIT_REACHED;else {from_network(settings,&net);Matter_Wifi_Id_T connected=net.enabled?settings->networks[0].id:(Matter_Wifi_Id_T){0};r=matter_runtime_provision_wifi(a->runtime,settings,&connected);bulb_wipe(settings,sizeof(*settings));free(settings);}}
            else r=bulb_nvs_write("nvs","app","wifi",j->data,j->size)==ESP_OK?MATTER_OK:MATTER_PORT_ERROR;
            bulb_wipe(&net,sizeof(net));}
        bulb_http_finish(j,r==MATTER_OK?200:500,r==MATTER_OK?"{\"status\":\"stored; restarting\"}":"{\"error\":\"store failed; restarting previous state\"}");a->reboot_at=now()+1000;
    }
    if(!a->reboot){a->reboot=true;if(!a->reboot_at)a->reboot_at=now()+500;}
    if(now()<a->reboot_at)return;
    if(a->erase){
        /* Durable reset intent in factory partition makes interrupted two-
         * partition erasure resume on boot, without resurrecting fabrics. */
        uint8_t one=1;REQUIRE(bulb_nvs_write("factory_data","reset","pending",&one,1)==ESP_OK);
    }
#if CONFIG_BULB_DEVELOP
    heap_log(a,"before-reboot");
#endif
    esp_restart();
}
static bool boot_reset(void){
    uint8_t* pending=NULL;size_t size=0;esp_err_t e=bulb_nvs_read("factory_data","reset","pending",&pending,&size,1);bool reset=e==ESP_OK&&size==1&&pending[0]==1;free(pending);REQUIRE(e==ESP_OK||e==ESP_ERR_NVS_NOT_FOUND);
    nvs_handle_t h;REQUIRE(nvs_open("app",NVS_READWRITE,&h)==ESP_OK);uint8_t count=0;e=nvs_get_u8(h,"rapid_boots",&count);REQUIRE(e==ESP_OK||e==ESP_ERR_NVS_NOT_FOUND);
    if(esp_reset_reason()==ESP_RST_POWERON){if(count<5)++count;REQUIRE(nvs_set_u8(h,"rapid_boots",count)==ESP_OK);reset|=count>=5;}else {count=0;REQUIRE(nvs_set_u8(h,"rapid_boots",0)==ESP_OK);}
    REQUIRE(nvs_commit(h)==ESP_OK);
    nvs_close(h);
    if(reset){uint8_t one=1;REQUIRE(bulb_nvs_write("factory_data","reset","pending",&one,1)==ESP_OK);REQUIRE(nvs_flash_deinit_partition("matter")==ESP_OK&&nvs_flash_deinit_partition("nvs")==ESP_OK);REQUIRE(nvs_flash_erase_partition("matter")==ESP_OK&&nvs_flash_erase_partition("nvs")==ESP_OK);REQUIRE(nvs_flash_init()==ESP_OK&&nvs_flash_init_partition("matter")==ESP_OK);uint8_t zero=0;REQUIRE(bulb_nvs_write("factory_data","reset","pending",&zero,1)==ESP_OK);}
    return count!=0&&!reset;
}
void app_main(void){
    REQUIRE(psa_crypto_init()==PSA_SUCCESS);struct timeval clock={.tv_sec=BULB_BUILD_UTC};REQUIRE(settimeofday(&clock,NULL)==0);
    REQUIRE(nvs_flash_init()==ESP_OK&&nvs_flash_init_partition("factory_data")==ESP_OK&&nvs_flash_init_partition("matter")==ESP_OK);
    App* a=calloc(1,sizeof(*a));REQUIRE(a);a->boot=now();a->boot_counter=boot_reset();load_clock(a);a->led=bulb_led_new();REQUIRE(a->led);
    uint8_t* b=NULL;size_t size=0;REQUIRE(bulb_nvs_read("factory_data","bootstrap","record",&b,&size,BULB_BOOTSTRAP_SIZE)==ESP_OK&&bulb_bootstrap_decode(b,size,&a->bootstrap));bulb_wipe(b,size);free(b);
    Matter_Crypto_Entropy_Port_T ep={NULL,entropy};a->crypto=matter_crypto_mbedtls_new(&ep);REQUIRE(a->crypto);
    esp_err_t e=bulb_nvs_read("factory_data","identity","record",&a->identity,&a->identity_size,BULB_FACTORY_MAX);if(e==ESP_OK){REQUIRE(validate_identity(a,a->identity,a->identity_size,&a->factory));a->has_identity=true;}else REQUIRE(e==ESP_ERR_NVS_NOT_FOUND);
    /* A blank unit must accept ConnectNetwork during initial commissioning.
     * A saved application record (including an explicit disable) overrides it. */
    a->initial.enabled=true;
    e=bulb_nvs_read("nvs","app","wifi",&b,&size,BULB_NETWORK_MAX);if(e==ESP_OK){Bulb_Network n;REQUIRE(bulb_network_decode(b,size,&n));from_network(&a->initial,&n);bulb_wipe(&n,sizeof(n));bulb_wipe(b,size);free(b);}else REQUIRE(e==ESP_ERR_NVS_NOT_FOUND);
    boot_wifi(a);a->http=bulb_http_new(&a->bootstrap,a->sta);REQUIRE(a->http);if(a->has_identity)start_matter(a);
    else {esp_ota_img_states_t image;if(esp_ota_get_state_partition(esp_ota_get_running_partition(),&image)==ESP_OK&&image==ESP_OTA_IMG_PENDING_VERIFY)REQUIRE(esp_ota_mark_app_valid_cancel_rollback()==ESP_OK);}
    const Matter_Fabric_View_T* fabrics=matter_runtime_fabrics(a->runtime);a->window=!fabrics||!fabrics->count?now()+300000:0;
    gpio_config_t button={.pin_bit_mask=UINT64_C(1)<<9,.mode=GPIO_MODE_INPUT,.pull_up_en=GPIO_PULLUP_ENABLE};REQUIRE(gpio_config(&button)==ESP_OK);
    for(;;){uint64_t t=now();
        if(a->boot_counter&&t-a->boot>=5000){nvs_handle_t h;REQUIRE(nvs_open("app",NVS_READWRITE,&h)==ESP_OK&&nvs_set_u8(h,"rapid_boots",0)==ESP_OK&&nvs_commit(h)==ESP_OK);nvs_close(h);a->boot_counter=false;}
        if(!gpio_get_level(9)){if(!a->button)a->button=t;if(!a->button_used&&t-a->button>=3000){a->window=t+300000;a->button_used=true;}if(t-a->button>=10000){a->draining=true;a->erase=true;}}else {a->button=0;a->button_used=false;}
        wifi_ap_record_t ap;esp_ip6_addr_t local;
        bool connected=esp_netif_get_ip6_linklocal(a->sta,&local)==ESP_OK&&esp_wifi_sta_get_ap_info(&ap)==ESP_OK;
        bulb_http_process(a->http,connected,t<a->window&&!a->reboot&&!a->draining);jobs(a);
        if(a->runtime){Matter_Root_Fail_Safe_State_T fs;bool allow=!a->draining&&matter_runtime_recovered(a->runtime)&&matter_root_fail_safe_state(matter_runtime_root(a->runtime),&fs)==MATTER_OK&&fs==MATTER_ROOT_FAIL_SAFE_IDLE;matter_idf_wifi_allow_reconnect(a->wifi,allow);
            if(allow&&t>=a->clock_retry){uint64_t previous=a->clock.floor;
                if(!bulb_clock_update(&a->clock,matter_runtime_fabrics(a->runtime),true,store_clock,a))a->clock_retry=t+1000;
                if(a->clock.floor>previous){struct timeval clock={.tv_sec=(time_t)a->clock.floor};REQUIRE(settimeofday(&clock,NULL)==0);
#if CONFIG_BULB_DEVELOP
                    printf("BULB_CERT_CLOCK lkg=%llu committed=1\n",(unsigned long long)a->clock.floor);
#endif
                }}
            if(!a->draining){Matter_Udp_T udp=matter_idf_network_module(a->udp);if(!matter_udp_is_active(udp))(void)matter_udp_start(udp);esp_ip6_addr_t ip;
                if(esp_netif_get_ip6_linklocal(a->sta,&ip)==ESP_OK){Matter_Udp_Path_T path={.local.port=5540,.interface_id=(uint32_t)esp_netif_get_netif_impl_index(a->sta)};memcpy(path.local.address,ip.addr,16);(void)matter_runtime_set_outbound(a->runtime,&path);}
                Matter_Result_T r=matter_runtime_process(a->runtime,t);REQUIRE(r==MATTER_OK||r==MATTER_PENDING);country_sync(a);(void)advertisements(a,false);}
            (void)matter_idf_network_process(a->udp,t);uint32_t closed=matter_idf_network_closed_take(a->udp);if(closed)REQUIRE(matter_runtime_link_closed(a->runtime,closed)==MATTER_OK);
            (void)matter_udp_process(matter_idf_network_module(a->udp));(void)matter_idf_multicast_process(a->multicast);
            REQUIRE(matter_light_process(a->light,t)==MATTER_OK);uint8_t rgb[3];matter_light_pixels(a->light,rgb);REQUIRE(bulb_led_pixels(a->led,rgb));
            static const uint32_t ids[]={3,6,8,0x300};bool changed=false;
            for(unsigned i=0;i<4;++i){uint32_t version=0;REQUIRE(matter_cluster_version(matter_light_cluster(a->light,ids[i]),&version)==MATTER_OK);if(a->light_versions[i]!=version){a->light_versions[i]=version;changed=true;}}
            if(changed&&!a->draining)matter_runtime_changed(a->runtime);
            if(!a->draining&&!a->ota_ready&&t-a->boot>5000&&matter_runtime_recovered(a->runtime)){Matter_Result_T r=matter_runtime_ota_boot_ready(a->runtime);if(r==MATTER_OK)a->ota_ready=true;else REQUIRE(r==MATTER_LIMIT_REACHED);}
            if(matter_runtime_reset_requested(a->runtime)&&matter_runtime_reset_ready(a->runtime)){a->erase=true;a->draining=true;}
            if(matter_runtime_ota_reboot_requested(a->runtime)&&matter_runtime_ota_reboot_ready(a->runtime))a->draining=true;
        }else {matter_idf_wifi_allow_reconnect(a->wifi,!a->draining);(void)matter_idf_wifi_process(a->wifi,NULL,t);uint8_t rgb[]={10,5,0};REQUIRE(bulb_led_pixels(a->led,rgb));}
        complete_drain(a);
#if CONFIG_BULB_DEVELOP
        if(t>=a->log_at){esp_ip6_addr_t ip;esp_netif_ip_info_t v4;bool online=esp_netif_get_ip6_linklocal(a->sta,&ip)==ESP_OK;heap_log(a,"periodic");if(online)printf("BULB_IPV6 " IPV6STR "\n",IPV62STR(ip));if(esp_netif_get_ip_info(a->sta,&v4)==ESP_OK)printf("BULB_IPV4 " IPSTR "\n",IP2STR(&v4.ip));a->log_at=t+30000;}
#endif
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
