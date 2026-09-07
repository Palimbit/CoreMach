/* Copyright holders: Sarah Walker
   see COPYING for more details
*/
#ifndef EMU_NMI_H
#define EMU_NMI_H

extern int nmi_mask;

typedef void (*nmi_mask_callback_t)(int enabled, void *priv);

extern void nmi_set_mask_callback(nmi_mask_callback_t callback, void *priv);
extern int nmi;
extern int nmi_auto_clear;

extern void nmi_init(void);

extern void nmi_write(uint16_t port, uint8_t val, void *priv);

#endif /*EMU_NMI_H*/
