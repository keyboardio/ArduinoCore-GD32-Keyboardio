#pragma once

#include "api/PluggableUSB.h"

typedef struct {
    uint32_t dwDTERate;                   /*!< data terminal rate */
    uint8_t  bCharFormat;                 /*!< stop bits */
    uint8_t  bParityType;                 /*!< parity */
    uint8_t  bDataBits;                   /*!< data bits */
} acmLineCoding;

typedef struct {
    uint8_t bmRequestType;                /*!< type of request */
    uint8_t bNotification;                /*!< communication interface class notifications */
    uint16_t wValue;                      /*!< value of notification */
    uint16_t wIndex;                      /*!< index of interface */
    uint16_t wLength;                     /*!< length of notification data */
} acmNotification;

class CDCACM: public arduino::PluggableUSBModule {
public:
  CDCACM();

protected:
  int getInterface(uint8_t* interfaceNum);
  int getDescriptor(arduino::USBSetup& setup);
  bool setup(arduino::USBSetup& setup);

private:
  unsigned int epType[3];
  acmLineCoding lineCoding;
};

extern CDCACM cdcacm;
