#include "bulb_store.h"
#include "nvs.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#undef calloc
#undef free
static unsigned calls,fail_at,live;
void* test_store_calloc(size_t n,size_t size){if(++calls==fail_at)return NULL;void* p=calloc(n,size);if(p)++live;return p;}
void test_store_free(void* p){if(p){assert(live);--live;free(p);}}
static uint8_t disk[4200];static size_t used;static bool store_fail;
static Matter_Result_T read_record(void* u,uint8_t* p,size_t capacity,size_t* size){(void)u;if(!used)return MATTER_OFFLINE;if(capacity<used)return MATTER_LIMIT_REACHED;*size=used;if(p)memcpy(p,disk,used);return MATTER_OK;}
static Matter_Result_T write_record(void* u,const uint8_t* p,size_t size){(void)u;if(store_fail)return MATTER_PORT_ERROR;assert(size<=sizeof(disk));memcpy(disk,p,size);used=size;return MATTER_OK;}
Matter_Idf_Store_T matter_idf_store_new(const char* partition,const char* space){assert(!strcmp(partition,"nvs")&&!strcmp(space,"light"));return (Matter_Idf_Store_T)disk;}
Matter_Store_Blob_Port_T matter_idf_store_port(Matter_Idf_Store_T disk_handle){assert(disk_handle==(Matter_Idf_Store_T)disk);return (Matter_Store_Blob_Port_T){NULL,read_record,write_record};}
void matter_light_defaults(Matter_Light_State_T* state){*state=(Matter_Light_State_T){.on=true,.level=127,.hue=13,.saturation=254,.x=2345,.y=3456,.temperature=250,.start_temperature=65535,.start_level=65535,.start_on=65535,.on_level=65535};}
esp_err_t nvs_open_from_partition(const char* a,const char* b,int c,nvs_handle_t* d){(void)a;(void)b;(void)c;(void)d;return ESP_ERR_NVS_NOT_FOUND;}
esp_err_t nvs_get_blob(nvs_handle_t h,const char* k,void* p,size_t* n){(void)h;(void)k;(void)p;(void)n;return ESP_ERR_NVS_NOT_FOUND;}
esp_err_t nvs_set_blob(nvs_handle_t h,const char* k,const void* p,size_t n){(void)h;(void)k;(void)p;(void)n;return ESP_ERR_NVS_NOT_FOUND;}
esp_err_t nvs_commit(nvs_handle_t h){(void)h;return ESP_OK;}
void nvs_close(nvs_handle_t h){(void)h;}
int main(void){
    Bulb_Store b={0};assert(bulb_store_init(&b)&&b.state.on&&b.state.level==127&&!b.groups);
    uint8_t group[]={1,2,3,4,5};Matter_Light_State_T state=b.state;state.on=false;state.level=234;state.hue=200;state.saturation=201;state.mode=2;state.options_level=3;state.options_color=1;state.x=45000;state.y=12345;state.start_temperature=300;state.start_on=2;state.start_level=254;state.on_level=200;
    for(unsigned i=1;i<=2;++i){unsigned before=live;fail_at=calls+i;assert(bulb_store_joint(&b,group,5,&state)==MATTER_LIMIT_REACHED);fail_at=0;assert(!used&&b.state.on&&live==before);}
    assert(bulb_store_joint(&b,group,5,&state)==MATTER_OK&&used==40&&!b.state.on&&b.group_size==5);
    uint8_t previous[40];memcpy(previous,disk,40);store_fail=true;state.level=1;assert(bulb_store_joint(&b,(uint8_t[]){9},1,&state)==MATTER_PORT_ERROR&&b.state.level==234&&b.group_size==5&&!memcmp(disk,previous,40));store_fail=false;
    Bulb_Store reboot={0};assert(bulb_store_init(&reboot)&&reboot.state.level==234&&reboot.state.hue==200&&reboot.state.x==45000&&reboot.state.start_on==2&&reboot.state.options_color==1&&reboot.group_size==5&&!memcmp(reboot.groups,group,5));
    size_t size=0;assert(bulb_store_groups_load(&reboot,NULL,0,&size)==MATTER_OK&&size==5);uint8_t out[5];assert(bulb_store_groups_load(&reboot,out,4,&size)==MATTER_LIMIT_REACHED&&bulb_store_groups_load(&reboot,out,5,&size)==MATTER_OK&&!memcmp(out,group,5));
    disk[8]^=1;Bulb_Store corrupt={0};assert(!bulb_store_init(&corrupt));test_store_free(b.groups);test_store_free(reboot.groups);assert(!live);
    puts("PASS atomic light+scene storage: roundtrip, store rejection, both publication OOM sites, bounds, reboot and corruption");return 0;
}
