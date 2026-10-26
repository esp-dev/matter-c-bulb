#ifndef BULB_FACTORY_CODEC_H
#define BULB_FACTORY_CODEC_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
enum { BULB_FACTORY_MAX=4096,BULB_BOOTSTRAP_SIZE=66,BULB_NETWORK_MAX=111 };
typedef struct { const uint8_t* data; size_t size; } Bulb_Bytes;
/* All spans borrow the caller's complete authenticated record. Keep its heap
 * storage alive through Matter runtime deletion (DER providers borrow it). */
typedef struct {
    uint16_t vendor,product,discriminator,hardware;
    uint32_t passcode;
    char serial[33],vendor_name[33],product_name[33];
    Bulb_Bytes dac,pai,paa,cd,private_key;
} Bulb_Factory;
typedef struct { uint8_t mac[6],token[32]; char ap_password[17]; } Bulb_Bootstrap;
typedef struct { uint8_t ssid[32],password[64],ssid_size,password_size; bool enabled; } Bulb_Network;
uint32_t bulb_crc32(const uint8_t*,size_t);
bool bulb_factory_decode(const uint8_t*,size_t,Bulb_Factory*);
bool bulb_bootstrap_decode(const uint8_t*,size_t,Bulb_Bootstrap*);
bool bulb_network_decode(const uint8_t*,size_t,Bulb_Network*);
void bulb_wipe(void*,size_t);
bool bulb_secret_equal(const uint8_t*,const uint8_t*,size_t);
#endif
