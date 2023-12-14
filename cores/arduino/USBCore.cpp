#ifdef USBCON
#include "USBCore.h"

extern "C" {
#include "gd32/usb.h"
#include "usbd_enum.h"
#include "usbd_lld_regs.h"
#include "usbd_pwr.h"
#include "usbd_transc.h"
}

#include <cassert>

// bMaxPower in Configuration Descriptor
#define USB_CONFIG_POWER_MA(mA)                ((mA)/2)
#ifndef USB_CONFIG_POWER
#define USB_CONFIG_POWER                      (500)
#endif

// TX timeout in milliseconds
#ifndef USBCORE_TIMEOUT
#define USBCORE_TIMEOUT 250
#endif

// TODO: make the device descriptor a member variable which can be
// overridden by subclasses.
static usb_desc_dev devDesc = {
    .header = {
        .bLength          = USB_DEV_DESC_LEN,
        .bDescriptorType  = USB_DESCTYPE_DEV
    },
    .bcdUSB                = 0x0200,
    .bDeviceClass          = 0xef,
    .bDeviceSubClass       = 0x02,
    .bDeviceProtocol       = 0x01,
    // TODO: this depends on what the mcu can support, but this is
    // device dependent code, so nevermind?
    .bMaxPacketSize0       = USB_EP_SIZE,
    .idVendor              = USB_VID,
    .idProduct             = USB_PID,
    .bcdDevice             = 0x0100,
    // Can set these to 0 so they’ll be ignored.
    .iManufacturer         = STR_IDX_MFC,
    .iProduct              = STR_IDX_PRODUCT,
    .iSerialNumber         = STR_IDX_SERIAL,
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
#ifdef USBD_IS_SELF_POWERED
#ifdef USBD_REMOTE_WAKEUP
    .bmAttributes = 0b11100000,
#else
    .bmAttributes = 0b11000000,
#endif // USBD_REMOTE_WAKEUP
#else
#ifdef USBD_REMOTE_WAKEUP
    .bmAttributes = 0b10100000,
#else
    .bmAttributes = 0b10000000,
#endif // USBD_REMOTE_WAKEUP
#endif // USBD_SELF_POWERED
    .bMaxPower = USB_CONFIG_POWER_MA(USB_CONFIG_POWER)
};

#pragma pack(1)
/* String descriptor with char16_t[], to use UTF-16 string literals */
typedef struct _usb_desc_utf16 {
    usb_desc_header header;
    char16_t unicode_string[];
} usb_desc_utf16;
#pragma pack()

/* Turn an ordinary string literal into a UTF-16 one */
#define XUSTR(s) u ## s
#define USTR(s) XUSTR(s)
#define USTRLEN(s) (2 * ((sizeof(s) - 1)))
#define U16DESC(s)                                                          \
    {                                                                       \
        .header = {                                                         \
            .bLength = sizeof(usb_desc_header) + USTRLEN(s),                \
            .bDescriptorType = USB_DESCTYPE_STR                             \
        },                                                                  \
        /* Can't use designated initializer here because G++ complains */   \
        USTR(s)                                                             \
    }

/* USB language ID Descriptor */
static usb_desc_LANGID usbd_language_id_desc =
{
    .header =
     {
         .bLength         = sizeof(usb_desc_LANGID),
         .bDescriptorType = USB_DESCTYPE_STR
     },
    .wLANGID              = ENG_LANGID
};

static usb_desc_utf16 mfcDesc = U16DESC(USB_MANUFACTURER);
static usb_desc_utf16 prodDesc = U16DESC(USB_PRODUCT);

/* USBD serial string */
static usb_desc_str serialDesc = {
    .header = {
        .bLength         = USB_STRING_LEN(12),
        .bDescriptorType = USB_DESCTYPE_STR,
    },
    .unicode_string = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}
};

