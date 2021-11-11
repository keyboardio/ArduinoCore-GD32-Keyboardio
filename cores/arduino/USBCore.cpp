#include "USBCore.h"

#include "Arduino.h"

extern "C" {
#include "gd32/usb.h"
#include "usbd_enum.h"
#include "usbd_lld_regs.h"
#include "usbd_transc.h"
}

#include <cassert>

/*
 * #defines from Arduino:
 *   USB_MANUFACTURER
 *   USB_PRODUCT
 * These are both C-strings, not utf16, which is needed by USB; so
 * they need to be converted.
 */

#define STR_IDX_LANGID 0
#define STR_IDX_MFC 1
#define STR_IDX_PRODUCT 2
#define STR_IDX_SERIAL 3

// bMaxPower in Configuration Descriptor
#define USB_CONFIG_POWER_MA(mA)                ((mA)/2)
#ifndef USB_CONFIG_POWER
 #define USB_CONFIG_POWER                      (500)
#endif

// TODO: make the device descriptor a member variable which can be
// overridden by subclasses.
static usb_desc_dev devDesc = {
  .header = {
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
  .idVendor              = USB_VID,
  .idProduct             = USB_PID,
  .bcdDevice             = 0x0100,
  // Can set these to 0 so they’ll be ignored.
  .iManufacturer         = STR_IDX_MFC,
  .iProduct              = STR_IDX_PRODUCT,
  .iSerialNumber         = STR_IDX_SERIAL,
  // TODO: for PluggableUSB, should probably be 1. Configured in
  // usbd_conf.h
  .bNumberConfigurations = 1
};

usb_desc_config configDesc = {
  .header = {
    .bLength = sizeof(usb_desc_config),
    .bDescriptorType = USB_DESCTYPE_CONFIG
  },
  .wTotalLength = 0,
  .bNumInterfaces = 0,
  .bConfigurationValue = 1,
  .iConfiguration = 0,
  .bmAttributes = 0b10000000,
  .bMaxPower = USB_CONFIG_POWER_MA(USB_CONFIG_POWER)
};

/* USBD serial string */
static usb_desc_str serialDesc = {
  .header = {
    .bLength         = USB_STRING_LEN(12),
    .bDescriptorType = USB_DESCTYPE_STR,
  },
  .unicode_string = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}
};

/*
 * We need to keep the pointer for ‘STR_IDX_SERIAL’ because it’s
 * filled in by ‘usbd_init’.
 */
static uint8_t* stringDescs[] = {
  [STR_IDX_LANGID]  = nullptr,
  [STR_IDX_MFC]     = nullptr,
  [STR_IDX_PRODUCT] = nullptr,
  [STR_IDX_SERIAL]  = (uint8_t *)&serialDesc
};

usb_desc desc = {
  .dev_desc    = (uint8_t *)&devDesc,
  .config_desc = (uint8_t *)&configDesc,
  .bos_desc    = nullptr,
  .strings     = stringDescs
};

static uint8_t class_core_init(usb_dev* usbd, uint8_t config_index)
{
  (void)config_index;

  /*
   * Endpoint 0 is configured during startup, so skip it and only
   * handle what’s configured by ‘PluggableUSB’.
   */
  uint32_t buf_offset = EP0_RX_ADDR;
  for (uint8_t ep = 1; ep < PluggableUSB().epCount(); ep++) {
    uint16_t epBufferInfo = *(uint16_t*)epBuffer(ep);
    usb_desc_ep ep_desc = {
      .header = {
        .bLength = sizeof(ep_desc),
        .bDescriptorType = USB_DESCTYPE_EP,
      },
      .bEndpointAddress = EPTYPE_DIR(epBufferInfo) | ep,
      .bmAttributes = EPTYPE_TYPE(epBufferInfo),
      .wMaxPacketSize = USBD_EP0_MAX_SIZE,
      .bInterval = 0,
    };
    /*
     * Assume all endpoints have a max packet length of
     * ‘USBD_EP0_MAX_SIZE’.
     */
#if 0
    buf_offset += USBD_EP0_MAX_SIZE/2;
    assert(buf_offset <= (0x5fff - USBD_EP0_MAX_SIZE/2));
#else
    buf_offset += USBD_EP0_MAX_SIZE;
    assert(buf_offset <= (0x5fff - USBD_EP0_MAX_SIZE));
#endif
    usbd->ep_transc[ep][TRANSC_IN] = USBCore_::transcInHelper;
    usbd->ep_transc[ep][TRANSC_OUT] = USBCore_::transcOutHelper;
    usbd->drv_handler->ep_setup(usbd, EP_BUF_SNG, buf_offset, &ep_desc);
  }
  return USBD_OK;
}

