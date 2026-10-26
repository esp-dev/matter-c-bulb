#include "bulb_clock.h"
#include "codec/matter_tlv.h"

static bool not_before(const uint8_t* data,size_t size,uint64_t* floor){
    if(!data||!size||size>MATTER_FABRIC_CERT_MAX)return false;
    Matter_Tlv_Reader_T reader;Matter_Tlv_Element_T e;bool found=false;
    if(matter_tlv_init(&reader,data,size,MATTER_TLV_MAX_DEPTH)!=MATTER_TLV_OK||
       matter_tlv_next(&reader,&e)!=MATTER_TLV_OK||e.type!=MATTER_TLV_STRUCTURE||e.tag.control)return false;
    Matter_Tlv_Result_T r;
    while((r=matter_tlv_next(&reader,&e))==MATTER_TLV_OK){
        if(e.tag.control==1&&e.tag.number==4){
            if(found||e.type!=MATTER_TLV_UNSIGNED||e.unsigned_value>UINT32_MAX)return false;
            uint64_t candidate=UINT64_C(946684800)+e.unsigned_value;
            if(candidate>*floor)*floor=candidate;
            found=true;
        }
        if(e.type==MATTER_TLV_STRUCTURE||e.type==MATTER_TLV_ARRAY||e.type==MATTER_TLV_LIST)
            if(matter_tlv_skip_container(&reader)!=MATTER_TLV_OK)return false;
    }
    return r==MATTER_TLV_DONE&&found;
}
bool bulb_clock_update(Bulb_Clock* c,const Matter_Fabric_View_T* v,bool stable,
    bool (*store)(void*,uint64_t),void* user){
    if(!c||!v||!store)return false;
    if(!stable||v->pending_index||v->root_ready||v->added)return true;
    if(c->seen&&c->revision==v->revision)return true;
    uint64_t candidate=c->floor;
    for(unsigned i=0;i<MATTER_FABRIC_CAPACITY;++i){
        const Matter_Fabric_Record_T* f=&v->records[i];if(!f->index)continue;
        if(!not_before(f->noc,f->noc_size,&candidate)||!not_before(f->root,f->root_size,&candidate)||
           (f->icac_size&&!not_before(f->icac,f->icac_size,&candidate)))return false;
    }
    if(candidate>c->floor&&!store(user,candidate))return false;
    c->floor=candidate;c->revision=v->revision;c->seen=true;return true;
}