static uint8_t* stringDescs[] = {
    [STR_IDX_LANGID]  = (uint8_t *)&usbd_language_id_desc,
    [STR_IDX_MFC]     = (uint8_t *)&mfcDesc,
    [STR_IDX_PRODUCT] = (uint8_t *)&prodDesc,
    [STR_IDX_SERIAL]  = (uint8_t *)&serialDesc
};

usb_desc desc = {
    .dev_desc    = (uint8_t *)&devDesc,
    .config_desc = nullptr,
    .bos_desc    = nullptr,
    .strings     = stringDescs
};

// Must be called with interrupts disabled
template<size_t L>
void EPBuffer<L>::init(uint8_t ep)
{
    this->ep = ep;
    this->reset();
    this->pendingFlush = false;
    this->rxWaiting = false;
    this->txWaiting = false;
    this->sendZLP = false;
}

template<size_t L>
size_t EPBuffer<L>::push(const void *d, size_t len)
{
    usb_disable_interrupts();
    size_t w = min(this->sendSpace(), len);
    const uint8_t* d8 = (const uint8_t*)d;
    for (size_t i = 0; i < w; i++) {
        *this->p++ = *d8++;
    }
    assert(this->p >= this->buf);
    if (this->sendSpace() == 0) {
        this->flush();
    }
    usb_enable_interrupts();
    return w;
}

template<size_t L>
size_t EPBuffer<L>::pop(void* d, size_t len)
{
    usb_disable_interrupts();
    size_t r = min(this->available(), len);
    uint8_t* d8 = (uint8_t*)d;
    for (size_t i = 0; i < r; i++) {
        *d8++ = *this->p++;
    }
    assert(this->p <= this->tail);

    if (this->available() == 0) {
        this->enableOutEndpoint();
    }
    usb_enable_interrupts();
    return r;
}

// Must be called with interrupts disabled
template<size_t L>
void EPBuffer<L>::reset()
{
    this->p = this->buf;
    this->tail = this->buf;
}

// Must be called with interrupts disabled
template<size_t L>
size_t EPBuffer<L>::len()
{
    assert(this->p >= this->buf);
    return this->p - this->buf;
}

// Must be called with interrupts disabled
template<size_t L>
size_t EPBuffer<L>::available()
{
    assert(this->p <= this->tail);
    return this->tail - this->p;
}

// Must be called with interrupts disabled
template<size_t L>
size_t EPBuffer<L>::sendSpace()
{
    if (this->pendingFlush) {
        // Report 0, to avoid consolidating packets
        return 0;
    } else {
        return L - this->len();
    }
}

// Must be called with interrupts disabled, or via ISR
template<size_t L>
void EPBuffer<L>::flush()
{
    assert(this->ep != 0);
    // Don't flush an empty buffer, unless sending a ZLP
    if (this->len() == 0 && !this->sendZLP) {
        return;
    }
    /*
     * Don't do anything if a flush is pending on the buffer. The ISR will call
     * us again once the queued transmission on the peripheral completes.
     */
    if (this->pendingFlush) {
        return;
    }
    USBCore().logEP('_', this->ep, '>', this->len());

    // Only attempt to send if the device is configured enough.
    switch (USBCore().usbDev().cur_status) {
    case USBD_DEFAULT:
    case USBD_ADDRESSED:
        break;
    case USBD_CONFIGURED:
    case USBD_SUSPENDED: {
        /*
         * If there's already a queued transmission on the peripheral, mark
         * the buffer as pending flush. The ISR will flush again once the
         * queued packet is sent.
         *
         * This implements software double buffering. The hardware only
         * supports double buffering on bulk or isochronous endpoints.
         */
        if (this->txWaiting) {
            this->pendingFlush = true;
            // Leave buffer pointers alone, to flush later
            return;
        } else {
            this->txWaiting = true;
            /*
             * If this packet is full, allow the next flush to send a ZLP.
             * This signals end of transmission to some hosts that might not
             * signal a completed read if the most recent packet is full.
             *
             * Some versions of Windows apparently need this.
             *
             * XXX This should check the declared endpoint wMaxPacketSize,
             * not the allocated buffer size, but so should some other stuff.
             *
             * XXX Theoretically, some applications might not want this
             * behavior, but the AVR core sends excess ZLPs instead, and this
             * doesn't seem to cause obvious problems.
             */
            if (this->len() == L) {
                this->sendZLP = true;
            } else {
                // Don't send multiple ZLPs in a row
                this->sendZLP = false;
            }
            usbd_ep_send(&USBCore().usbDev(), this->ep, (uint8_t *)this->buf, this->len());
            USBCore().logEP('>', this->ep, '>', this->len());
        }
        break;
    }
    default:
        break;
    }
    this->reset();
}

