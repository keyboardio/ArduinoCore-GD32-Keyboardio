#include "api/PluggableUSB.h"

extern "C" {
#include "gd32/usb.h"

  // USBD_EP0_MAX_SIZE
#include "usbd_core.h"

  // usb_reqsta, ENG_LANGID
#include "usbd_enum.h"
}

// USBD_EP0_MAX_SIZE from ‘usbd_core.h’ is max packet length.
// the usb_transc structure (ibid) has it per-endpoint in ‘max_len’.
// Can use ‘udev->usb_ep_transc(udev, endpoint_num)’ to get it.

// NB: cannot use actual transactions because they will send data at
// the end of the buffer, and we need to collect it across multiple
// calls to ‘USB_SendControl‘. To that end, we can write directly into
// its EP buffer, then, if we’re at maxPacketLen, send it (via
// USB_flush?) and then wait for the buffer to become clear again so
// we can continue writing to it.
//
// Looks like ‘udev->ep_status(udev, endpoint_num)’ can be used. Seems
// like true values indicate ready-to-send/receive.
//
// Ideally, we’d copy straight into USB RAM, but since ‘btable_ep’ is
// hidden inside ‘usbd_lld_core’, we’d have to use the same constants
// it does, which is perhaps poor from a code-duplication standpoint?
// But then again, it is literally how the chip works according to the
// data sheet:
//
// USBD_RAM + 2 * (BTABLE_OFFSET & 0xFFF8)
//
// Never-the-less, it would be better to get this from the firmware
// library.
//
// Actually, since we have to bypass usbd_ep_data_write anyway, I
// think we’re going to have to copy straight into this buffer.

// We can use buf for zero-copy by way of ‘usbd_ep_init’ from
// ‘usb_core.h’. This isn’t used for endpoint 0 for some reason in the
// extant code, but there’s no reason it shouldn’t work.

#define USBD_VID 0xdead
#define USBD_PID 0xbeef

#define STR_IDX_LANGID 0
#define STR_IDX_MFC 1
#define STR_IDX_PRODUCT 2
#define STR_IDX_SERIAL 3

static volatile bool TX_AVAILABLE = true; // TODO: Needs atomic protection in the event of multi-core.
static uint8_t buf[USBD_EP0_MAX_SIZE];
static uint8_t* tail = buf + sizeof(buf);
static uint8_t* p = buf;

static void (*old_transc_setup)(usb_dev*, uint8_t);
static void (*old_transc_out)(usb_dev*, uint8_t);
static void (*old_transc_in)(usb_dev*, uint8_t);
static void (*old_transc_unknown)(usb_dev*, uint8_t);

static void transc_setup(usb_dev*, uint8_t);
static void transc_out(usb_dev*, uint8_t);
static void transc_in(usb_dev*, uint8_t);
static void transc_unknown(usb_dev*, uint8_t);

static inline void send_zlp(usb_dev*, uint8_t);

static usb_desc_dev dev_desc =
  {
    .header =
    {
      .bLength          = USB_DEV_DESC_LEN,
      .bDescriptorType  = USB_DESCTYPE_DEV
    },
    .bcdUSB                = 0x0200U,
    .bDeviceClass          = 0x00U,
    .bDeviceSubClass       = 0x00U,
    .bDeviceProtocol       = 0x00U,
    // TODO: this depends on what the mcu can support, but this is
    // device dependent code, so nevermind?
    .bMaxPacketSize0       = USBD_EP0_MAX_SIZE,
    .idVendor              = USBD_VID,
    .idProduct             = USBD_PID,
    .bcdDevice             = 0x0100U,
    // Can set these to 0 so they’ll be ignored.
    .iManufacturer         = STR_IDX_MFC,
    .iProduct              = STR_IDX_PRODUCT,
    .iSerialNumber         = STR_IDX_SERIAL,
    // TODO: for PluggableUSB, should probably be 1. Configured in
    // usbd_conf.h
    .bNumberConfigurations = USBD_CFG_MAX_NUM
  };

static uint8_t config_desc[] = {
  // bLength, bDescriptorType
  9, 2,

  // wTotalLength, bNumInterfaces, bConfigurationValue, iConfiguration
  25, 0, 1, 1, 0,

  // bmAttributes, bMaxPower
  0b10000000, 50,

  /*
   * Interface 1
   */
  // bLength, bDescriptorType
  9, 4,

  // bInterfaceNumber, bAlternateSetting, bNumEndpoints
  0, 0, 1,

  // bInterfaceClass, bInterfaceSubClass, bInterfaceProtocol
  3, 1, 1,

  // iInterface
  0,

  /*
   * Endpoint 1
   */
  // bLength, bDescriptorType
  7, 5,

  // bEndpointAddress, bmAttributes, wMaxPacketSize, bInterval
  0b10000001, 0b00000011, 8, 0, 64,
};

