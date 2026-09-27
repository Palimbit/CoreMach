/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2026 BluMach contributors */
#include <blumach/systems/olivetti_pcs286.h>
#include <blumach/platforms/null_host.h>
#include <assert.h>
#include <string.h>
static uint8_t bios[BM_PCS286_FIRMWARE_BYTES];
int main(void)
{
 bm_host_services_t host=bm_null_host_services();
 bm_pcs286_config_t config={0};
 bm_machine_config_t mc;
 bm_session_t *a=NULL,*b=NULL;
 bm_input_event_t key={0};
 bm_video_geometry_t geometry;
 uint64_t count; size_t size; uint8_t cmos[128];
 /* Authored infinite loop at true reset vector; no preserved firmware. */
 bios[0x1fff0]=0xeb; bios[0x1fff1]=0xfe;
 config.size=sizeof(config); config.version=BM_PCS286_CONFIG_VERSION;
 config.ram_kib=1024; config.firmware.image[0].data=bios;
 config.firmware.image[0].size=sizeof(bios);
 config.floppy[0].installed=1;
 config.floppy[0].geometry=(bm_floppy_geometry_t){80,2,18,512};
 mc=bm_pcs286_machine_config(&config);
 assert(bm_session_create(&host,&a)==BM_STATUS_OK);
 assert(bm_session_create(&host,&b)==BM_STATUS_OK);
 assert(bm_session_configure(a,&mc)==BM_STATUS_OK);
 assert(bm_session_configure(b,&mc)==BM_STATUS_OK);
 assert(bm_session_start(a)==BM_STATUS_OK);
 assert(bm_session_start(b)==BM_STATUS_OK);
 assert(bm_session_run_for(a,10000)==BM_STATUS_OK);
 assert(bm_session_inspect_machine(a,"boundaries",&count)==BM_STATUS_OK && count==10);
 assert(bm_session_inspect_machine(b,"boundaries",&count)==BM_STATUS_OK && count==0);
 assert(bm_session_video_geometry(a,&geometry)==BM_STATUS_OK);
 assert(bm_session_pause(a)==BM_STATUS_OK);
 key.kind=BM_INPUT_KEY; key.key=BM_KEY_F1; key.pressed=1;
 assert(bm_session_send_input(a,&key)==BM_STATUS_OK);
 key.pressed=0; assert(bm_session_send_input(a,&key)==BM_STATUS_OK);
 assert(bm_session_persistent_state_size(a,"rtc",&size)==BM_STATUS_OK && size==128);
 assert(bm_session_save_persistent_state(a,"rtc",cmos,sizeof(cmos))==BM_STATUS_OK);
 assert(cmos[6]==3);
 assert(bm_session_reset(a)==BM_STATUS_OK);
 assert(bm_session_inspect_machine(a,"boundaries",&count)==BM_STATUS_OK && count==0);
 assert(bm_session_resume(a)==BM_STATUS_OK);
 assert(bm_session_run_for(a,10000)==BM_STATUS_OK);
 assert(bm_session_stop(a)==BM_STATUS_OK);
 bm_session_destroy(a); bm_session_destroy(b);
 /* Manual NOP timing must affect throughput, not only report metadata.
  * Fourteen NOPs and a short jump loop entirely inside the reset ROM. */
 memset(bios+0x1fff0,0x90,14);
 bios[0x1fffe]=0xeb; bios[0x1ffff]=0xf0;
 a=NULL;
 assert(bm_session_create(&host,&a)==BM_STATUS_OK);
 assert(bm_session_configure(a,&mc)==BM_STATUS_OK);
 assert(bm_session_start(a)==BM_STATUS_OK);
 assert(bm_session_run_for(a,10000)==BM_STATUS_OK);
 assert(bm_session_inspect_machine(a,"timing_manual_steps",&count)==BM_STATUS_OK && count>10);
 assert(bm_session_inspect_machine(a,"peripheral_ns",&count)==BM_STATUS_OK && count>=9000 && count<=12000);
 assert(bm_session_reset(a)==BM_STATUS_OK);
 assert(bm_session_inspect_machine(a,"timing_manual_steps",&count)==BM_STATUS_OK && count==0);
 assert(bm_session_stop(a)==BM_STATUS_OK);
 bm_session_destroy(a);
 return 0;
}
