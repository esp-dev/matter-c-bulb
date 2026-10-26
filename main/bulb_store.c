#include "bulb_store.h"
#include "nvs.h"
#include <stdlib.h>
#include <string.h>
enum { PREFIX=31, OVERHEAD=35, MAXIMUM=OVERHEAD+MATTER_LIGHT_GROUP_IMAGE_MAX };
static void put(uint8_t* p,uint32_t v,unsigned n){for(unsigned i=0;i<n;++i)p[i]=(uint8_t)(v>>(8*i));}
static uint32_t get(const uint8_t* p,unsigned n){uint32_t v=0;for(unsigned i=0;i<n;++i)v|=(uint32_t)p[i]<<(8*i);return v;}
static void encode(uint8_t* p,const Matter_Light_State_T* s) {
    uint16_t words[]={s->x,s->y,s->temperature,s->start_temperature,s->on_level,s->start_level,s->start_on};
    for(unsigned i=0;i<7;++i)put(p+2*i,words[i],2);
    p[14]=s->level;p[15]=s->hue;p[16]=s->saturation;p[17]=s->mode;p[18]=s->options_level;p[19]=s->options_color;p[20]=s->on;
}
static bool decode(const uint8_t* p,Matter_Light_State_T* s) {
    *s=(Matter_Light_State_T){.x=get(p,2),.y=get(p+2,2),.temperature=get(p+4,2),.start_temperature=get(p+6,2),.on_level=get(p+8,2),.start_level=get(p+10,2),.start_on=get(p+12,2),
        .level=p[14],.hue=p[15],.saturation=p[16],.mode=p[17],.options_level=p[18],.options_color=p[19],.on=p[20]!=0};
    return p[20]<=1; /* Full operating-state constraints checked by light_new. */
}
esp_err_t bulb_nvs_read(const char* partition,const char* space,const char* key,uint8_t** out,size_t* size,size_t maximum) {
    nvs_handle_t h;*out=NULL;*size=0;esp_err_t e=nvs_open_from_partition(partition,space,NVS_READONLY,&h);if(e!=ESP_OK)return e;
    size_t n=0;e=nvs_get_blob(h,key,NULL,&n);
    if(e==ESP_OK&&(!n||n>maximum))e=ESP_ERR_INVALID_SIZE;
    uint8_t* p=e==ESP_OK?calloc(1,n):NULL;if(e==ESP_OK&&!p)e=ESP_ERR_NO_MEM;
    if(e==ESP_OK)e=nvs_get_blob(h,key,p,&n);
    nvs_close(h);if(e!=ESP_OK){if(p)bulb_wipe(p,n);free(p);return e;}*out=p;*size=n;return ESP_OK;
}
esp_err_t bulb_nvs_write(const char* partition,const char* space,const char* key,const void* p,size_t n) {
    nvs_handle_t h;esp_err_t e=nvs_open_from_partition(partition,space,NVS_READWRITE,&h);if(e!=ESP_OK)return e;
    e=nvs_set_blob(h,key,p,n);
    /* IDF6.1 set_blob publishes synchronously. REMOVE_FAILED means published;
     * never report a rejection after a successful publication. */
    bool published=e==ESP_OK||e==ESP_ERR_NVS_REMOVE_FAILED;
    if(published)(void)nvs_commit(h);
    nvs_close(h);return published?ESP_OK:e;
}
bool bulb_store_init(Bulb_Store* b) {
    b->disk=matter_idf_store_new("nvs","light");if(!b->disk)return false;
    Matter_Store_Blob_Port_T port=matter_idf_store_port(b->disk);size_t n=0;
    Matter_Result_T r=port.read(port.user,NULL,MAXIMUM,&n);
    if(r==MATTER_OFFLINE){matter_light_defaults(&b->state);return true;}
    if(r!=MATTER_OK||n<OVERHEAD||n>MAXIMUM)return false;
    uint8_t* p=calloc(1,n);if(!p)return false;
    r=port.read(port.user,p,n,&n);
    bool ok=r==MATTER_OK&&!memcmp(p,"BLIGHT01",8)&&get(p+29,2)==n-OVERHEAD&&get(p+n-4,4)==bulb_crc32(p,n-4)&&decode(p+8,&b->state);
    if(ok&&n>OVERHEAD){b->groups=calloc(1,n-OVERHEAD);if(!b->groups)ok=false;else {b->group_size=n-OVERHEAD;memcpy(b->groups,p+PREFIX,b->group_size);}}
    free(p);return ok;
}
Matter_Result_T bulb_store_joint(void* u,const uint8_t* groups,size_t size,const Matter_Light_State_T* light) {
    Bulb_Store* b=u;if(!light||size>MATTER_LIGHT_GROUP_IMAGE_MAX||(!groups&&size))return MATTER_INVALID_ARGUMENT;
    uint8_t* next=size?calloc(1,size):NULL;if(size&&!next)return MATTER_LIMIT_REACHED;
    if(size)memcpy(next,groups,size);
    size_t n=size+OVERHEAD;uint8_t* p=calloc(1,n);if(!p){free(next);return MATTER_LIMIT_REACHED;}
    memcpy(p,"BLIGHT01",8);encode(p+8,light);put(p+29,size,2);if(size)memcpy(p+PREFIX,groups,size);put(p+n-4,bulb_crc32(p,n-4),4);
    Matter_Store_Blob_Port_T port=matter_idf_store_port(b->disk);Matter_Result_T r=port.replace(port.user,p,n);free(p);
    if(r==MATTER_OK){free(b->groups);b->groups=next;b->group_size=size;b->state=*light;}else free(next);
    return r;
}
Matter_Result_T bulb_store_light_load(void* u,Matter_Light_State_T* out){*out=((Bulb_Store*)u)->state;return MATTER_OK;}
Matter_Result_T bulb_store_light(void* u,const Matter_Light_State_T* state){Bulb_Store* b=u;return bulb_store_joint(b,b->groups,b->group_size,state);}
Matter_Result_T bulb_store_groups(void* u,const uint8_t* bytes,size_t size){Bulb_Store* b=u;return bulb_store_joint(b,bytes,size,&b->state);}
Matter_Result_T bulb_store_groups_load(void* u,uint8_t* out,size_t capacity,size_t* size){Bulb_Store* b=u;*size=b->group_size;if(out){if(capacity<b->group_size)return MATTER_LIMIT_REACHED;if(b->group_size)memcpy(out,b->groups,b->group_size);}return MATTER_OK;}
