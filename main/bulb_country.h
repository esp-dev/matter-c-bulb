#ifndef BULB_COUNTRY_H_INCLUDED
#define BULB_COUNTRY_H_INCLUDED 1
#include <stdbool.h>
#include <stdint.h>
typedef struct {char applied[3];bool valid;} Bulb_Country;
bool bulb_country_supported(const uint8_t code[2]);
/* Owner only, after Location is published by Matter, including restored state.
 * Unknown/XX selects region-agnostic world policy. Cache only SDK success;
 * reapply a changed/restored Location, without a separate persistence journal. */
bool bulb_country_apply(Bulb_Country*,const uint8_t code[2],bool (*apply)(void*,const char[3]),void*);
#endif
