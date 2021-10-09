#include "CDCACM.h"
#include "USBCore.h"

CDCACM cdcacm;

/* class-specific notification codes for PSTN subclasses */
#define USB_CDC_NOTIFY_SERIAL_STATE 0x20

#define SEND_ENCAPSULATED_COMMAND 0x00
#define GET_ENCAPSULATED_RESPONSE 0x01
#define SET_COMM_FEATURE          0x02
#define GET_COMM_FEATURE          0x03
#define CLEAR_COMM_FEATURE        0x04
#define SET_LINE_CODING           0x20
#define GET_LINE_CODING           0x21
#define SET_CONTROL_LINE_STATE    0x22
#define SEND_BREAK                0x23
#define NO_CMD                    0xFF

CDCACM::CDCACM() : arduino::PluggableUSBModule(2, 3, epType) {
  this->epType[0] = EPTYPE(USB_TRX_IN, USB_EP_ATTR_INT);
  this->epType[1] = EPTYPE(USB_TRX_OUT, USB_EP_ATTR_BULK);
  this->epType[2] = EPTYPE(USB_TRX_IN, USB_EP_ATTR_BULK);
  PluggableUSB().plug(this);
}

int CDCACM::getInterface(uint8_t* interfaceCount)
{
  *interfaceCount += 2;

  const uint8_t desc[] = {
    /*
     * Command interface
     */
    // bLength, bDescriptorType
    9, 4,

    // bInterfaceNumber, bAlternateSettuing, bNumEndpoints
    this->pluggedInterface, 0, 1,

    // bInterfaceClass, bInterfaceSubClass, bInterfaceProtocol
    2, 2, 1,

    // iInterface
    0,

    /*
     * CDC header
     */
    // bLength, bDescriptorType
    5, 0x24,

    // bDescriptorSubtype, bcdCDC
    0, 0x10, 0x01,

    /*
     * CDC call management
     */
    // bLength, bDescriptorType
    5, 0x24,

    // bDescriptorSubtype, bmCapabilities, bDataInterface,
    1, 0, this->pluggedInterface+1,

    /*
     * CDC ACM
     */
    // bLength, bDescriptorType
    4, 0x24,

    // bDescriptorSubtype, bmCapabilities
    2, 2,

    /*
     * CDC Union
     */
    // bLength, bDescriptorType
    5, 0x24,

    // bDescriptorSubtype, bMasterInterface, bSlaveInterface0,
    6, this->pluggedInterface, this->pluggedInterface+1,

    /*
     * Endpoint 1 - control
     */
    // bLength, bDescriptorType
    7, 5,

    // bEndpointAddress, bmAttributes, wMaxPacketSize, bInterval
    EPTYPE_DIR(this->epType[0]) | this->pluggedEndpoint, EPTYPE_TYPE(this->epType[0]), 64, 0, 8,

    /*
     * CDC data interface
     */
    // bLength, bDescriptorType
    9, 4,

    // bInterfaceNumber, bAlternateSettuing, bNumEndpoints
    this->pluggedInterface+1, 0, 2,

    // bInterfaceClass, bInterfaceSubClass, bInterfaceProtocol
    10, 0, 0,

    // iInterface
    0,

    /*
     * Endpoint 2 - data out
     */
    // bLength, bDescriptorType
    7, 5,

    // bEndpointAddress, bmAttributes, wMaxPacketSize, bInterval
    EPTYPE_DIR(this->epType[1]) | this->pluggedEndpoint+1, EPTYPE_TYPE(this->epType[1]), 64, 0, 0,

    /*
     * Endpoint 3 - data in
     */
    // bLength, bDescriptorType
    7, 5,

    // bEndpointAddress, bmAttributes, wMaxPacketSize, bInterval
    EPTYPE_DIR(this->epType[2]) | this->pluggedEndpoint+2, EPTYPE_TYPE(this->epType[2]), 64, 0, 0,
  };
  return USB_SendControl(0, &desc, sizeof(desc));
}

int CDCACM::getDescriptor(arduino::USBSetup& setup)
{
}

bool CDCACM::setup(arduino::USBSetup& setup)
{
  acmLineCoding lineCoding;
  if (this->pluggedInterface != setup.wIndex) {
    return false;
  }

  uint8_t notificationBuf[10];
  acmNotification* notification = (acmNotification*)notificationBuf;

  switch (setup.bRequest) {
  case SEND_ENCAPSULATED_COMMAND:
    break;

  case GET_ENCAPSULATED_RESPONSE:
    break;

  case SET_COMM_FEATURE:
    break;

  case GET_COMM_FEATURE:
    break;

  case CLEAR_COMM_FEATURE:
    break;

  case SET_LINE_CODING:
    if (setup.wLength == sizeof(this->lineCoding)) {
      USB_RecvControl(&this->lineCoding, sizeof(this->lineCoding));
      return true;
    }
    break;

  case GET_LINE_CODING:
    USB_SendControl(0, &this->lineCoding, sizeof(this->lineCoding));
    return true;

  case SET_CONTROL_LINE_STATE:
    notification->bmRequestType = 0xa1;
    notification->bNotification = USB_CDC_NOTIFY_SERIAL_STATE;
    notification->wIndex = 0;
    notification->wValue = 0;
    notification->wLength = 2;
    notificationBuf[8] = setup.wValueL & 3;
    notificationBuf[9] = 0;

    return true;

  case SEND_BREAK:
    break;

  default:
    break;
  }

  return false;
}
