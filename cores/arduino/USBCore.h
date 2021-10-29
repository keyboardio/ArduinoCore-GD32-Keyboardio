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

template<size_t L>
class EPBuffer {
public:
  size_t push(const void* d, size_t len);
  uint8_t* ptr();
  void reset();
  size_t len();
  size_t remaining();

private:
  uint8_t buf[L];
  uint8_t* tail = buf + sizeof(buf);
  uint8_t* p = buf;
};

class USBCore_ {
public:
  USBCore_();

  void connect();

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
  EPBuffer<USBD_EP0_MAX_SIZE> epBufs[EP_COUNT];
  // TODO: verify that this only applies to the control endpoint’s use of wLength
  // I think this is only on the setup packet, so it should be fine.
  uint16_t maxWrite = 0;

  /*
   * Pointers to the transaction routines specified by ‘usbd_init’.
   */
  void (*oldTranscSetup)(usb_dev* usbd, uint8_t ep);
  void (*oldTranscOut)(usb_dev* usbd, uint8_t ep);
  void (*oldTranscIn)(usb_dev* usbd, uint8_t ep);
  void (*oldTranscUnknown)(usb_dev* usbd, uint8_t ep);

  /*
   * Static member function helpers called from ISR.
   *
   * These pull the core handle from ‘usbd’ and use it to call the
   * instance member functions.
   */
  static void transcSetupHelper(usb_dev* usbd, uint8_t ep);
  static void transcOutHelper(usb_dev* usbd, uint8_t ep);
  static void transcInHelper(usb_dev* usbd, uint8_t ep);
  static void transcUnknownHelper(usb_dev* usbd, uint8_t ep);

  void transcSetup(usb_dev* usbd, uint8_t ep);
  void transcOut(usb_dev* usbd, uint8_t ep);
  void transcIn(usb_dev* usbd, uint8_t ep);
  void transcUnknown(usb_dev* usbd, uint8_t ep);

  void sendDeviceConfigDescriptor(usb_dev* usbd);
  void waitForDataReady(uint8_t ep);
  void clearDataReady(uint8_t ep);
  void waitForWriteComplete(uint8_t ep);
  void clearWriteComplete(uint8_t ep);

  void sendZLP(usb_dev* usbd, uint8_t ep);
};

USBCore_& USBCore();