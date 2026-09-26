/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 BluMach contributors
 * Author-written 286 task mechanism from Intel PRM-1987, not later-x86 code.
 */
#include "task_286.h"
#include <string.h>

static bm_status_t fault(bm_286_task_result_t *r,unsigned vector,unsigned selector,bool ext)
{
    r->fault_vector=(uint8_t)vector;
    r->fault_error=(uint16_t)((selector&0xfffcu)|(ext?1u:0u));
    return BM_STATUS_OK;
}
static bm_status_t word(const bm_286_config_t *c,uint32_t address,bool write,
    uint16_t *value,bm_286_task_result_t *r)
{
    unsigned size=(address&1u)?1u:2u; uint16_t read=0;
    for(unsigned i=0;i<2;i+=size) {
        bm_bus_transaction_t t={0}; bm_status_t status;
        t.address=(address+i)&0xffffffu; t.space=BM_ADDRESS_DATA;
        t.operation=write?BM_BUS_WRITE:BM_BUS_READ; t.size=t.alignment=size;
        t.endianness=BM_ENDIAN_LITTLE;
        t.value=write?(size==2?*value:(uint8_t)(*value>>(8*i))):0;
        status=c->access(c->access_context,&t); if(status!=BM_STATUS_OK) return status;
        r->waits+=t.wait_states;
        if(!write) read|=(uint16_t)((size==2?(uint16_t)t.value:(uint8_t)t.value)<<(8*i));
    }
    if(!write) *value=read;
    return BM_STATUS_OK;
}
static bm_status_t lookup(const bm_286_arch_state_t *a,const bm_286_config_t *c,
    uint16_t selector,bm_286_pm_lookup_t *d,bm_286_task_result_t *r)
{
    bm_status_t status=bm_286_pm_lookup_descriptor(&a->gdtr,&a->ldtr,selector,c->access,c->access_context,d);
    r->waits+=d->waits; return status;
}
/* new=true tests fresh incoming availability. RETURN checks busy without
 * rewriting it (table8-2 overrides generic B12's unconditional set). */
static bm_status_t busy(const bm_286_config_t *c,uint32_t address,bool incoming,
    bool returning,uint16_t selector,bool ext,bm_286_task_result_t *r,uint8_t *access)
{
    bm_bus_transaction_t t={0}; bm_status_t status;
    t.address=address&0xffffffu; t.space=BM_ADDRESS_DATA; t.operation=BM_BUS_READ;
    t.size=t.alignment=1; t.endianness=BM_ENDIAN_LITTLE; t.attributes=BM_BUS_TRANSACTION_LOCKED;
    c->bus_lock(c->pin_context,1); status=c->access(c->access_context,&t);
    if(status==BM_STATUS_OK) {
        r->waits+=t.wait_states; *access=(uint8_t)t.value;
        if(incoming && (*access&0x1fu)!=(returning?3u:1u))
            fault(r,returning?10:13,selector,ext);
        else if(incoming && !(*access&0x80u)) fault(r,11,selector,ext);
        else if(!(incoming && returning)) {
            t.operation=BM_BUS_WRITE; t.value=incoming?(*access|2u):(*access&~2u); t.wait_states=0;
            status=c->access(c->access_context,&t);
            if(status==BM_STATUS_OK) {r->waits+=t.wait_states; *access=(uint8_t)t.value;}
        }
    }
    c->bus_lock(c->pin_context,0); return status;
}
static bm_286_segment_state_t cache(uint16_t selector,const bm_286_pm_descriptor_t *d)
{
    bm_286_segment_state_t s={0}; s.selector=selector; s.base=d->base;
    s.limit=d->limit; s.access=d->access; s.valid=1; return s;
}
static void unloaded(bm_286_segment_state_t *s,uint16_t selector)
{
    memset(s,0,sizeof(*s)); s->selector=selector;
}
static bm_status_t segment(const bm_286_config_t *c,bm_286_task_result_t *r,
    bm_286_segment_state_t *s,unsigned role,bool ext)
{
    bm_286_arch_state_t *a=&r->candidate; bm_286_pm_lookup_t d;
    bm_286_pm_load_plan_t plan={0}; bm_status_t status;
    uint16_t selector=s->selector; bool valid;
    /* OS Writer's Guide 121960-001, 7-5 explicitly permits a null LDTR
     * during a 286 task switch, refining the abbreviated PRM B12 check. */
    if((role==0 || role==3) && !(selector&0xfffcu)) return BM_STATUS_OK;
    if(role==0 && (selector&4u)) return fault(r,10,selector,ext);
    status=lookup(a,c,selector,&d,r); if(status!=BM_STATUS_OK) return status;
    if(d.reason!=BM_286_PM_FOUND) return fault(r,10,selector,ext);
    if(role==0) valid=d.descriptor.kind==BM_286_PM_LDT;
    else if(role==1) valid=d.descriptor.kind==BM_286_PM_DATA && d.descriptor.writable &&
        (selector&3u)==a->cpl && d.descriptor.dpl==a->cpl;
    else if(role==2) valid=d.descriptor.kind==BM_286_PM_CODE &&
        (d.descriptor.conforming?d.descriptor.dpl<=a->cpl:d.descriptor.dpl==a->cpl);
    else valid=(d.descriptor.kind==BM_286_PM_DATA ||
        (d.descriptor.kind==BM_286_PM_CODE && d.descriptor.readable)) &&
        (d.descriptor.conforming || (d.descriptor.dpl>=a->cpl && d.descriptor.dpl>=(selector&3u)));
    if(!valid) return fault(r,10,selector,ext);
    if(!d.descriptor.present) return fault(r,role==0?10:role==1?12:11,selector,ext);
    if(role==0) {*s=cache(selector,&d.descriptor); return BM_STATUS_OK;}
    plan.prepared=true; plan.needs_accessed_write=true; plan.segment=cache(selector,&d.descriptor);
    plan.access_address=(((selector&4u)?a->ldtr.base:a->gdtr.base)+(selector&0xfff8u)+5u)&0xffffffu;
    status=bm_286_pm_commit_load(&plan,c->access,c->access_context,c->bus_lock,c->pin_context,s);
    r->waits+=plan.waits; return status;
}

