#include "bulb_clock.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned writes;static uint64_t disk;static bool fail;
static bool store(void* u,uint64_t candidate){(void)u;++writes;if(fail)return false;disk=candidate;return true;}
/* Only the already verified OWN view is consumed. Include a nested tag4:
 * it must never be mistaken for the certificate's top-level NotBefore. */
static uint16_t certificate(uint8_t* out,uint32_t before){
    const uint8_t wire[]={0x15,0x26,4,0,0,0,0,0x35,6,0x26,4,255,255,255,255,0x18,0x18};
    memcpy(out,wire,sizeof(wire));for(unsigned i=0;i<4;++i)out[3+i]=(uint8_t)(before>>(8*i));return sizeof(wire);
}
int main(void){
    Matter_Fabric_View_T* v=calloc(1,sizeof(*v));assert(v);
    Bulb_Clock c={.floor=UINT64_C(1700000000)};
    assert(bulb_clock_update(&c,v,false,store,NULL)&&!c.seen&&!writes);
    v->records[0].index=1;v->count=1;v->revision=7;
    Matter_Fabric_Record_T* f=&v->records[0];
    f->noc_size=certificate(f->noc,800000000);f->root_size=certificate(f->root,900000000);
    f->icac_size=certificate(f->icac,850000000);
    v->added=true;assert(bulb_clock_update(&c,v,true,store,NULL)&&!writes&&!c.seen);v->added=false;
    v->pending_index=1;assert(bulb_clock_update(&c,v,true,store,NULL)&&!writes);v->pending_index=0;
    v->root_ready=true;assert(bulb_clock_update(&c,v,true,store,NULL)&&!writes);v->root_ready=false;
    fail=true;assert(!bulb_clock_update(&c,v,true,store,NULL)&&writes==1&&!c.seen&&c.floor==1700000000&&!disk);
    fail=false;assert(bulb_clock_update(&c,v,true,store,NULL)&&writes==2&&c.floor==1846684800&&disk==c.floor&&c.seen);
    assert(bulb_clock_update(&c,v,true,store,NULL)&&writes==2); /* No repeated NVS writes. */
    Bulb_Clock reboot={.floor=1700000000};assert(bulb_clock_update(&reboot,v,true,store,NULL)&&reboot.floor==c.floor);
    /* Malformed new committed state never partially publishes a newer floor. */
    v->revision++;f->noc_size=certificate(f->noc,1000000000);f->root_size--;
    unsigned before=writes;assert(!bulb_clock_update(&c,v,true,store,NULL)&&c.floor==disk&&writes==before&&c.revision==7);
    f->root_size++;assert(bulb_clock_update(&c,v,true,store,NULL)&&c.floor==1946684800&&writes==before+1);
    /* A rolled-back pending certificate and later fabric removal cannot lower LKG. */
    v->revision++;v->pending_index=1;f->noc_size=certificate(f->noc,UINT32_MAX);
    assert(bulb_clock_update(&c,v,true,store,NULL)&&c.floor==1946684800);
    memset(v->records,0,sizeof(v->records));v->pending_index=0;v->count=0;v->revision++;
    assert(bulb_clock_update(&c,v,true,store,NULL)&&c.floor==1946684800&&writes==before+1);
    assert(!bulb_clock_update(NULL,v,true,store,NULL)&&!bulb_clock_update(&c,NULL,true,store,NULL));
    free(v);puts("PASS LKG: committed own certs, nested tags, fail-safe/recovery gate, atomic store failure/retry, reboot, malformed input, rollback/removal and write cache");return 0;
}
