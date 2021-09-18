#pragma once

#include "api/PluggableUSB.h"

#define EP_TYPE_INTERRUPT_IN 0

class TestPlug: public arduino::PluggableUSBModule {
 public:
  TestPlug();
  char* foo();

 protected:
  int getInterface(uint8_t* interfaceNum);
  int getDescriptor(arduino::USBSetup& setup);
  bool setup(arduino::USBSetup& setup);
  uint8_t getShortName(char* name);

 private:
  unsigned int epType[1];
};

extern TestPlug tp;