static uint8_t class_core_deinit(usb_dev* usbd, uint8_t config_index)
{
  // TODO: Called when SetConfiguration setup packet sets the configuration
  // to 0.
  (void)usbd;
  (void)config_index;
  return USBD_OK;
}

// Called when ep0 gets a SETUP packet after configuration.
static uint8_t class_core_req_process(usb_dev* usbd, usb_req* req)
{
  (void)usbd;

  bool data_sent = false;
  arduino::USBSetup setup;
  memcpy(&setup, req, sizeof(setup));
  if (setup.bRequest == USB_GET_DESCRIPTOR) {
    auto sent = PluggableUSB().getDescriptor(setup);
    if (sent > 0) {
      data_sent = true;
    } else if (sent < 0) {
      return REQ_NOTSUPP;
    }
  } else {
    if (PluggableUSB().setup(setup)) {
      //data_sent = true;
    }
  }
  if (data_sent) {
    USBCore().flush(0);
  }
  return REQ_SUPP;
}

// Called when ep0 is done sending all data from an IN stage.
static uint8_t class_core_ctlx_in(usb_dev* usbd)
{
  (void)usbd;
  return REQ_SUPP;
}

// Called when ep0 is done receiving all data from an OUT stage.
static uint8_t class_core_ctlx_out(usb_dev* usbd)
{
  (void)usbd;
  return REQ_SUPP;
}

// Appears to be unused in usbd library, but used in usbfs.
static void class_core_data_in(usb_dev* usbd, uint8_t ep)
{
  (void)usbd;
  (void)ep;
  return;
}

// Appears to be unused in usbd library, but used in usbfs.
static void class_core_data_out(usb_dev* usbd, uint8_t ep)
{
  (void)usbd;
  (void)ep;
  return;
}

usb_class class_core = {
  .req_cmd	= 0xff,
  .req_altset   = 0x0,
  .init		= class_core_init,
  .deinit	= class_core_deinit,
  .req_process	= class_core_req_process,
  .ctlx_in	= class_core_ctlx_in,
  .ctlx_out	= class_core_ctlx_out,
  .data_in	= class_core_data_in,
  .data_out	= class_core_data_out,
};

template<size_t L>
size_t EPBuffer<L>::push(const void *d, size_t len)
{
  size_t w = min(this->remaining(), len);
  memcpy(this->p, d, w);
  this->p += w;
  return w;
}

template<size_t L>
void EPBuffer<L>::reset()
{
  this->p = this->buf;
}

template<size_t L>
size_t EPBuffer<L>::len()
{
  return this->p - this->buf;
}

template<size_t L>
size_t EPBuffer<L>::remaining()
{
  return this->tail - this->p;
}

int fl = 0;
int wfrc = 0;
int mc = 0;

template<size_t L>
void EPBuffer<L>::flush(uint8_t ep)
{
  fl++;
  Serial.print("f");
  if (this->txWaiting) {
    /*
     * Busy loop until the previous IN transaction completes.
     */
    this->waitForWriteComplete(ep);
  }
  this->txWaiting = true;
  usbd.drv_handler->ep_write(this->buf, ep, this->len());
  this->reset();
}

template<size_t L>
void EPBuffer<L>::markComplete()
{
  mc++;
  Serial.println("c");
  this->txWaiting = false;
}

