#pragma once

#include "Arduino.h"
#include "USBDefs.h"

extern "C" {
#include "usbd_core.h"
#include "usb_ch9_std.h"
}

/*
 * Macros for encoding the endpoint into a 16-bit integer containing
 * the endpoint’s direction, and type.
 */
#define EPTYPE(dir, type)                  EPTYPEANDMAXLEN(dir, type, USB_EP_SIZE)
#define EPTYPEANDMAXLEN(dir, type, maxlen) ((uint16_t)(((dir | (type & 0x3)) << 8) | 0x40))
#define EPTYPE_DIR(eptype)                 ((uint8_t)((eptype >> 8) & 0xfc))
#define EPTYPE_TYPE(eptype)                ((uint8_t)((eptype >> 8) & 0x3))
#define EPTYPE_MAXLEN(eptype)              ((uint8_t)(eptype & 0xff))

// DIR & TYPE << 8 | MAXLEN

/*
 * Mappings from Arduino USB API to USBCore singleton functions.
 */
#define TRANSFER_PGM     0x80
#define TRANSFER_ZERO    0x20
#define TRANSFER_RELEASE 0x40

#define USB_SendControl     USBCore().sendControl
#define USB_RecvControl     USBCore().recvControl
#define USB_RecvControlLong USBCore().recvControlLong
#define USB_Available       USBCore().available
#define USB_SendSpace       USBCore().sendSpace
#define USB_Send            USBCore().send
#define USB_Recv            USBCore().recv
#define USB_Flush           USBCore().flush

template<size_t L>
class EPBuffer
{
    public:
        void init(uint8_t ep);

        size_t push(const void* d, size_t len);
        size_t pop(void* d, size_t len);
        void reset();
        size_t len();
        size_t available();
        size_t sendSpace();
        void flush();
        void fetch();
        void markComplete();
        uint8_t* ptr();
        void enableOutEndpoint();

        void transcIn();
        void transcOut();

    private:
        void waitForDataReady();
        void waitForWriteComplete();

        uint8_t ep;
        uint8_t buf[L];
        uint8_t* tail = buf;
        uint8_t* p = buf;

        // TODO: this should probably be explicitly atomic.
        volatile bool txWaiting = false;

        // TODO: remove this debug stuff
        uint32_t flCount = 0;
        uint32_t mcCount = 0;
        uint32_t wfwcCount = 0;
};

template<size_t L, size_t C>
class EPBuffers_
{
    public:
        EPBuffers_();

        EPBuffer<L>& buf(uint8_t ep);
        void markComplete(uint8_t ep);

    private:
        EPBuffer<L> epBufs[C];
};

EPBuffers_<USBD_EP0_MAX_SIZE, EP_COUNT>& EPBuffers();

class USBCore_
{
    public:
        USBCore_();

        void connect();

        /*
         * PluggableUSB interface.
         */
        int sendControl(uint8_t flags, const void* data, int len);
        int recvControl(void* data, int len);
        int recvControlLong(void* data, int len);
        uint8_t available(uint8_t ep);
        uint8_t sendSpace(uint8_t ep);
        int send(uint8_t ep, const void* data, int len);
        int recv(uint8_t ep, void* data, int len);
        int recv(uint8_t ep);
        int flush(uint8_t ep);

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

    private:
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

        void transcSetup(usb_dev* usbd, uint8_t ep);
        void transcOut(usb_dev* usbd, uint8_t ep);
        void transcIn(usb_dev* usbd, uint8_t ep);
        void transcUnknown(usb_dev* usbd, uint8_t ep);

        void sendDeviceConfigDescriptor();
        void sendDeviceStringDescriptor();

        void sendStringDesc(const char *str);

        void sendZLP(usb_dev* usbd, uint8_t ep);
};

USBCore_& USBCore();
