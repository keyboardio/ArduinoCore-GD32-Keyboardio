#ifndef _GD_USB_H
#define _GD_USB_H

#include "usbd_core.h"
#include "usbd_lld_core.h"

extern usb_dev usbd;

void usb_init(usb_desc*, usb_class*);
void usb_connect();
uint8_t* usb_ep_rx_addr(uint8_t);
uint32_t* usb_ep_rx_count(uint8_t);
uint8_t* usb_ep_tx_addr(uint8_t);
uint32_t* usb_ep_tx_count(uint8_t);

#endif