/* USB language ID Descriptor */
const usb_desc_LANGID usbd_language_id_desc =
  {
    .header =
    {
      .bLength         = sizeof(usb_desc_LANGID),
      .bDescriptorType = USB_DESCTYPE_STR
    },
    .wLANGID              = ENG_LANGID
  };

/* USB manufacture string */
static const usb_desc_str manufacturer_string =
  {
    .header =
    {
      .bLength         = USB_STRING_LEN(10U),
      .bDescriptorType = USB_DESCTYPE_STR,
    },
    .unicode_string = {'G', 'i', 'g', 'a', 'D', 'e', 'v', 'i', 'c', 'e'}
  };

/* USB product string */
static const usb_desc_str product_string =
  {
    .header =
    {
      .bLength         = USB_STRING_LEN(17U),
      .bDescriptorType = USB_DESCTYPE_STR,
    },
    .unicode_string = {'D', 'e', 'v', 'T', 'e','s', 't', '-', 'P', 'l', 'u', 'g', 'g', 'a', 'b', 'l', 'e', 'U', 'S', 'B' }
  };

/* USBD serial string */
static usb_desc_str serial_string =
  {
    .header =
    {
      .bLength         = USB_STRING_LEN(12U),
      .bDescriptorType = USB_DESCTYPE_STR,
    }
  };

static uint8_t* usbd_hid_strings[] =
  {
    [STR_IDX_LANGID]  = (uint8_t *)&usbd_language_id_desc,
    [STR_IDX_MFC]     = (uint8_t *)&manufacturer_string,
    [STR_IDX_PRODUCT] = (uint8_t *)&product_string,
    [STR_IDX_SERIAL]  = (uint8_t *)&serial_string
  };

usb_desc desc = {
  .dev_desc    = (uint8_t *)&dev_desc,
  .config_desc = config_desc,
  .bos_desc    = nullptr,
  .strings     = usbd_hid_strings
};

static uint8_t class_core_init(usb_dev *udev, uint8_t config_index)
{
  return USBD_OK;
}

static uint8_t class_core_deinit(usb_dev *udev, uint8_t config_index)
{
  return USBD_OK;
}

static uint8_t class_core_req_handler(usb_dev *udev, usb_req *req)
{
  return REQ_SUPP;
}

// Can’t use -- this call is immediately followed by a status out
// stage, and the most we can do here is queue up another outgoing
// packet after the interrupt is over.
//
// May actually work? ‘usb_ctl_out’ only enables reception, which we
// shouldn’t get until the data transfer is complete.
static uint8_t class_core_ctlx_in(usb_dev *udev)
{
  return REQ_SUPP;
}

static uint8_t class_core_ctlx_out(usb_dev *udev)
{
  return REQ_SUPP;
}

static void class_core_data_in(usb_dev *udev, uint8_t ep_num)
{
  return;
}

static void class_core_data_out(usb_dev *udev, uint8_t ep_num)
{
  return;
}

usb_class class_core = {
  .req_cmd	= 0xFFU,
  .init		= class_core_init,
  .deinit		= class_core_deinit,
  .req_process	= class_core_req_handler,
  .ctlx_in	= class_core_ctlx_in,
  .ctlx_out	= class_core_ctlx_out,
  .data_in	= class_core_data_in,
  .data_out	= class_core_data_out,
};

void usbcore_init() {
  usb_init(&desc, &class_core);

  old_transc_setup = usbd.ep_transc[0][TRANSC_SETUP];
  usbd.ep_transc[0][TRANSC_SETUP] = transc_setup;

  old_transc_out = usbd.ep_transc[0][TRANSC_OUT];
  usbd.ep_transc[0][TRANSC_OUT] = transc_out;

  old_transc_in = usbd.ep_transc[0][TRANSC_IN];
  usbd.ep_transc[0][TRANSC_IN] = transc_in;

  old_transc_unknown = usbd.ep_transc[0][TRANSC_UNKNOWN];
  usbd.ep_transc[0][TRANSC_UNKNOWN] = transc_unknown;
}

// usb_transc_config(udev->transc_in[ep_num], pbuf, buf_len, 0U);

static int calls = 0;
static usb_reqsta send_dev_config_desc(usb_dev* usbd) {
  uint8_t interfaces;
  calls++;

  // TODO: need to call ‘getInterface’ twice, once to find out how
  // many interfaces there even are.
  const uint8_t config_header[] = {
    // bLength, bDescriptorType
    9, 2,

    // wTotalLength, bNumInterfaces, bConfigurationValue, iConfiguration
    25, 0, 1, 1, 0,

    // bmAttributes, bMaxPower
    0b10000000, 50,
  };
  USB_SendControl(0, &config_header, sizeof(config_header));
  PluggableUSB().getInterface(&interfaces);
  USB_Flush(0);
  return REQ_SUPP;
}

