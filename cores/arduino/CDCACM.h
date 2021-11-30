#pragma once

#include "api/ArduinoAPI.h"
#include "USBDefs.h"

/*
 * TODO: abstract the interfaces/endpoints
 */
#define CDC_FIRST_ENDPOINT 1
#define CDC_ENDPOINT_ACM (CDC_FIRST_ENDPOINT)
#define CDC_ENDPOINT_OUT (CDC_FIRST_ENDPOINT+1)
#define CDC_ENDPOINT_IN (CDC_FIRST_ENDPOINT+2)

#define CDC_COMMUNICATION_INTERFACE_CLASS 0x02
#define CDC_CALL_MANAGEMENT               0x01
#define CDC_ABSTRACT_CONTROL_MODEL        0x02
#define CDC_HEADER                        0x00
#define CDC_ABSTRACT_CONTROL_MANAGEMENT   0x02
#define CDC_UNION                         0x06
#define CDC_CS_INTERFACE                  0x24
#define CDC_CS_ENDPOINT                   0x25
#define CDC_DATA_INTERFACE_CLASS          0x0a

#define CDC_SET_LINE_CODING        0x20
#define CDC_GET_LINE_CODING        0x21
#define CDC_SET_CONTROL_LINE_STATE 0x22
#define CDC_SEND_BREAK             0x23

#define D_CDCCS(_subtype,_d0,_d1) { 5, 0x24, _subtype, _d0, _d1 }
#define D_CDCCS4(_subtype,_d0)    { 4, 0x24, _subtype, _d0 }

#pragma pack(push, 1)
typedef struct {
    uint8_t len;
    uint8_t dtype;
    uint8_t subtype;
    uint8_t d0;
    uint8_t d1;
} CDCCSInterfaceDescriptor;

typedef struct {
    uint8_t len;
    uint8_t dtype;
    uint8_t subtype;
    uint8_t bmCapabilities;
    uint8_t bDataInterface;
} CMFunctionalDescriptor;

typedef struct {
    uint8_t len;
    uint8_t dtype;
    uint8_t subtype;
    uint8_t bmCapabilities;
} ACMFunctionalDescriptor;

typedef struct {
    // IAD
    IADDescriptor iad;

    // Control
    InterfaceDescriptor cif;
    CDCCSInterfaceDescriptor header;
    CMFunctionalDescriptor callManagement;
    ACMFunctionalDescriptor controlManagement;
    CDCCSInterfaceDescriptor functionalDescriptor;
    EndpointDescriptor cifin;

    // Data
    InterfaceDescriptor dif;
    EndpointDescriptor in;
    EndpointDescriptor out;
} CDCDescriptor;

typedef struct
{
    uint32_t dwDTERate;
    uint8_t bCharFormat;
    uint8_t bParityType;
    uint8_t bDataBits;
} LineCoding;

#pragma pack(pop)

class CDCACM_ : public Stream {
public:
    CDCACM_(uint8_t firstInterface, uint8_t firstEndpoint);

    bool setup(arduino::USBSetup& setup);
    int getInterface();

    void begin(uint32_t baud);
    void begin(uint32_t baud, uint8_t config);
    void end();

    operator bool();

    int available();
    int peek();
    int read();
    int availableForWrite();
    size_t write(uint8_t c);
    size_t write(const uint8_t* d, size_t len);
    void flush();
    using Print::write;

private:
    uint8_t acmInterface;
    uint8_t dataInterface;
    uint8_t acmEndpoint;
    uint8_t outEndpoint;
    uint8_t inEndpoint;

    volatile LineCoding lineCoding = { 57600, 0x00, 0x00, 0x00 };
    uint8_t lineState = 0;
    volatile int32_t breakValue = -1;

    // We only store one octet, but up to 16 bits to have a flag that
    // specifies whether or not a character has been read.
    volatile int16_t peekBuffer = -1;
};

#ifdef USE_CDC_SERIAL
#define Serial CDCACM()
extern CDCACM_& CDCACM();
#endif