// Must be called with interrupts disabled
template<size_t L>
void EPBuffer<L>::enableOutEndpoint()
{
    // Don’t attempt to read from the endpoint buffer until it’s
    // ready.
    if (this->rxWaiting) return;
    this->rxWaiting = true;

    this->reset();
    usb_transc_config(&USBCore().usbDev().transc_out[this->ep],
                      (uint8_t*)this->buf, sizeof(this->buf), 0);
    USBCore().usbDev().drv_handler->ep_rx_enable(&USBCore().usbDev(), this->ep);
}

// Must be called via ISR
template<size_t L>
void EPBuffer<L>::transcOut()
{
    auto count = USBCore().usbDev().transc_out[this->ep].xfer_count;
    this->tail = this->buf + count;
    // Reset rxWaiting now so enableOutEndpoint works properly for ZLPs
    this->rxWaiting = false;
    if (count == 0) {
        /*
         * Got a ZLP; re-enable endpoint so we don't stop accepting input! If
         * the application is polling available() instead of calling recv(), it
         * would otherwise never execute pop(). This means that the endpoint
         * would remain disabled, and the driver would keep sending NAK to the
         * host when the host sends more data, causing an apparent lockup.
         *
         * Background: Hosts can send a ZLP if sending data that's exactly a
         * multiple of wMaxPacketSize (which is 64 bytes for full-speed USB).
         * At least some versions of macOS do this.
         */
        this->enableOutEndpoint();
    }
}

// Must be called via ISR
template<size_t L>
void EPBuffer<L>::transcIn()
{
    this->txWaiting = false;
    /*
     * If the buffer was waiting for a prior transmission to complete,
     * flush it now.
     */
    if (this->pendingFlush) {
        this->pendingFlush = false;
        this->flush();
    }
}

// Unused?
template<size_t L>
uint8_t* EPBuffer<L>::ptr()
{
    return this->buf;
}

template<size_t L, size_t C>
EPBuffers_<L, C>::EPBuffers_()
{
    this->init();
}

template<size_t L, size_t C>
void EPBuffers_<L, C>::init()
{
    for (uint8_t ep = 0; ep < C; ep++) {
        this->buf(ep).init(ep);
    }
}

template<size_t L, size_t C>
EPBuffer<L>& EPBuffers_<L, C>::buf(uint8_t ep)
{
    return this->epBufs[ep];
}

template<size_t L, size_t C>
EPDesc* EPBuffers_<L, C>::desc(uint8_t ep)
{
    assert(ep < C);
    static EPDesc descs[C];
    return &descs[ep];
}

EPBuffers_<USB_EP_SIZE, EP_COUNT>& EPBuffers()
{
    static EPBuffers_<USB_EP_SIZE, EP_COUNT> obj;
    return obj;
}

class ClassCore
{
    private:
        static arduino::USBSetup setup;
    public:
        static usb_class *structPtr()
        {
            static usb_class rc = {
                .req_cmd     = 0xff,
                .req_altset  = 0x0,
                .init        = ClassCore::init,
                .deinit      = ClassCore::deinit,
                .req_process = ClassCore::reqProcess,
                .ctlx_in     = ClassCore::ctlIn,
                .ctlx_out    = ClassCore::ctlOut,
                .data_in     = ClassCore::dataIn,
                .data_out    = ClassCore::dataOut
            };
            return &rc;
        }

