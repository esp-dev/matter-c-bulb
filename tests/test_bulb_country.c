#include "bulb_country.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static unsigned calls;static bool fail;static char driver[3];
static bool apply(void* user,const char code[3]){(void)user;++calls;if(fail)return false;memcpy(driver,code,3);return true;}
int main(void){
    Bulb_Country state={0};
    assert(bulb_country_supported((const uint8_t*)"PL")&&bulb_country_supported((const uint8_t*)"US")&&bulb_country_supported((const uint8_t*)"XX"));
    assert(!bulb_country_supported((const uint8_t*)"ZZ")&&!bulb_country_supported((const uint8_t*)"pl")&&!bulb_country_supported(NULL));
    assert(bulb_country_apply(&state,(const uint8_t*)"XX",apply,NULL)&&calls==1&&!strcmp(driver,"01"));
    assert(bulb_country_apply(&state,(const uint8_t*)"PL",apply,NULL)&&calls==2&&!strcmp(driver,"PL"));
    assert(bulb_country_apply(&state,(const uint8_t*)"PL",apply,NULL)&&calls==2);
    fail=true;assert(!bulb_country_apply(&state,(const uint8_t*)"US",apply,NULL)&&calls==3&&!strcmp(state.applied,"PL")&&!strcmp(driver,"PL"));
    fail=false;assert(bulb_country_apply(&state,(const uint8_t*)"US",apply,NULL)&&calls==4&&!strcmp(driver,"US"));
    assert(bulb_country_apply(&state,(const uint8_t*)"PL",apply,NULL)&&calls==5&&!strcmp(driver,"PL")); /* Restored publication. */
    assert(bulb_country_apply(&state,(const uint8_t*)"ZZ",apply,NULL)&&calls==6&&!strcmp(driver,"01"));
    assert(bulb_country_apply(&state,(const uint8_t*)"XX",apply,NULL)&&calls==6);
    Bulb_Country reboot={0};assert(bulb_country_apply(&reboot,(const uint8_t*)"PL",apply,NULL)&&calls==7&&!strcmp(driver,"PL"));
    assert(!bulb_country_apply(NULL,(const uint8_t*)"PL",apply,NULL)&&!bulb_country_apply(&state,NULL,apply,NULL)&&!bulb_country_apply(&state,(const uint8_t*)"PL",NULL,NULL));
    puts("PASS country: PL/US/unknown/XX, unchanged cache, SDK failure/retry, restored Location and reboot");return 0;
}