// Busy loop until an OUT packet is received on endpoint ‘ep’.
template<size_t L>
void EPBuffer<L>::waitForDataReady(uint8_t ep)
{
  while (this->txWaiting) {
    volatile uint16_t int_status = (uint16_t)USBD_INTF;
    uint8_t ep_num = int_status & INTF_EPNUM;
    if ((int_status & INTF_STIF) == INTF_STIF
        && (int_status & INTF_DIR) == INTF_DIR
        && (USBD_EPxCS(ep_num) & EPxCS_RX_ST) == EPxCS_RX_ST
        && ep_num == ep) {
      USBD_EP_RX_ST_CLEAR(ep);
      this->markComplete();
    }
  }
}

// Busy loop until the latest IN packet has been sent from endpoint
// ‘ep’.
template<size_t L>
void EPBuffer<L>::waitForWriteComplete(uint8_t ep)
{
  wfrc++;
  /*
   * I’m not sure how much of this is necessary, but this is the
   * series of checks that’s used by ‘usbd_isr’ to verify the IN
   * packet has been sent.
   */
  while (this->txWaiting) {
    volatile uint16_t int_status = (uint16_t)USBD_INTF;
    uint8_t ep_num = int_status & INTF_EPNUM;
    if ((int_status & INTF_STIF) == INTF_STIF
        && (int_status & INTF_DIR) == 0
        && (USBD_EPxCS(ep_num) & EPxCS_TX_ST) == EPxCS_TX_ST
        && ep_num == ep) {
      USBD_EP_TX_ST_CLEAR(ep);
      this->markComplete();
    }
  }
}

USBCore_::USBCore_()
{
  usb_init(&desc, &class_core);
  usbd.user_data = this;

  this->oldTranscSetup = usbd.ep_transc[0][TRANSC_SETUP];
  usbd.ep_transc[0][TRANSC_SETUP] = USBCore_::transcSetupHelper;

  this->oldTranscOut = usbd.ep_transc[0][TRANSC_OUT];
  usbd.ep_transc[0][TRANSC_OUT] = USBCore_::transcOutHelper;

  this->oldTranscIn = usbd.ep_transc[0][TRANSC_IN];
  usbd.ep_transc[0][TRANSC_IN] = USBCore_::transcInHelper;

  this->oldTranscUnknown = usbd.ep_transc[0][TRANSC_UNKNOWN];
  usbd.ep_transc[0][TRANSC_UNKNOWN] = USBCore_::transcUnknownHelper;
}

void USBCore_::connect()
{
  Serial.begin(115200);
  Serial.println("connect");
  usb_connect();
}

// Send ‘len’ octets of ‘d’ through the control pipe (endpoint 0).
// Blocks until ‘len’ octets are sent. Returns the number of octets
// sent, or -1 on error.
int USBCore_::sendControl(uint8_t flags, const void* data, int len)
{
  // TODO: parse out flags like we do for ‘send’.
  (void)flags;
  uint8_t* d = (uint8_t*)data;
  auto l = min(len, this->maxWrite);
  auto wrote = 0;
  while (wrote < l) {
    size_t w = this->epBufs[0].push(d, l - wrote);
    d += w;
    wrote += w;
    this->maxWrite -= w;
    if (this->sendSpace(0) == 0) {
      this->flush(0);
    }
  }

  return len;
}

// Does not timeout or cross fifo boundaries. Returns the number of
// octets read.
int USBCore_::recvControl(void* d, int len)
{
  auto read = 0;
  return read;
  while (read < len) {
    usbd.drv_handler->ep_rx_enable(&usbd, 0);
    // TODO: use epBufs
    //this->waitForDataReady(0);
    //this->clearDataReady(0);
    read += usbd.drv_handler->ep_read((uint8_t*)d+read, 0, EP_BUF_SNG);

    // TODO: break if ‘read’ is less than endpoint’s max packet
    // length, indicating short packet.
  }

  return read;
}

// TODO: no idea? this isn’t in the avr 1.8.2 library, although it has
// the function prototype.
int USBCore_::recvControlLong(void* d, int len)
{
  (void)d;
  (void)len;
  return -1;
}