        // Called after device configuration is set.
        static uint8_t init(usb_dev* usbd, uint8_t config_index)
        {
            (void)config_index;

            /*
             * Endpoint 0 is configured during startup, so skip it and only
             * handle what’s configured by ‘PluggableUSB’.
             */
            uint32_t buf_offset = EP0_RX_ADDR + USB_EP_SIZE;
            for (uint8_t ep = 1; ep < PluggableUSB().epCount(); ep++) {
                auto desc = *(EPDesc*)epBuffer(ep);
                usb_desc_ep ep_desc = {
                    .header = {
                        .bLength = sizeof(ep_desc),
                        .bDescriptorType = USB_DESCTYPE_EP,
                    },
                    .bEndpointAddress = (uint8_t)(desc.dir() | ep),
                    .bmAttributes = desc.type(),
                    .wMaxPacketSize = desc.maxlen(),
                    .bInterval = 0
                };
                // Don’t overflow the hardware buffer table.
                assert((buf_offset + ep_desc.wMaxPacketSize) <= 512);

                // Reinit EPBuffer, in case a packet got queued after reset
                // but before configuration
                EPBuffers().buf(ep).init(ep);
                usbd->ep_transc[ep][TRANSC_IN] = USBCore_::transcInHelper;
                usbd->ep_transc[ep][TRANSC_OUT] = USBCore_::transcOutHelper;
                usbd->drv_handler->ep_setup(usbd, EP_BUF_SNG, buf_offset, &ep_desc);

                /*
                 * Allow data to come in to OUT buffers immediately, as it
                 * will be copied out as it comes in.
                 */
                if (desc.dir() == 0) {
                    EPBuffers().buf(ep).enableOutEndpoint();
                }

                buf_offset += ep_desc.wMaxPacketSize;
            }
            return USBD_OK;
        }

        // Called when SetConfiguration setup packet sets the
        // configuration to 0.
        static uint8_t deinit(usb_dev* usbd, uint8_t config_index)
        {
            (void)usbd;
            (void)config_index;
            return USBD_OK;
        }

        // Called when ep0 gets a SETUP packet after configuration.
        static uint8_t reqProcess(usb_dev* usbd, usb_req* req)
        {
            USBCore().logStatus("ClassCore");
            (void)usbd;

            // Stash setup contents for later use by ctlOut
            memcpy(&setup, req, sizeof(setup));
            USBCore().setupClass(req->wLength);
            if (setup.bRequest == USB_GET_DESCRIPTOR) {
                auto sent = PluggableUSB().getDescriptor(setup);
                if (sent > 0) {
                    USBCore().flush(0);
                } else if (sent < 0) {
                    return REQ_NOTSUPP;
                }
            } else if ((setup.bmRequestType & USB_RECPTYPE_MASK) == USB_RECPTYPE_EP) {
                uint8_t ep = EP_ID(setup.wIndex);
                // Reset endpoint state on ClearFeature(EndpointHalt)
                EPBuffers().buf(ep).init(ep);
                return REQ_SUPP;
            } else if ((req->bmRequestType & USB_TRX_MASK) == USB_TRX_OUT && req->wLength != 0) {
                /*
                 * Don't call the class setup functions for control writes that
                 * have a data stage. Instead, defer them until we have
                 * received the data.
                 *
                 * The low-level firmware ISR expects the class driver to only
                 * validate the request header at this point, not to do a
                 * blocking receive of the data stage. Arduino code expects to
                 * be able to call recvControl from within the setup functions,
                 * so we give them what they're expecting by deferring the
                 * call.
                 *
                 * Unfortunately, this means that invalid requests aren't
                 * rejected until the status stage, instead of at the data
                 * stage.
                 */
                return USBCore().setupCtlOut(req);
            } else {
#ifdef USBD_USE_CDC
                if (CDCACM().setup(setup))
                    return REQ_SUPP;
#endif
                if (PluggableUSB().setup(setup)) {
                    return REQ_SUPP;
                }

                return REQ_NOTSUPP;
            }

            return REQ_SUPP;
        }