// Called in interrupt context.
static void transc_setup(usb_dev* usbd, uint8_t ep_num) {
  usb_reqsta reqstat = REQ_NOTSUPP;

  uint16_t count = usbd->drv_handler->ep_read((uint8_t *)(&usbd->control.req), 0U, (uint8_t)EP_BUF_SNG);

  if (count != USB_SETUP_PACKET_LEN) {
    usbd_ep_stall(usbd, 0x0U);

    return;
  }

  switch (usbd->control.req.bmRequestType & USB_REQTYPE_MASK) {
    /* standard device request */
  case USB_REQTYPE_STRD:
    // TODO: The problem!
    // Maybe just work around usbd->control.req->bRequest == USB_GET_DESCRIPTOR, bRequestType == USB_DESCTYPE_{DEV,CONFIG,STR}
    if (usbd->control.req.bRequest == USB_GET_DESCRIPTOR
        && (usbd->control.req.bRequest & USB_RECPTYPE_MASK) == USB_RECPTYPE_DEV
        && (usbd->control.req.wValue >> 8) == USB_DESCTYPE_CONFIG) {
      reqstat = send_dev_config_desc(usbd);
    } else {
      reqstat = usbd_standard_request(usbd, &usbd->control.req);
    }
    break;

    /* device class request */
  case USB_REQTYPE_CLASS:
    // Calls into class_core->req_process, does nothing else.
    reqstat = usbd_class_request(usbd, &usbd->control.req);
    break;

    /* vendor defined request */
  case USB_REQTYPE_VENDOR:
    // Does nothing.
    reqstat = usbd_vendor_request(usbd, &usbd->control.req);
    break;

  default:
    break;
  }

  if (REQ_SUPP == reqstat) {
    if (0U == usbd->control.req.wLength) {
      /* USB control transfer status in stage */
      send_zlp(usbd, 0);
    } else {
      if (usbd->control.req.bmRequestType & 0x80U) {
        usbd_ep_send(usbd, 0U, usbd->transc_in[0].xfer_buf, usbd->transc_in[0].xfer_len);
      } else {
        /* USB control transfer data out stage */
        usbd->drv_handler->ep_rx_enable(usbd, 0U);
      }
    }
  } else {
    usbd_ep_stall(usbd, 0x0U);
  }
}

// Called in interrupt context.
static void transc_out(usb_dev* usbd, uint8_t ep_num) {
  old_transc_out(usbd, ep_num);
}

// Called in interrupt context.
static void transc_in(usb_dev* usbd, uint8_t ep_num) {
  // Mark this endpoint’s transaction as complete.
  TX_AVAILABLE = true;

  old_transc_in(usbd, ep_num);
}

static void transc_unknown(usb_dev* usbd, uint8_t ep_num) {
  old_transc_unknown(usbd, ep_num);
}

static inline void send_zlp(usb_dev* usbd, uint8_t ep_num) {
  usbd->drv_handler->ep_write(nullptr, 0, 0);
}

// Send ‘len’ octets of ‘d’ through the control pipe (endpoint 0).
// Blocks until ‘len’ octets are sent. Returns the number of octets
// sent, or -1 on error.
int USB_SendControl(uint8_t flags, const void* d, int len)
{
  while (len > 0) {
    // TODO: this will break when using ‘USB_SendControl’ to calculate
    // the config descriptor length, because ‘transc_in’ isn’t called
    // in that circumstance.
    while (!TX_AVAILABLE) {
      // busy loop until the previous transaction was processed.
      //wfi();
    }

    // usb_transc_config(udev->transc_in[0], p, len, 0U);
    for (; p < tail && len > 0; p++, len--) {
      *p = *(uint8_t *)d;
      d++;
    }

    if (p == tail) {
      if (len == 0) {
        // TODO: Send ZLP after the previous packet finishes. Mark the
        // transaction as needing a zlp when done.
      }
      USB_Flush(0);
      p = buf;
    }
  }

  return len;
}

// Does not timeout or cross fifo boundaries. Returns the number of
// octets read.
int USB_RecvControl(void* d, int len)
{
  return -1;
}

// TODO: no idea? this isn’t in the avr 1.8.2 library, although it has
// the function prototype.
int USB_RecvControlLong(void* d, int len)
{
  return -1;
}

// Number of octets available on OUT endpoint.
uint8_t USB_Available(uint8_t ep)
{
  return 0;
}

// Space left in IN endpoint buffer.
uint8_t USB_SendSpace(uint8_t ep)
{
  return tail-p;
}

// Blocking send of data to an endpoint. Returns the number of octets
// sent, or -1 on error.
int USB_Send(uint8_t ep, const void* d, int len)
{
  return -1;
}

// Non-blocking receive. Returns the number of octets read, or -1 on
// error.
int USB_Recv(uint8_t ep, void* d, int len)
{
  return -1;
}

// Receive one octet from OUT endpoint ‘ep’. Returns -1 if no bytes
// available.
int USB_Recv(uint8_t ep)
{
  return -1;
}

// Flushes an outbound transaction unconditionally.
void USB_Flush(uint8_t ep) {
  // TODO: set up transaction.
  TX_AVAILABLE = false;
  usbd.drv_handler->ep_write(buf, ep, p-buf);
}
