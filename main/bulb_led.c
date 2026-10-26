#include "bulb_led.h"
#include "driver/rmt_tx.h"
#include <stdlib.h>
#include <string.h>
struct Bulb_Led {rmt_channel_handle_t channel;rmt_encoder_handle_t encoder;uint8_t grb[3];bool pending,valid;};
static size_t encode(const void* data,size_t size,size_t written,size_t available,rmt_symbol_word_t* symbols,bool* done,void* user) {
    (void)user;if(available<8)return 0;
    size_t pos=written/8;
    if(pos<size){uint8_t b=((const uint8_t*)data)[pos];for(unsigned i=0;i<8;++i){bool one=(b&(0x80u>>i))!=0;symbols[i]=(rmt_symbol_word_t){.level0=1,.duration0=one?9:3,.level1=0,.duration1=one?3:9};}return 8;}
    symbols[0]=(rmt_symbol_word_t){.level0=0,.duration0=400,.level1=0,.duration1=400};*done=true;return 1;
}
Bulb_Led bulb_led_new(void) {
    Bulb_Led l=calloc(1,sizeof(*l));if(!l)return NULL;
    rmt_tx_channel_config_t config={.gpio_num=8,.clk_src=RMT_CLK_SRC_DEFAULT,.resolution_hz=10000000,.mem_block_symbols=48,.trans_queue_depth=1};
    rmt_simple_encoder_config_t enc={.callback=encode,.min_chunk_size=8};
    bool enabled=false;
    if(rmt_new_tx_channel(&config,&l->channel)==ESP_OK&&rmt_new_simple_encoder(&enc,&l->encoder)==ESP_OK&&rmt_enable(l->channel)==ESP_OK)enabled=true;
    if(enabled)return l;
    if(l->encoder)(void)rmt_del_encoder(l->encoder);
    if(l->channel)(void)rmt_del_channel(l->channel);
    free(l);return NULL;
}
bool bulb_led_pixels(Bulb_Led l,const uint8_t rgb[3]) {
    if(l->pending){esp_err_t e=rmt_tx_wait_all_done(l->channel,0);if(e==ESP_ERR_TIMEOUT)return true;if(e!=ESP_OK)return false;l->pending=false;}
    uint8_t grb[]={rgb[1],rgb[0],rgb[2]};if(l->valid&&!memcmp(grb,l->grb,3))return true;
    memcpy(l->grb,grb,3);rmt_transmit_config_t config={0};if(rmt_transmit(l->channel,l->encoder,l->grb,3,&config)!=ESP_OK)return false;l->valid=true;l->pending=true;return true;
}