bm_status_t bm_286_pm_switch_task(const bm_286_arch_state_t *arch,
    const bm_286_config_t *c,const bm_286_task_request_t *q,bm_286_task_result_t *r)
{
    bm_286_pm_lookup_t target; bm_status_t status; uint8_t ac;
    uint16_t selector,values[15]; bool returning,nest;
    if(!r) return BM_STATUS_INVALID_ARGUMENT;
    memset(r,0,sizeof(*r));
    if(!arch || !c || !q || !c->access || q->kind<BM_286_TASK_CALL || q->kind>BM_286_TASK_RETURN)
        return BM_STATUS_INVALID_ARGUMENT;
    if(!(arch->msw&1u) || arch->cpl>3 || (arch->halted && !q->event) || arch->shutdown ||
        arch->tr.valid>1 || arch->tr.base>0xffffffu) return BM_STATUS_INVALID_STATE;
    if(arch->tr.valid && (!(arch->tr.selector&0xfffcu) || (arch->tr.selector&4u) ||
        (arch->tr.access&0x9fu)!=0x83u)) return BM_STATUS_INVALID_STATE;
    r->candidate=*arch; returning=q->kind==BM_286_TASK_RETURN; nest=q->kind==BM_286_TASK_CALL;
    selector=q->selector;
    if(returning) {
        if(!arch->tr.valid || arch->tr.limit<1) return fault(r,10,arch->tr.selector,q->external);
        status=word(c,arch->tr.base,false,&selector,r); if(status!=BM_STATUS_OK) return status;
    }
    if(!(selector&0xfffcu) || (selector&4u)) return fault(r,returning?10:13,selector,q->external);
    status=lookup(arch,c,selector,&target,r); if(status!=BM_STATUS_OK) return status;
    if(target.reason!=BM_286_PM_FOUND) return fault(r,returning?10:13,selector,q->external);
    if(q->direct && !returning && (target.descriptor.dpl<arch->cpl || target.descriptor.dpl<(selector&3u)))
        return fault(r,13,selector,q->external);
    if(target.descriptor.kind!=(returning?BM_286_PM_TSS_BUSY:BM_286_PM_TSS_AVAILABLE))
        return fault(r,returning?10:13,selector,q->external);
    if(!target.descriptor.present) return fault(r,11,selector,q->external);
    if(!c->bus_lock) return BM_STATUS_UNSUPPORTED;
    status=busy(c,arch->gdtr.base+(selector&0xfff8u)+5u,true,returning,selector,q->external,r,&ac);
    if(status!=BM_STATUS_OK || r->fault_vector) return status;
    r->candidate.tr=cache(selector,&target.descriptor); r->candidate.tr.access=ac;
    r->phase=BM_286_TASK_SELECTED;
    /* B12 identifies the new task for an invalid outgoing save area. Preserve
     * unsaved registers until incoming state has actually been read. */
    if(!arch->tr.valid || arch->tr.limit<41) {
        if(nest) {
            uint16_t link=arch->tr.selector;
            status=word(c,target.descriptor.base,true,&link,r); if(status!=BM_STATUS_OK) return status;
        }
        return fault(r,10,selector,q->external);
    }
    values[0]=q->return_ip; values[1]=returning?(uint16_t)(arch->flags&~0x4000u):arch->flags;
    values[2]=arch->ax; values[3]=arch->cx; values[4]=arch->dx; values[5]=arch->bx;
    values[6]=arch->sp; values[7]=arch->bp; values[8]=arch->si; values[9]=arch->di;
    values[10]=arch->es.selector; values[11]=arch->cs.selector;
    values[12]=arch->ss.selector; values[13]=arch->ds.selector;
    for(unsigned i=0;i<14;++i) {
        status=word(c,arch->tr.base+14+2*i,true,&values[i],r); if(status!=BM_STATUS_OK) return status;
    }
    if(nest) {
        uint16_t link=arch->tr.selector;
        status=word(c,target.descriptor.base,true,&link,r);
    } else status=busy(c,arch->gdtr.base+(arch->tr.selector&0xfff8u)+5u,false,false,0,false,r,&ac);
    if(status!=BM_STATUS_OK) return status;
    if(target.descriptor.limit<43) return fault(r,10,selector,q->external);
    for(unsigned i=0;i<15;++i) {
        status=word(c,target.descriptor.base+14+2*i,false,&values[i],r); if(status!=BM_STATUS_OK) return status;
    }
    bm_286_arch_state_t *a=&r->candidate;
    a->ip=values[0]; a->flags=(uint16_t)((values[1]&0x7fd5u)|2u);
    if(nest) a->flags|=0x4000u; else if(!returning) a->flags&=(uint16_t)~0x4000u;
    a->ax=values[2]; a->cx=values[3]; a->dx=values[4]; a->bx=values[5];
    a->sp=values[6]; a->bp=values[7]; a->si=values[8]; a->di=values[9];
    unloaded(&a->es,values[10]); unloaded(&a->cs,values[11]);
    unloaded(&a->ss,values[12]); unloaded(&a->ds,values[13]); unloaded(&a->ldtr,values[14]);
    a->msw|=8u; a->cpl=(uint8_t)(a->cs.selector&3u);
    a->trap_pending=0; a->interrupt_shadow=BM_286_SHADOW_NONE; r->phase=BM_286_TASK_REGISTERS;
    status=segment(c,r,&a->ldtr,0,q->external); if(status!=BM_STATUS_OK || r->fault_vector) return status;
    r->phase=BM_286_TASK_LDT;
    status=segment(c,r,&a->ss,1,q->external); if(status!=BM_STATUS_OK || r->fault_vector) return status;
    r->phase=BM_286_TASK_STACK;
    status=segment(c,r,&a->cs,2,q->external); if(status!=BM_STATUS_OK || r->fault_vector) return status;
    r->phase=BM_286_TASK_CODE;
    status=segment(c,r,&a->ds,3,q->external); if(status!=BM_STATUS_OK || r->fault_vector) return status;
    r->phase=BM_286_TASK_DATA;
    status=segment(c,r,&a->es,3,q->external); if(status!=BM_STATUS_OK || r->fault_vector) return status;
    if(q->has_error) {
        bm_286_pm_descriptor_t stack=bm_286_cached_descriptor(&a->ss);
        uint32_t top=a->sp?a->sp:65536u; uint16_t value=q->error_code;
        if(top<2 || !bm_286_pm_segment_contains(&stack,top-2,2)) return fault(r,12,0,false);
        status=word(c,a->ss.base+top-2,true,&value,r); if(status!=BM_STATUS_OK) return status;
        a->sp=(uint16_t)(top-2);
    }
    if(a->ip>a->cs.limit) return fault(r,13,0,false);
    if(returning) a->nmi_blocked=0;
    r->phase=BM_286_TASK_COMPLETE; return BM_STATUS_OK;
}

void bm_286_pm_publish_task(bm_286_arch_state_t *a,const bm_286_arch_state_t *n)
{
    a->ax=n->ax; a->cx=n->cx; a->dx=n->dx; a->bx=n->bx; a->sp=n->sp;
    a->bp=n->bp; a->si=n->si; a->di=n->di; a->ip=n->ip; a->flags=n->flags;
    a->msw=n->msw; a->cpl=n->cpl; a->es=n->es; a->cs=n->cs; a->ss=n->ss;
    a->ds=n->ds; a->ldtr=n->ldtr; a->tr=n->tr;
    a->trap_pending=n->trap_pending; a->interrupt_shadow=n->interrupt_shadow;
    /* nmi_pending is callback-owned, nmi_blocked is entry/IRET-owned. */
}
