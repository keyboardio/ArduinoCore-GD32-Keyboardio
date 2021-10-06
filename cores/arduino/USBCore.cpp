#include "USBCore.h"

#include "api/PluggableUSB.h"

#include <cstring>

extern "C" {
#include "gd32/usb.h"

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

static usb_desc_dev dev_desc =
  {
    .header =
    {
      .bLength          = USB_DEV_DESC_LEN,
      .bDescriptorType  = USB_DESCTYPE_DEV
    },
    .bcdUSB                = 0x0200,
    .bDeviceClass          = 0x00,
    .bDeviceSubClass       = 0x00,
    .bDeviceProtocol       = 0x00,
    // TODO: this depends on what the mcu can support, but this is
    // device dependent code, so nevermind?
    .bMaxPacketSize0       = USBD_EP0_MAX_SIZE,
    .idVendor              = USBD_VID,
    .idProduct             = USBD_PID,
    .bcdDevice             = 0x0100,
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
      .bLength         = USB_STRING_LEN(10),
      .bDescriptorType = USB_DESCTYPE_STR,
    },
    .unicode_string = {'G', 'i', 'g', 'a', 'D', 'e', 'v', 'i', 'c', 'e'}
  };

/* USB product string */
static const usb_desc_str product_string =
  {
    .header =
    {
      .bLength         = USB_STRING_LEN(17),
      .bDescriptorType = USB_DESCTYPE_STR,
    },
    .unicode_string = {'D', 'e', 'v', 'T', 'e','s', 't', '-', 'P', 'l', 'u', 'g', 'g', 'a', 'b', 'l', 'e', 'U', 'S', 'B' }
  };