        // Called when ep0 is done sending all data from an IN stage.
        static uint8_t ctlIn(usb_dev* usbd)
        {
            (void)usbd;
            // CDCACM deferred action for 1200bps touch reboot
            CDCACM().ctlIn();
            return REQ_SUPP;
        }

        // Called when ep0 is done receiving all data from an OUT stage.
        static uint8_t ctlOut(usb_dev* usbd)
        {
            (void)usbd;
            /*
             * If the control request is a read or a zero-length write,
             * always return success, because this is probably coming from
             * a Status OUT stage.
             */
            if ((setup.bmRequestType & USB_TRX_MASK) == USB_TRX_IN || setup.wLength == 0) {
                return USBD_OK;
            }
            /*
             * These do the deferred request validation, so that recvControl
             * can read from an already-filled buffer.
             */
            if (CDCACM().setup(setup))
                return USBD_OK;
            if (PluggableUSB().setup(setup))
                return USBD_OK;

            return USBD_FAIL;
        }

        // Appears to be unused in usbd library, but used in usbfs.
        static void dataIn(usb_dev* usbd, uint8_t ep)
        {
            (void)usbd;
            (void)ep;
            return;
        }

        // Appears to be unused in usbd library, but used in usbfs.
        static void dataOut(usb_dev* usbd, uint8_t ep)
        {
            (void)usbd;
            (void)ep;
            return;
        }
};
arduino::USBSetup ClassCore::setup;

static void (*resetHook)();
void (*oldResetHandler)(usb_dev *usbd);
void handleReset(usb_dev *usbd)
{
    USBCore().logStatus("Reset");
    USBCore().setupClass(0);
    EPBuffers().init();
    USBCore().buildDeviceConfigDescriptor();
    if (resetHook) {
        resetHook();
    }
    oldResetHandler(usbd);
}

void USBCore_::setResetHook(void (*hook)())
{
    resetHook = hook;
}

void (*oldSuspendHandler)();
void handleSuspend()
{
    USBCore().logStatus("Suspend");
    oldSuspendHandler();
}

void (*oldResumeHandler)();
void handleResume()
{
    usb_disable_interrupts();
    USBCore().logStatus("Resume");
    usb_enable_interrupts();
    oldResumeHandler();
}

USBCore_::USBCore_()
{
#ifdef USBCORE_TRACE
    Serial1.begin(USBCORE_TRACE_SPEED);
#endif
    /*
     * Use global ‘usbd’ here, instead of wrapped version, to avoid
     * initialization loop.
     */
    usb_init(&desc, ClassCore::structPtr());
    usbd.user_data = this;

    oldResetHandler = usbd.drv_handler->ep_reset;
    usbd.drv_handler->ep_reset = handleReset;

    oldSuspendHandler = usbd.drv_handler->suspend;
    usbd.drv_handler->suspend = handleSuspend;

    oldResumeHandler = usbd.drv_handler->suspend_leave;
    usbd.drv_handler->suspend_leave = handleResume;

    this->oldTranscSetup = usbd.ep_transc[0][TRANSC_SETUP];
    usbd.ep_transc[0][TRANSC_SETUP] = USBCore_::transcSetupHelper;

    this->oldTranscOut = usbd.ep_transc[0][TRANSC_OUT];
    usbd.ep_transc[0][TRANSC_OUT] = USBCore_::transcOutHelper;

    this->oldTranscIn = usbd.ep_transc[0][TRANSC_IN];
    usbd.ep_transc[0][TRANSC_IN] = USBCore_::transcInHelper;
}

