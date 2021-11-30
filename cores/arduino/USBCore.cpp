#include "USBCore.h"

#include "Arduino.h"

extern "C" {
#include "gd32/usb.h"
#include "usbd_enum.h"
#include "usbd_lld_regs.h"
#include "usbd_transc.h"
}

#include <cassert>

#define USBD_VID 0xdead
#define USBD_PID 0xbeef

#define STR_IDX_LANGID 0
#define STR_IDX_MFC 1
#define STR_IDX_PRODUCT 2
#define STR_IDX_SERIAL 3

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
  .idVendor              = USBD_VID,
  .idProduct             = USBD_PID,
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
  .bmAttributes = 0b100000000,
  .bMaxPower = 50
};

/* USB language ID Descriptor */
const usb_desc_LANGID usbd_language_id_desc = {
  .header = {
    .bLength         = sizeof(usb_desc_LANGID),
    .bDescriptorType = USB_DESCTYPE_STR
  },
  .wLANGID = ENG_LANGID
};

/* USB manufacture string */
static const usb_desc_str manufacturer_string = {
  .header = {
    .bLength         = USB_STRING_LEN(7),
    .bDescriptorType = USB_DESCTYPE_STR,
  },
  .unicode_string = {'A', 'r', 'd', 'u', 'i', 'n', 'o'}
};

/* USB product string */
static const usb_desc_str product_string = {
  .header = {
    .bLength         = USB_STRING_LEN(8),
    .bDescriptorType = USB_DESCTYPE_STR,
  },
  .unicode_string = {'U', 'S', 'B', ' ', 't', 'e', 's', 't'}
};

/* USBD serial string */
static usb_desc_str serial_string = {
  .header = {
    .bLength         = USB_STRING_LEN(12),
    .bDescriptorType = USB_DESCTYPE_STR,
  }
};

static uint8_t* usbd_hid_strings[] = {
  [STR_IDX_LANGID]  = (uint8_t *)&usbd_language_id_desc,
  [STR_IDX_MFC]     = (uint8_t *)&manufacturer_string,
  [STR_IDX_PRODUCT] = (uint8_t *)&product_string,
  [STR_IDX_SERIAL]  = (uint8_t *)&serial_string
};

usb_desc desc = {
  .dev_desc    = (uint8_t *)&devDesc,
  .config_desc = (uint8_t *)&configDesc,
  .bos_desc    = nullptr,
  .strings     = usbd_hid_strings
};

static uint8_t class_core_init(usb_dev* usbd, uint8_t config_index)
{
  /*
   * Endpoint 0 is configured during startup, so skip it and only
   * handle what’s configured by ‘PluggableUSB’.
   */
  uint32_t buf_offset = EP0_RX_ADDR;
  for (uint8_t ep = 1; ep < PluggableUSB().epCount(); ep++) {
    usb_desc_ep ep_desc = {
      .header = {
        .bLength = sizeof(ep_desc),
        .bDescriptorType = USB_DESCTYPE_EP,
      },
      .bEndpointAddress = EPTYPE_DIR(*(uint16_t *)epBuffer(ep)) | ep,
      .bmAttributes = EPTYPE_TYPE(*(uint16_t *)epBuffer(ep)),
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
  return USBD_OK;
}

// Called when ep0 gets a SETUP packet after configuration.
static uint8_t class_core_req_process(usb_dev* usbd, usb_req* req)
{
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
  return REQ_SUPP;
}

// Called when ep0 is done receiving all data from an OUT stage.
static uint8_t class_core_ctlx_out(usb_dev* usbd)
{
  return REQ_SUPP;
}

// Appears to be unused in usbd library, but used in usbfs.
static void class_core_data_in(usb_dev* usbd, uint8_t ep_num)
{
  return;
}

// Appears to be unused in usbd library, but used in usbfs.
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

template<size_t L>
size_t EPBuffer<L>::push(const void *d, size_t len) {
  size_t w = min(this->remaining(), len);
  memcpy(this->p, d, w);
  this->p += w;
  return w;
}

template<size_t L>
void EPBuffer<L>::reset() {
  this->p = this->buf;
}

template<size_t L>
size_t EPBuffer<L>::len() {
  return this->p - this->buf;
}

template<size_t L>
size_t EPBuffer<L>::remaining() {
  return this->tail - this->p;
}

int fl = 0;
int wfrc = 0;
int mc = 0;

template<size_t L>
void EPBuffer<L>::flush(uint8_t ep) {
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
void EPBuffer<L>::markComplete() {
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
int USBCore_::sendControl(uint8_t flags, const void* d, int len)
{
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
int USBCore_::send(uint8_t ep, const void* d, int len)
{
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
  this->epBufs[ep].flush(ep);
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
void USBCore_::transcSetup(usb_dev* usbd, uint8_t ep) {
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
      this->sendDeviceConfigDescriptor(usbd);
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
void USBCore_::transcOut(usb_dev* usbd, uint8_t ep) {
  this->epBufs[ep].markComplete();
  this->oldTranscOut(usbd, ep);
}

// Called in interrupt context.
void USBCore_::transcIn(usb_dev* usbd, uint8_t ep) {
  this->epBufs[ep].markComplete();
  this->oldTranscIn(usbd, ep);
}

void USBCore_::transcUnknown(usb_dev* usbd, uint8_t ep) {
  this->oldTranscUnknown(usbd, ep);
}

void USBCore_::sendDeviceConfigDescriptor(usb_dev* usbd)
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