/* USBD serial string */
static usb_desc_str serial_string =
  {
    .header =
    {
      .bLength         = USB_STRING_LEN(12),
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

static uint8_t class_core_init(usb_dev* usbd, uint8_t config_index)
{
  /*
   * Endpoint 0 is configured during startup, so skip it and only
   * handle what’s configured by ‘PluggableUSB’.
   */
  for (int i = 1; i < PluggableUSB().epCount(); i++) {
    usb_desc_ep ep_desc = {
      .header = {
        .bLength = sizeof(ep_desc),
        .bDescriptorType = USB_DESCTYPE_EP,
      },
      .bEndpointAddress = EPTYPE_ADDR(*(uint16_t *)epBuffer(i)),
      .bmAttributes = EPTYPE_TYPE(*(uint16_t *)epBuffer(i)),
      .wMaxPacketSize = USBD_EP0_MAX_SIZE,
      .bInterval = 0,
    };
    /*
     * Assume all endpoints have a max packet length of
     * ‘USBD_EP0_MAX_SIZE’ and are uni-directional.
     */
    uint32_t buf_addr = EP0_RX_ADDR + (i * USBD_EP0_MAX_SIZE/2);
    usbd->drv_handler->ep_setup(usbd, EP_BUF_SNG, buf_addr, &ep_desc);
  }
  return USBD_OK;
}

static uint8_t class_core_deinit(usb_dev* usbd, uint8_t config_index)
{
  // TODO: Called when SetConfiguration setup packet sets the configuration
  // to 0.
  return USBD_OK;
}

// Called when ep0 gets a SETUP packet.
static uint8_t class_core_req_process(usb_dev* usbd, usb_req* req)
{
  arduino::USBSetup setup;
  memcpy(&setup, req, sizeof(setup));
  PluggableUSB().setup(setup);
  return REQ_SUPP;
}

// Called when ep0 is done sending all data from an IN stage.
static uint8_t class_core_ctlx_in(usb_dev* usbd)
{
  return REQ_SUPP;
}

// Called when ep0 is done receiving all data from an OUT stage.
static uint8_t class_core_ctlx_out(usb_dev* usbd)
{
  return REQ_SUPP;
}

static void class_core_data_in(usb_dev* usbd, uint8_t ep_num)
{
  return;
}

static void class_core_data_out(usb_dev* usbd, uint8_t ep_num)
{
  return;
}

usb_class class_core = {
  .req_cmd	= 0xFFU,
  .init		= class_core_init,
  .deinit	= class_core_deinit,
  .req_process	= class_core_req_process,
  .ctlx_in	= class_core_ctlx_in,
  .ctlx_out	= class_core_ctlx_out,
  .data_in	= class_core_data_in,
  .data_out	= class_core_data_out,
};

// usb_transc_config(usbd->transc_in[ep_num], pbuf, buf_len, 0);

void PacketBuf::push(uint8_t d) {
  *this->p++ = d;
  if (this->p == this->tail) {
    USBCore().flush(this->ep);
  }
}

USBCore_::USBCore_()
{
  for (int i = 0; i < EP_COUNT; i++) {
    this->txAvailable[i] = true;
  }
}

void USBCore_::init()
{
  usb_init(&desc, &class_core);
  usbd.user_data = this;

  this->old_transc_setup = usbd.ep_transc[0][TRANSC_SETUP];
  usbd.ep_transc[0][TRANSC_SETUP] = USBCore_::_transc_setup;

  this->old_transc_out = usbd.ep_transc[0][TRANSC_OUT];
  usbd.ep_transc[0][TRANSC_OUT] = USBCore_::_transc_out;

  this->old_transc_in = usbd.ep_transc[0][TRANSC_IN];
  usbd.ep_transc[0][TRANSC_IN] = USBCore_::_transc_in;

  this->old_transc_unknown = usbd.ep_transc[0][TRANSC_UNKNOWN];
  usbd.ep_transc[0][TRANSC_UNKNOWN] = USBCore_::_transc_unknown;
}

// Send ‘len’ octets of ‘d’ through the control pipe (endpoint 0).
// Blocks until ‘len’ octets are sent. Returns the number of octets
// sent, or -1 on error.
int USBCore_::sendControl(uint8_t flags, const void* d, int len)
{
  int wrote = 0;

  while (wrote < len) {
    // TODO: this will break when using ‘USB_SendControl’ to calculate
    // the config descriptor length, because ‘transc_in’ isn’t called
    // in that circumstance.

    // usb_transc_config(usbd->transc_in[0], p, len, 0);
    for (; this->p < this->tail && len > 0; wrote++) {
      *this->p++ = *(uint8_t *)d++;
    }
    this->flush(0);
  }

  // Send a ZLP only when we’re done with the data transmission and
  // the final transmission is exactly the max packet size.
  if (wrote % sizeof(this->buf) == 0) {
    // TODO: check to see if we need ‘sendZLP’ here, or if
    // ‘transc_in’ will be called even when a ZLP is sent.
    //
    // If ‘transc_in’ is called, then this is going to be the more
    // correct behavior, because it waits for the transaction to
    // complete before allowing further ones.
    this->flush(0);
  }

  return wrote;
}

// Does not timeout or cross fifo boundaries. Returns the number of
// octets read.
int USBCore_::recvControl(void* d, int len)
{
  return -1;
}

// TODO: no idea? this isn’t in the avr 1.8.2 library, although it has
// the function prototype.
int USBCore_::recvControlLong(void* d, int len)
{
  return -1;
}

// Number of octets available on OUT endpoint.
uint8_t USBCore_::available(uint8_t ep)
{
  return 0;
}

// Space left in IN endpoint buffer.
uint8_t USBCore_::sendSpace(uint8_t ep)
{
  return this->tail - this->p;
}

// Blocking send of data to an endpoint. Returns the number of octets
// sent, or -1 on error.
int USBCore_::send(uint8_t ep, const void* d, int len)
{
  return -1;
}

// Non-blocking receive. Returns the number of octets read, or -1 on
// error.
int USBCore_::recv(uint8_t ep, void* d, int len)
{
  return -1;
}

// Receive one octet from OUT endpoint ‘ep’. Returns -1 if no bytes
// available.
int USBCore_::recv(uint8_t ep)
{
  return -1;
}

// Flushes an outbound transmission as soon as possible.
int USBCore_::flush(uint8_t ep)
{
  // TODO: this is broken: we’re flushing to an endpoint with ‘buf’
  // and ‘p’, which is not per-endpoint.
  while (!this->txAvailable[0]) {
    // busy loop until the previous transaction was processed.
    //wfi();
  }

  this->txAvailable[ep] = false;
  usbd.drv_handler->ep_write(buf, ep, p-buf);
  this->p = this->buf;
}

void USBCore_::_transc_setup(usb_dev* usbd, uint8_t ep)
{
  USBCore_* core = (USBCore_ *)usbd->user_data;
  core->transc_setup(usbd, ep);
}

void USBCore_::_transc_out(usb_dev* usbd, uint8_t ep)
{
  USBCore_* core = (USBCore_ *)usbd->user_data;
  core->transc_out(usbd, ep);
}

void USBCore_::_transc_in(usb_dev* usbd, uint8_t ep)
{
  USBCore_* core = (USBCore_ *)usbd->user_data;
  core->transc_in(usbd, ep);
}

void USBCore_::_transc_unknown(usb_dev* usbd, uint8_t ep)
{
  USBCore_* core = (USBCore_ *)usbd->user_data;
  core->transc_unknown(usbd, ep);
}

// Called in interrupt context.
void USBCore_::transc_setup(usb_dev* usbd, uint8_t ep) {
  usb_reqsta reqstat = REQ_NOTSUPP;

  uint16_t count = usbd->drv_handler->ep_read((uint8_t *)(&usbd->control.req), 0, (uint8_t)EP_BUF_SNG);

  if (count != USB_SETUP_PACKET_LEN) {
    usbd_ep_stall(usbd, 0);

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
      this->sendDeviceConfigDescriptor(usbd);
      reqstat = REQ_SUPP;
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

  if (reqstat == REQ_SUPP) {
    if (usbd->control.req.wLength == 0) {
      /* USB control transfer status in stage */
      this->sendZLP(usbd, 0);
    } else {
      if (usbd->control.req.bmRequestType & USB_TRX_IN) {
        usbd_ep_send(usbd, 0, usbd->transc_in[0].xfer_buf, usbd->transc_in[0].xfer_len);
      } else {
        /* USB control transfer data out stage */
        usbd->drv_handler->ep_rx_enable(usbd, 0);
      }
    }
  } else {
    usbd_ep_stall(usbd, 0);
  }
}

// Called in interrupt context.
void USBCore_::transc_out(usb_dev* usbd, uint8_t ep) {
  this->old_transc_out(usbd, ep);
}

// Called in interrupt context.
void USBCore_::transc_in(usb_dev* usbd, uint8_t ep) {
  // Mark this endpoint’s transaction as complete.
  this->txAvailable[ep] = true;

  this->old_transc_in(usbd, ep);
}

void USBCore_::transc_unknown(usb_dev* usbd, uint8_t ep) {
  this->old_transc_unknown(usbd, ep);
}

// TODO: make the device descriptor a member variable which can be
// overridden by subclasses.
void USBCore_::sendDeviceConfigDescriptor(usb_dev* usbd)
{
  uint8_t interfaces;

  // TODO: need to call ‘getInterface’ twice, once to find out how
  // many interfaces there even are.
  const uint8_t configHeader[] = {
    // bLength, bDescriptorType
    9, 2,

    // wTotalLength, bNumInterfaces, bConfigurationValue, iConfiguration
    25, 0, 1, 1, 0,

    // bmAttributes, bMaxPower
    0b10000000, 50,
  };
  this->sendControl(0, &configHeader, sizeof(configHeader));
  PluggableUSB().getInterface(&interfaces);
  this->flush(0);
}

void USBCore_::sendZLP(usb_dev* usbd, uint8_t ep)
{
  usbd->drv_handler->ep_write(nullptr, 0, 0);
}

USBCore_& USBCore()
{
  static USBCore_ core;
  return core;
}

// -> returns a pointer to the Nth element of the EP buffer structure
void* epBuffer(unsigned int n)
{
  static uint16_t endPoints[EP_COUNT] = { EPTYPE(0, USB_EP_ATTR_CTL) };
  return &(endPoints[n]);
}