void USBCore_::logEP(char kind, uint8_t ep, char dir, size_t len)
{
#ifdef USBCORE_TRACE
    Serial1.print(USBD_EPxCS(ep), 16);
    Serial1.print(kind);
    Serial1.print(ep);
    Serial1.print(dir);
    Serial1.print(len);
    if (ep == 0) {
        usbd_ep_ram *btable_ep = (usbd_ep_ram *)(USBD_RAM + 2 * (BTABLE_OFFSET & 0xFFF8));
        auto rxcnt = &btable_ep[0].rx_count;
        Serial1.print(' ');
        Serial1.print(USBD_EPxCS(ep), 16);
        Serial1.print('(');
        Serial1.print(*rxcnt & EPRCNT_CNT);
        Serial1.print(')');
    }
    Serial1.println();
    // Serial1.flush();
#endif
}

void USBCore_::hexDump(char prefix, const uint8_t *buf, size_t len)
{
#ifdef USBCORE_TRACE
    Serial1.print(prefix);
    for (size_t i = 0; i < len; i++) {
        if (i != 0) {
            Serial1.print(' ');
        }
        Serial1.print(buf[i] >> 4, 16);
        Serial1.print((buf[i] & 0x0f), 16);
    }
    Serial1.println();
    // Serial1.flush();
#endif
}

void USBCore_::logStatus(const char *status)
{
#ifdef USBCORE_TRACE
    Serial1.println(status);
    // Serial1.flush();
#endif
}

void USBCore_::connect()
{
    usb_connect();
}

void USBCore_::disconnect()
{
    usb_disconnect();
}

void USBCore_::setupClass(uint16_t wLength)
{
    this->ctlIdx = 0;
    this->ctlOutLen = 0;
    this->maxWrite = wLength;
    auto usbd = &USBCore().usbDev();
    usb_transc_config(&usbd->transc_in[0], NULL, 0, 0);
    usb_transc_config(&usbd->transc_out[0], NULL, 0, 0);
}

// Send ‘len’ octets of ‘d’ through the control pipe (endpoint 0).
// Configures the low-level API's transfer buffer if TRANSFER_RELEASE
// is set, or when flushed.
//
// Limitations: There is a fixed maximum buffer size of USBCORE_CTL_BUFSZ,
// which must be adjusted per-application, in an attempt to avoid dynamic
// allocation.
//
// Returns the number of octets sent, or -1 on error.
//
// Must be called via ISR, or when the endpoint isn't in VALID status.
int USBCore_::sendControl(uint8_t flags, const void* data, int len)
{
    USBCore().logEP('+', 0, '>', len);

    uint8_t* d = (uint8_t*)data;
    auto usbd = &USBCore().usbDev();
    auto l = min(len, this->maxWrite);
    assert(l <= USBCORE_CTL_BUFSZ - this->ctlIdx);
    if (flags & TRANSFER_ZERO) {
        memset(&this->ctlBuf[this->ctlIdx], 0, l);
    } else {
        memcpy(&this->ctlBuf[this->ctlIdx], data, l);
    }
    ctlIdx += l;
    this->maxWrite -= l;
    if ((l != 0) && (flags & TRANSFER_RELEASE)) {
        USBCore().flush(0);
    }

    // Return ‘len’, rather than ‘wrote’, because PluggableUSB
    // calculates descriptor sizes by first having them write to an
    // empty buffer (setting ‘this->maxWrite’ to 0). To accomodate
    // that, we always just pretend we sent the entire buffer.
    //
    // TODO: this may cause issues when /actually/ sending buffers
    // larger than ‘this->maxWrite’, since we will have claimed to
    // send more data than we did.
    return len;
}

// Set up transaction for low-level firmware to copy control write contents
uint8_t USBCore_::setupCtlOut(usb_req* req)
{
    if (req->wLength > USBCORE_CTL_BUFSZ) {
        // Reject data that's larger than the static buffer
        return REQ_NOTSUPP;
    }
    this->ctlOutLen = req->wLength;
    usb_transc_config(&USBCore().usbDev().transc_out[0], this->ctlBuf, req->wLength, 0);
    // Allow all properly-sized Data OUT; defer req validation to ctlOut
    return REQ_SUPP;
}

