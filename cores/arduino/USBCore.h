#pragma once

#include "api/PluggableUSB.h"
extern "C" {
#include "usbd_core.h"
#include "usb_ch9_std.h"
}

/*
 * Macros for encoding the endpoint into a 16-bit integer containing
 * the endpoint’s direction, and type.
 */
#define EPTYPE(dir, type) ((dir << 8) | type)
#define EPTYPE_DIR(eptype) (eptype >> 8)
#define EPTYPE_TYPE(eptype) (eptype & 0xff)

/*
 * Mappings from Arduino USB API to USBCore singleton functions.
 */
#define USB_SendControl USBCore().sendControl
#define USB_RecvControl USBCore().recvControl
#define USB_RecvControlLong USBCore().recvControlLong
#define USB_Available USBCore().available
#define USB_SendSpace USBCore().sendSpace
#define USB_Send USBCore().send
#define USB_Recv USBCore().recv
#define USB_Flush USBCore().flush

class PacketBuf {
public:
  void init(uint8_t ep) { this->ep = ep; };
  void push(uint8_t d);
  uint8_t* data() { return buf; };
  int length() { return this->p - this->buf; };
  int remaining() { return sizeof(this->buf) - this->length(); };

private:
  uint8_t ep;
  uint8_t buf[USBD_EP0_MAX_SIZE];
  uint8_t* tail = buf + sizeof(buf);
  uint8_t* p = buf;
};

class NoOpPacketBuf: PacketBuf {
public:
  void push(uint8_t d) { len++; };
  uint8_t* data() { return NULL; };
  int length() { return this->len; };
  int remaining() { return 0; };

private:
  int len = 0;
};

class USBCore_ {
public:
  USBCore_();

  void init();

  int sendControl(uint8_t flags, const void* d, int len);
  int recvControl(void* d, int len);
  int recvControlLong(void* d, int len);
  uint8_t available(uint8_t ep);
  uint8_t sendSpace(uint8_t ep);
  int send(uint8_t ep, const void* d, int len);
  int recv(uint8_t ep, void* d, int len);
  int recv(uint8_t ep);
  int flush(uint8_t ep);

private:
  uint8_t buf[USBD_EP0_MAX_SIZE];
  uint8_t* tail = buf + sizeof(buf);
  uint8_t* p = buf;
  // TODO: verify that this only applies to the control endpoint’s use of wLength
  // I think this is only on the setup packet, so it should be fine.
  uint16_t maxWrite = 0;

  /*
   * Pointers to the transaction routines specified by ‘usbd_init’.
   */
  void (*old_transc_setup)(usb_dev* usbd, uint8_t ep);
  void (*old_transc_out)(usb_dev* usbd, uint8_t ep);
  void (*old_transc_in)(usb_dev* usbd, uint8_t ep);
  void (*old_transc_unknown)(usb_dev* usbd, uint8_t ep);

  /*
   * Static member function helpers called from ISR.
   *
   * These pull the core handle from ‘usbd’ and use it to call the
   * instance member functions.
   */
  static void _transc_setup(usb_dev* usbd, uint8_t ep);
  static void _transc_out(usb_dev* usbd, uint8_t ep);
  static void _transc_in(usb_dev* usbd, uint8_t ep);
  static void _transc_unknown(usb_dev* usbd, uint8_t ep);

  void transc_setup(usb_dev* usbd, uint8_t ep);
  void transc_out(usb_dev* usbd, uint8_t ep);
  void transc_in(usb_dev* usbd, uint8_t ep);
  void transc_unknown(usb_dev* usbd, uint8_t ep);

  void sendDeviceConfigDescriptor(usb_dev* usbd);

  void sendZLP(usb_dev* usbd, uint8_t ep);
};

USBCore_& USBCore();
