#ifndef BULB_CLOCK_H_INCLUDED
#define BULB_CLOCK_H_INCLUDED 1
#include "clusters/matter_fabric.h"
typedef struct { uint64_t floor; uint32_t revision; bool seen; } Bulb_Clock;
/* Owner thread only. Input is the verified OWN fabric view, never a peer's
 * certificate. stable means runtime recovery finished and fail-safe idle.
 * Store is atomic; publish/cache only after success. Retry after failure. */
bool bulb_clock_update(Bulb_Clock*,const Matter_Fabric_View_T*,bool stable,
    bool (*store)(void*,uint64_t),void*);
#endif