// Copies control write data from the static buffer, after it's been received.
// Must be called via ISR
int USBCore_::recvControl(void* data, int len)
{
    len = min(len, this->ctlOutLen - this->ctlIdx);
    if (len == 0) {
        return 0;
    }
    memcpy(data, &this->ctlBuf[this->ctlIdx], len);
    this->ctlIdx += len;
    return len;
}

// TODO: no idea? this isn’t in the avr 1.8.2 library, although it has
// the function prototype.
int USBCore_::recvControlLong(void* data, int len)
{
    (void)data;
    (void)len;
    return -1;
}

// Number of octets available on OUT endpoint.
uint8_t USBCore_::available(uint8_t ep)
{
    usb_disable_interrupts();
    auto r =  EPBuffers().buf(ep).available();
    usb_enable_interrupts();
    return r;
}

// Space left in IN endpoint buffer.
uint8_t USBCore_::sendSpace(uint8_t ep)
{
    usb_disable_interrupts();
    auto r = EPBuffers().buf(ep).sendSpace();
    usb_enable_interrupts();
    return r;
}

// Blocking send of data to an endpoint. Returns the number of octets
// sent, or -1 on error.
int USBCore_::send(uint8_t ep, const void* data, int len)
{
    uint8_t* d = (uint8_t*)data;
    // Top nybble is used for flags.
    auto flags = ep & 0xf0;
    ep &= 0x7;
    if (ep == 0) {
        return -1;
    }
    auto wrote = 0;
    auto usbd = &USBCore().usbDev();

    // usb_disable_interrupts();
    // USBCore().logEP('+', ep, '>', len);
    // usb_enable_interrupts();
#ifdef USBD_REMOTE_WAKEUP
    usb_disable_interrupts();
    if (usbd->cur_status == USBD_SUSPENDED && usbd->pm.remote_wakeup) {
        USBCore().logStatus("Remote wakeup");
        usb_enable_interrupts();
        usbd_remote_wakeup_active(usbd);
    } else {
        usb_enable_interrupts();
    }
#endif

    uint32_t start = millis();
    // TODO: query the endpoint for its max packet length.
    while (wrote < len) {
        auto w = 0;
        auto toWrite = len - wrote;
        if (millis() - start > USBCORE_TIMEOUT) {
            usb_disable_interrupts();
            USBCore().logEP('X', ep, '>', len);
            usb_enable_interrupts();
            return -1;
        }
        if (flags & TRANSFER_ZERO) {
            // TODO: handle writing zeros instead of ‘d’.
            return -1;
        } else {
            w = EPBuffers().buf(ep).push(d, toWrite);
        }
        // push() can write less than requested, due to pending flushes, etc
        d += w;
        wrote += w;
    }

    if (flags & TRANSFER_RELEASE) {
        this->flush(ep);
    }

    return wrote;
}

// Non-blocking receive. Returns the number of octets read, or -1 on
// error.
int USBCore_::recv(uint8_t ep, void* data, int len)
{
    uint8_t* d = (uint8_t*)data;
    return EPBuffers().buf(ep).pop(d, len);
}

// Receive one octet from OUT endpoint ‘ep’. Returns -1 if no bytes
// available.
int USBCore_::recv(uint8_t ep)
{
    uint8_t c;
    auto rc = this->recv(ep, &c, sizeof(c));
    if (rc < 0) {
        return rc;
    } else if (rc == 0) {
        return -1;
    }
    return c;
}

