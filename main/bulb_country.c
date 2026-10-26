#include "bulb_country.h"
#include <stddef.h>
#include <string.h>
bool bulb_country_supported(const uint8_t code[2]){
    if(!code)return false;
    /* ESP-IDF6.1 esp_wifi_set_country_code set, plus Matter unspecified XX. */
    static const char countries[][3]={"XX","AT","AU","BE","BG","BR","CA","CH","CN","CY","CZ","DE","DK","EE","ES","FI","FR","GB","GR","HK","HR","HU","IE","IN","IS","IT","JP","KR","LI","LT","LU","LV","MT","MX","NL","NO","NZ","PL","PT","RO","SE","SI","SK","TW","US"};
    for(size_t i=0;i<sizeof(countries)/sizeof(*countries);++i)if(!memcmp(countries[i],code,2))return true;
    return false;
}
bool bulb_country_apply(Bulb_Country* state,const uint8_t code[2],bool (*apply)(void*,const char[3]),void* user){
    if(!state||!code||!apply)return false;
    char radio[3]={'0','1',0};
    if(bulb_country_supported(code)&&memcmp(code,"XX",2))memcpy(radio,code,2);
    if(state->valid&&!memcmp(state->applied,radio,3))return true;
    if(!apply(user,radio))return false;
    memcpy(state->applied,radio,3);state->valid=true;return true;
}