// Number of octets available on OUT endpoint.
uint8_t USBCore_::available(uint8_t ep)
{
  return this->epBufs[ep].len();
}

// Space left in IN endpoint buffer.
uint8_t USBCore_::sendSpace(uint8_t ep)
{
  return this->epBufs[ep].remaining();
}

// Blocking send of data to an endpoint. Returns the number of octets
// sent, or -1 on error.
int USBCore_::send(uint8_t ep, const void* data, int len)
{
  uint8_t* d = (uint8_t*)data;
  // Top nybble is used for flags.
  auto flags = ep & 0xf0;
  ep &= 0x7;
  auto wrote = 0;

  auto transc = &usbd.transc_in[ep];
  usb_transc_config(transc, nullptr, 0, 0);

  // TODO: query the endpoint for its max packet length.
  while (wrote < len) {
    auto w = 0;
    auto toWrite = len - wrote;
    if (flags & TRANSFER_ZERO) {
      // TODO: handle writing zeros instead of ‘d’.
      return -1;
    } else {
      w = this->epBufs[ep].push(d, toWrite);
    }
    d += w;
    wrote += w;

    if (this->sendSpace(ep) == 0) {
      this->flush(ep);
    }
  }

  if (flags & TRANSFER_RELEASE) {
    this->flush(ep);
  }

  return wrote;
}

// Non-blocking receive. Returns the number of octets read, or -1 on
// error.
int USBCore_::recv(uint8_t ep, void* d, int len)
{
  (void)ep;
  (void)d;
  (void)len;
  return -1;
}

// Receive one octet from OUT endpoint ‘ep’. Returns -1 if no bytes
// available.
int USBCore_::recv(uint8_t ep)
{
  (void)ep;
  return -1;
}

// Flushes an outbound transmission as soon as possible.
int USBCore_::flush(uint8_t ep)
{
  this->epBufs[ep].flush(ep);
  return 0;
}

void USBCore_::transcSetupHelper(usb_dev* usbd, uint8_t ep)
{
  USBCore_* core = (USBCore_*)usbd->user_data;
  core->transcSetup(usbd, ep);
}

void USBCore_::transcOutHelper(usb_dev* usbd, uint8_t ep)
{
  USBCore_* core = (USBCore_*)usbd->user_data;
  core->transcOut(usbd, ep);
}

void USBCore_::transcInHelper(usb_dev* usbd, uint8_t ep)
{
  USBCore_* core = (USBCore_*)usbd->user_data;
  core->transcIn(usbd, ep);
}

void USBCore_::transcUnknownHelper(usb_dev* usbd, uint8_t ep)
{
  USBCore_* core = (USBCore_*)usbd->user_data;
  core->transcUnknown(usbd, ep);
}