// Flushes an outbound transmission as soon as possible.
int USBCore_::flush(uint8_t ep)
{
    if (ep == 0) {
        auto usbd = &USBCore().usbDev();
        usbd->transc_in[0].xfer_buf = ctlBuf;
        usbd->transc_in[0].xfer_len = ctlIdx;
        USBCore().logEP('_', 0, '>', ctlIdx);
        // USBCore().hexDump('>', ctlBuf, ctlIdx);
    } else {
        usb_disable_interrupts();
        EPBuffers().buf(ep).flush();
        usb_enable_interrupts();
    }
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

usb_dev& USBCore_::usbDev()
{
    return usbd;
}

/* Log the raw Setup stage data packet */
void USBCore_::transcSetup(usb_dev* usbd, uint8_t ep)
{
    USBCore().logEP(':', ep, '^', USB_SETUP_PACKET_LEN);
    USBCore().hexDump('^', (uint8_t *)&usbd->control.req, USB_SETUP_PACKET_LEN);

    this->oldTranscSetup(usbd, ep);
    USBCore().logEP('.', ep, '^', USB_SETUP_PACKET_LEN);
}

// Called in interrupt context.
void USBCore_::transcOut(usb_dev* usbd, uint8_t ep)
{
    auto transc = &usbd->transc_out[ep];
    auto count = transc->xfer_count;
    USBCore().logEP(':', ep, '<', count);
    if (ep == 0) {
        if (count != 0) {
            if (usbd->control.ctl_state == USBD_CTL_STATUS_OUT) {
                uint8_t buf[USBD_EP0_MAX_SIZE];
                uint8_t count = usbd->drv_handler->ep_read(buf, 0U, EP_BUF_SNG);
                USBCore().hexDump('!', buf, count);
            } else {
                USBCore().hexDump('<', ctlBuf, count);
            }
        }
        this->oldTranscOut(usbd, ep);
    } else {
        EPBuffers().buf(ep).transcOut();
    }
    USBCore().logEP('.', ep, '<', count);
}

// Called in interrupt context.
void USBCore_::transcIn(usb_dev* usbd, uint8_t ep)
{
    auto transc = &usbd->transc_in[ep];
    USBCore().logEP(':', ep, '>', transc->xfer_count);
    if (ep == 0) {
        this->oldTranscIn(usbd, ep);
    } else {
        EPBuffers().buf(ep).transcIn();
    }
    /*
     * Do NOT log if about to do a Status OUT, because that is very
     * timing-critical due to a possible hardware bug. If the Status
     * OUT interrupt isn't handled quickly enough, a following SETUP
     * packet could clobber it, even though the documentation says
     * it's not supposed to.
     */
    if (usbd->control.ctl_state != USBD_CTL_STATUS_OUT) {
        USBCore().logEP('.', ep, '>', transc->xfer_count);
    }
}

void USBCore_::buildDeviceConfigDescriptor()
{
    this->ctlIdx = 0;
    this->maxWrite = 0;
    uint8_t interfaceCount = 0;
    uint16_t len = 0;
#ifdef USBD_USE_CDC
    interfaceCount += 2;
    len += CDCACM().getInterface();
#endif
    len += PluggableUSB().getInterface(&interfaceCount);

    configDesc.wTotalLength = sizeof(configDesc) + len;
    configDesc.bNumInterfaces = interfaceCount;
    this->maxWrite = USBCORE_CTL_BUFSZ;
    this->ctlIdx = 0;
    this->sendControl(0, &configDesc, sizeof(configDesc));
    interfaceCount = 0;
#ifdef USBD_USE_CDC
    interfaceCount += 2;
    CDCACM().getInterface();
#endif
    PluggableUSB().getInterface(&interfaceCount);
    memcpy(this->cfgDesc, this->ctlBuf, this->ctlIdx);
    USBCore().usbDev().desc->config_desc = this->cfgDesc;
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
    return (void*)EPBuffers().desc(n);
}

bool USBCore_::isSuspended()
{
    return USBCore().usbDev().cur_status == USBD_SUSPENDED;
}

bool USBCore_::configured()
{
    return USBCore().usbDev().config != 0;
}
#endif
