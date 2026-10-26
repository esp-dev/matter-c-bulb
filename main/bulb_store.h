#ifndef BULB_STORE_H_INCLUDED
#define BULB_STORE_H_INCLUDED 1
#include "esp_err.h"
#include "factory_codec.h"
#include "clusters/matter_light_groups.h"
#include "matter_idf_store.h"
typedef struct {
    Matter_Idf_Store_T disk;Matter_Light_State_T state;
    uint8_t* groups;size_t group_size;
} Bulb_Store;
bool bulb_store_init(Bulb_Store*);
Matter_Result_T bulb_store_light_load(void*,Matter_Light_State_T*);
Matter_Result_T bulb_store_light(void*,const Matter_Light_State_T*);
Matter_Result_T bulb_store_groups_load(void*,uint8_t*,size_t,size_t*);
Matter_Result_T bulb_store_groups(void*,const uint8_t*,size_t);
Matter_Result_T bulb_store_joint(void*,const uint8_t*,size_t,const Matter_Light_State_T*);
/* No automatic erase or fallback on corruption. Caller owns returned heap. */
esp_err_t bulb_nvs_read(const char* partition,const char* space,const char* key,uint8_t**,size_t* size,size_t maximum);
esp_err_t bulb_nvs_write(const char* partition,const char* space,const char* key,const void*,size_t);
#endif
