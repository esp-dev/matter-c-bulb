#ifndef BULB_LED_H_INCLUDED
#define BULB_LED_H_INCLUDED 1
#include <stdbool.h>
#include <stdint.h>
typedef struct Bulb_Led* Bulb_Led;
Bulb_Led bulb_led_new(void);
/* Nonblocking. Copies GRB before starting DMA/ISR; keeps storage until done. */
bool bulb_led_pixels(Bulb_Led,const uint8_t rgb[3]);
#endif
