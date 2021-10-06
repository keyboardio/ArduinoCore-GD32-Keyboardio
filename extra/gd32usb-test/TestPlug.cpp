#include "TestPlug.h"
#include "USBCore.h"

#include <cstring>

TestPlug tp;

TestPlug::TestPlug() : arduino::PluggableUSBModule(1, 1, epType) {
  PluggableUSB().plug(this);
  this->epType[0] = EPTYPE(EP_IN(this->pluggedEndpoint), USB_EP_ATTR_INT);
}

int TestPlug::getInterface(uint8_t* interfaceNum) {
  // We’re only using one interface.
  *interfaceNum++;

  const uint8_t desc[] = {
    // bLength, bDescriptorType
    9, 4,

    // bInterfaceNumber, bAlternateSetting, bNumEndpoints
    this->pluggedInterface, 0, 1,

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
    EP_IN(this->pluggedEndpoint), 0b00000011, 64, 0, 8,
  };
  return USB_SendControl(0, desc, sizeof(desc));
}

int TestPlug::getDescriptor(arduino::USBSetup& setup) {
  return 0;
}

bool TestPlug::setup(arduino::USBSetup& setup) {
  return false;
}

uint8_t TestPlug::getShortName(char* name) {
  const uint8_t nm[] = "test";
  memcpy(name, nm, sizeof(nm));
  return sizeof(nm);
}

char* TestPlug::foo() {
  static char rc[] = "TestPlug::foo()";
  return rc;
}
