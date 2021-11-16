/*!
    \file    usbd_conf.h
    \brief   usb device driver basic configuration

    \version 2021-03-23, V2.0.0, demo for GD32F30x
*/

/*
    Copyright (c) 2021, GigaDevice Semiconductor Inc.

    Redistribution and use in source and binary forms, with or without modification,
are permitted provided that the following conditions are met:

    1. Redistributions of source code must retain the above copyright notice, this
       list of conditions and the following disclaimer.
    2. Redistributions in binary form must reproduce the above copyright notice,
       this list of conditions and the following disclaimer in the documentation
       and/or other materials provided with the distribution.
    3. Neither the name of the copyright holder nor the names of its contributors
       may be used to endorse or promote products derived from this software without
       specific prior written permission.

    THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY
OF SUCH DAMAGE.
*/

#ifndef __USBD_CONF_H
#define __USBD_CONF_H

#include "gd32f30x.h"

// USB_PULLUP stuff.
// TODO: think about how to file this away and include it.
#include "variant.h"

/*
 * define if low power mode is enabled; it allows entering the device
 * into DEEP_SLEEP mode following USB suspend event and wakes up after
 * the USB wakeup event is received.
 */
#undef USBD_LOWPWR_MODE_ENABLE

/* USB feature -- Self Powered */
/* #define USBD_SELF_POWERED */

/* link power mode support */
/* #define LPM_ENABLED */

/*
 * TODO: I’m currently using the maximum values allowed by the spec
 * for available interfaces and endpoints, because I can’t know this
 * ahead of time when using PluggableUSB. However, this wastes a fair
 * amount of memory: almost no device is going to have 256 interfaces.
 */
#define USBD_CFG_MAX_NUM 1
#define USBD_ITF_MAX_NUM 256

#define EP_COUNT 8

#define USB_STRING_COUNT 4

/*
 * Offset from USBD RAM base used to store endpoint buffer
 * descriptors.
 *
 * cf. GD32F30x User Manual §26.6.1.
 */
#define BTABLE_OFFSET 0

/*
 * Offsets from BTABLE in the peripheral for transmission and
 * reception buffers.
 *
 * These offsets are stored directly in the ‘USBD_EPxTBADDR’ and
 * ‘USBD_EPxRBADDR’ registers, and thus are half the real offset used
 * when accessing the data buffer.
 *
 * Other endpoint buffers are come after ‘EP0_RX_ADDR’, and assume the
 * maximum packet size is the same for all endpoints, at
 * ‘USBD_EP0_MAX_SIZE’ octets.
 */
#define EP0_TX_ADDR 0x40
#define EP0_RX_ADDR (EP0_TX_ADDR+USBD_EP0_MAX_SIZE)

#endif /* __USBD_CONF_H */