// Called in interrupt context.
void USBCore_::transcSetup(usb_dev* usbd, uint8_t ep)
{
  (void)ep;

  Serial.print("S");
  usb_reqsta reqstat = REQ_NOTSUPP;

  uint16_t count = usbd->drv_handler->ep_read((uint8_t *)(&usbd->control.req), 0, (uint8_t)EP_BUF_SNG);

  if (count != USB_SETUP_PACKET_LEN) {
    Serial.print("!");
    usbd_ep_stall(usbd, 0);

    return;
  }

  Serial.print(".");
  this->maxWrite = usbd->control.req.wLength;
  switch (usbd->control.req.bmRequestType & USB_REQTYPE_MASK) {
    /* standard device request */
  case USB_REQTYPE_STRD:
    if (usbd->control.req.bRequest == USB_GET_DESCRIPTOR
        && (usbd->control.req.bmRequestType & USB_RECPTYPE_MASK) == USB_RECPTYPE_DEV
        && (usbd->control.req.wValue >> 8) == USB_DESCTYPE_CONFIG) {
      Serial.println("confdesc");
      this->sendDeviceConfigDescriptor();
      return;
    } else if (usbd->control.req.bRequest == USB_GET_DESCRIPTOR
               && (usbd->control.req.bmRequestType & USB_RECPTYPE_MASK) == USB_RECPTYPE_DEV
               && (usbd->control.req.wValue >> 8) == USB_DESCTYPE_STR) {
      Serial.println("strdesc");
      this->sendDeviceStringDescriptor();
      return;
    } else if ((usbd->control.req.bmRequestType & USB_RECPTYPE_MASK) == USB_RECPTYPE_ITF) {
      Serial.println("itfdesc");
      class_core_req_process(usbd, &usbd->control.req);
      return;
    } else {
      Serial.println("stddesc");
      reqstat = usbd_standard_request(usbd, &usbd->control.req);
    }
    break;

    /* device class request */
  case USB_REQTYPE_CLASS:
    // Calls into class_core->req_process, does nothing else.
    Serial.println("classdesc");
    reqstat = usbd_class_request(usbd, &usbd->control.req);
    break;

    /* vendor defined request */
  case USB_REQTYPE_VENDOR:
    // Does nothing.
    Serial.println("vendordesc");
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
void USBCore_::transcOut(usb_dev* usbd, uint8_t ep)
{
  this->epBufs[ep].markComplete();
  this->oldTranscOut(usbd, ep);
}

// Called in interrupt context.
void USBCore_::transcIn(usb_dev* usbd, uint8_t ep)
{
  this->epBufs[ep].markComplete();
  this->oldTranscIn(usbd, ep);
}

void USBCore_::transcUnknown(usb_dev* usbd, uint8_t ep)
{
  this->oldTranscUnknown(usbd, ep);
}

void USBCore_::sendDeviceConfigDescriptor()
{
  auto oldMaxWrite = this->maxWrite;
  this->maxWrite = 0;
  uint8_t interfaceCount = 0;
  uint16_t len = PluggableUSB().getInterface(&interfaceCount);

  configDesc.wTotalLength = sizeof(configDesc) + len;
  configDesc.bNumInterfaces = interfaceCount;
  this->maxWrite = oldMaxWrite;
  this->sendControl(0, &configDesc, sizeof(configDesc));
  interfaceCount = 0;
  PluggableUSB().getInterface(&interfaceCount);
  // TODO: verify this sends ZLP properly when:
  //   wTotalLength % sizeof(this->buf) == 0
  this->flush(0);
}

void USBCore_::sendDeviceStringDescriptor()
{
  switch (lowByte(usbd.control.req.wValue)) {
  case STR_IDX_LANGID: {
    const usb_desc_LANGID desc = {
      .header = {
        .bLength = sizeof(usb_desc_LANGID),
        .bDescriptorType = USB_DESCTYPE_STR
      },
      .wLANGID = ENG_LANGID
    };
    USBCore().sendControl(0, &desc, desc.header.bLength);
    USBCore().flush(0);
    return;
  }
  case STR_IDX_MFC:
    this->sendStringDesc(USB_MANUFACTURER);
    break;
  case STR_IDX_PRODUCT:
    this->sendStringDesc(USB_PRODUCT);
    break;
  case STR_IDX_SERIAL:
    USBCore().sendControl(0, &serialDesc, serialDesc.header.bLength);
    USBCore().flush(0);
    break;
  default:
    usbd.drv_handler->ep_stall_set(&usbd, 0);
    return;
  }
}

void USBCore_::sendStringDesc(const char *str)
{
  usb_desc_header header = {
    .bLength = sizeof(header) + strlen(str) * 2,
    .bDescriptorType = USB_DESCTYPE_STR
  };

  USBCore().sendControl(0, &header, sizeof(header));
  for (size_t i = 0; i < strlen(str); i++) {
    uint8_t zero = 0;
    USBCore().sendControl(0, &str[i], sizeof(str[i]));
    USBCore().sendControl(0, &zero, sizeof(zero));
  }
  USBCore().flush(0);
}

void USBCore_::sendZLP(usb_dev* usbd, uint8_t ep)
{
  usbd->drv_handler->ep_write(nullptr, ep, 0);
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
