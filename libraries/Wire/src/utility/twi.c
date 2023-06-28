/*
    Copyright (c) 2021, GigaDevice Semiconductor Inc.

    Redistribution and use in source and binary forms, with or without modification,
are permitted provided that the following conditions are met:

    1. Redistributions of source code must retain the above copyright notice, this
       list of conditions and the following disclaimer.
    2. Redistributions in binary form must reproduce the above copyright notice,
       this list of conditions and the following disclaimer in the documentation
       and/or other materials provided with the distribution.
    3. Neither the name of the copyright holder nor the names of its contributors
       may be used to endorse or promote products derived from this software without
       specific prior written permission.

    THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY
OF SUCH DAMAGE.

    Based on mbed-os\targets\TARGET_GigaDevice\TARGET_GD32F30X\i2c_api.c
*/

#include "utility/twi.h"
#include "pinmap.h"
#include "twi.h"


#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
#if defined(I2C0)
    I2C0_INDEX,
#endif
#if defined(I2C1)
    I2C1_INDEX,
#endif
    I2C_NUM
} internal_i2c_index_t;

static struct i2c_s *obj_s_buf[I2C_NUM] = {NULL};

/*
 * Timeouts assume -O0, 120MHz SYSCLK, 60MHz APB1, and 400kHz I2C bus
 */
/*
 * around 27.8usec, or 11 bit times at 400kHz.
 * You might want to increase it if you have multiple controllers.
 */
#ifndef WIRE_I2C_FLAG_TIMEOUT
#define WIRE_I2C_FLAG_TIMEOUT 72
#endif

/* around 1ms. Increase if your target does lots of clock stretching */
#ifndef WIRE_I2C_ACK_TIMEOUT
#define WIRE_I2C_ACK_TIMEOUT 2587
#endif

#ifndef WIRE_I2C_FLAG_TIMEOUT_START
#define WIRE_I2C_FLAG_TIMEOUT_START WIRE_I2C_FLAG_TIMEOUT
#endif

/* Because some targets stretch the clock after ACKing the last byte */
#ifndef WIRE_I2C_FLAG_TIMEOUT_STOP_BIT_RESET
#define WIRE_I2C_FLAG_TIMEOUT_STOP_BIT_RESET WIRE_I2C_ACK_TIMEOUT
#endif

#ifndef WIRE_I2C_FLAG_TIMEOUT_ADDR_ACK
#define WIRE_I2C_FLAG_TIMEOUT_ADDR_ACK WIRE_I2C_ACK_TIMEOUT
#endif

#ifndef WIRE_I2C_FLAG_TIMEOUT_DATA_ACK
#define WIRE_I2C_FLAG_TIMEOUT_DATA_ACK WIRE_I2C_ACK_TIMEOUT
#endif

#ifndef WIRE_I2C_FLAG_TIMEOUT_BYTE_TRANSMITTED
#define WIRE_I2C_FLAG_TIMEOUT_BYTE_TRANSMITTED WIRE_I2C_ACK_TIMEOUT
#endif

#ifndef WIRE_I2C_FLAG_TIMEOUT_BYTE_RECEIVED
#define WIRE_I2C_FLAG_TIMEOUT_BYTE_RECEIVED WIRE_I2C_ACK_TIMEOUT
#endif

#define I2C_S(obj)    (struct i2c_s *) (obj)

#if defined(GD32F1x0) || defined(GD32F3x0) || defined(GD32F4xx) || defined(GD32E23x)|| defined(GD32E50X)
#define GD32_I2C_FLAG_IS_TRANSMTR_OR_RECVR I2C_FLAG_TR
#else
#define GD32_I2C_FLAG_IS_TRANSMTR_OR_RECVR I2C_FLAG_TRS
#endif

/** Initialize the I2C peripheral
 *
 * @param obj       The I2C object
 * @param sda       The sda pin
 * @param scl       The scl pin
 * @param address   The I2C own address

 */
void i2c_init(i2c_t *obj, PinName sda, PinName scl, uint8_t address)
{
    struct i2c_s *obj_s = I2C_S(obj);
    uint32_t default_speed = 100000;
    /* find the I2C by pins */
    uint32_t i2c_sda = pinmap_peripheral(sda, PinMap_I2C_SDA);
    uint32_t i2c_scl = pinmap_peripheral(scl, PinMap_I2C_SCL);

    obj_s->sda = sda;
    obj_s->scl = scl;
    obj_s->i2c = pinmap_merge(i2c_sda, i2c_scl);

    switch (obj_s->i2c) {
        case I2C0:
            /* enable I2C0 clock and configure the pins of I2C0 */
            obj_s->index = 0;
            rcu_periph_clock_enable(RCU_I2C0);

            break;
        case I2C1:
            /* enable I2C1 clock and configure the pins of I2C1 */
            obj_s->index = 1;
            rcu_periph_clock_enable(RCU_I2C1);

            break;
        default:
            break;
    }

    /* configure the pins of I2C */
    pinmap_pinout(sda, PinMap_I2C_SDA);
    pinmap_pinout(scl, PinMap_I2C_SCL);

    /* I2C clock configure */
    i2c_clock_config(obj->i2c, default_speed, I2C_DTCY_2);

    /* I2C address configure */
    i2c_mode_addr_config(obj->i2c, I2C_I2CMODE_ENABLE, I2C_ADDFORMAT_7BITS, address);


    /* enable I2C */
    i2c_enable(obj->i2c);
    /* enable acknowledge */
    i2c_ack_config(obj->i2c, I2C_ACK_ENABLE);
    /* get obj_s_buf */
    obj_s_buf[obj_s->index] = obj_s;
}

/** Enable the I2C interrupt
 *
 * @param obj       The I2C object
 */
void i2c_slaves_interrupt_enable(i2c_t *obj)
{
    struct i2c_s *obj_s = I2C_S(obj);
    switch (obj_s->i2c) {
        case I2C0:
            /* enable I2C0 interrupt */
#if !defined(GD32E23x)
            nvic_irq_enable(I2C0_EV_IRQn, 1, 3);
            nvic_irq_enable(I2C0_ER_IRQn, 1, 2);
#else 
            nvic_irq_enable(I2C0_EV_IRQn, 1);
            nvic_irq_enable(I2C0_ER_IRQn, 1);
#endif           
            break;
        case I2C1:
            /* enable I2C1 interrupt */
#if !defined(GD32E23x)
            nvic_irq_enable(I2C1_EV_IRQn, 1, 3);
            nvic_irq_enable(I2C1_ER_IRQn, 1, 2);
#else 
            nvic_irq_enable(I2C1_EV_IRQn, 1);
            nvic_irq_enable(I2C1_ER_IRQn, 1);
#endif           
            break;
        default:
            break;
    }

    i2c_interrupt_enable(obj_s->i2c, I2C_INT_ERR);
    i2c_interrupt_enable(obj_s->i2c, I2C_INT_BUF);
    i2c_interrupt_enable(obj_s->i2c, I2C_INT_EV);
}

/** Wait for an I2C_STAT0 flag
 *
 * @param obj the I2C object
 * @param flag the I2C_STAT0 flag to wait for
 * @param timeout timeout
 *
 * @return I2C_OK on success, I2C_ERROR on error, I2C_TIMEOUT on timeout,
 * I2C_DATA_NACK on NACK
 */
static i2c_status_enum i2c_wait_flag(i2c_t *obj, uint32_t flag, uint32_t timeout)
{
    i2c_status_enum ret = I2C_ERROR;
    uint32_t stat0;
    do {
        stat0 = I2C_STAT0(obj->i2c);
        if (stat0 & (flag | I2C_STAT0_AERR | I2C_STAT0_LOSTARB | I2C_STAT0_BERR)) {
            break;
        }
    } while (--timeout != 0);
    /* Clear any error flags that were set */
    I2C_STAT0(obj->i2c) = stat0 & ~(I2C_STAT0_AERR | I2C_STAT0_LOSTARB | I2C_STAT0_BERR);
    if (stat0 & flag) {
        ret = I2C_OK;
    }
    if (stat0 & I2C_STAT0_AERR) {
        /* wait_addr will translate this */
        ret = I2C_NACK_DATA;
    }
    /* Hard errors take priority over NACK */
    if (stat0 & (I2C_STAT0_LOSTARB | I2C_STAT0_BERR)) {
        ret = I2C_ERROR;
    }
    if (0 == timeout) {
        ret = I2C_TIMEOUT;
    }
    return ret;
}

/** Write one byte (master)
 *
 * @param obj  The I2C object
 * @param data Byte to be written
 * @param last whether this is the last byte
 *
 * @return I2C_NACK_DATA if NACK was received, I2C_OK if ACK was received,
 * I2C_TIMEOUT for timeout
 */
i2c_status_enum i2c_byte_write(i2c_t *obj, int data, int last)
{
    struct i2c_s *obj_s = I2C_S(obj);

    I2C_DATA(obj_s->i2c) = (uint8_t)data;

    if (last) {
        return i2c_wait_flag(obj, I2C_STAT0_BTC, WIRE_I2C_FLAG_TIMEOUT_BYTE_TRANSMITTED);
    } else {
        return i2c_wait_flag(obj, I2C_STAT0_TBE, WIRE_I2C_FLAG_TIMEOUT_BYTE_TRANSMITTED);
    }
}

/** Send START command
 *
 * @param obj the I2C object
 *
 * @return I2C_OK on success, I2C_ERROR on error, I2C_BUSY on busy bus
 * timeout, I2C_TIMEOUT on timeout if we already own the bus
 */
static i2c_status_enum i2c_start(i2c_t *obj)
{
    i2c_status_enum ret = I2C_ERROR;
    uint32_t timeout, stat0, stat1;

    /* generate a START condition */
    i2c_start_on_bus(obj->i2c);

    /* ensure the i2c has been started successfully */
    timeout = WIRE_I2C_FLAG_TIMEOUT_START;
    do {
        stat0 = I2C_STAT0(obj->i2c);
        if (stat0 & (I2C_STAT0_LOSTARB | I2C_STAT0_BERR | I2C_STAT0_SBSEND)) {
            break;
        }
    } while (--timeout != 0);
    if (stat0 & I2C_STAT0_SBSEND) {
        /*
         * Don't clear any flags, because that disrupts the automatic
         * clearing of SBSEND
         */
        return I2C_OK;
    }
    /* Clear any error flags that were set */
    I2C_STAT0(obj->i2c) = stat0 & ~(I2C_STAT0_AERR | I2C_STAT0_LOSTARB | I2C_STAT0_BERR);
    if (stat0 & (I2C_STAT0_LOSTARB | I2C_STAT0_BERR)) {
        ret = I2C_ERROR;
    }
    if (0 == timeout) {
        stat1 = I2C_STAT1(obj->i2c);
        if ((stat1 & (I2C_STAT1_I2CBSY | I2C_STAT1_MASTER)) == I2C_STAT1_I2CBSY) {
            ret = I2C_BUSY;
        } else {
            /* If we own the bus, a target might be stretching the clock */
            ret = I2C_TIMEOUT;
        }
    }
    return ret;
}

/** Send STOP command
 *
 * @param obj The I2C object
 */
i2c_status_enum  i2c_stop(i2c_t *obj)
{
    struct i2c_s *obj_s = I2C_S(obj);
    bool own_bus = I2C_STAT1(obj_s->i2c) & I2C_STAT1_MASTER;

    /* generate a STOP condition */
    i2c_stop_on_bus(obj_s->i2c);
    /* If we don't own the bus (lost arbitration, etc), don't wait */
    if (!own_bus) {
        return I2C_OK;
    }

    /* wait for STOP bit reset with timeout */
    int timeout = WIRE_I2C_FLAG_TIMEOUT_STOP_BIT_RESET;
    while ((I2C_CTL0(obj_s->i2c) & I2C_CTL0_STOP)) {
        if ((timeout--) == 0) {
            return I2C_TIMEOUT;
        }
    }

    return I2C_OK;
}

/** Wait for address transmission
 *
 * Wait for the ACK or NACK after we sent the address
 *
 * @param obj The I2C object
 * @return I2C_OK on success, I2C_ERROR on error, I2C_NACK_ADDR on NACK, I2C_TIMEOUT on timeout
 */
static i2c_status_enum i2c_wait_addr(i2c_t *obj)
{
    i2c_status_enum ret = I2C_ERROR;

    ret = i2c_wait_flag(obj, I2C_STAT0_ADDSEND, WIRE_I2C_FLAG_TIMEOUT_ADDR_ACK);
    /* clear ADDSEND */
    i2c_flag_clear(obj->i2c, I2C_FLAG_ADDSEND);
    if (ret == I2C_NACK_DATA) {
        /* Translate NACK result */
        ret = I2C_NACK_ADDR;
    }
    return ret;
}

/** Write bytes at a given address
 *
 * @param obj     The I2C object
 * @param address 7-bit address (last bit is 0)
 * @param data    The buffer for sending
 * @param length  Number of bytes to write
 * @param stop    Stop to be generated after the transfer is done
 * @return Status
 */
i2c_status_enum i2c_master_transmit(i2c_t *obj, uint8_t address, uint8_t *data, uint16_t length,
                                    uint8_t stop)
{


    /* When size is 0, this is usually an I2C scan / ping to check if device is there and ready */
    if (length == 0) {
        return i2c_wait_standby_state(obj, address);
    }

    if (length > I2C_BUFFER_SIZE) {
        return I2C_DATA_TOO_LONG;
    }

    i2c_status_enum ret = I2C_OK;
    uint32_t timeout = 0;
    uint32_t count = 0;

    /* Don't wait on BUSY; peripheral does that before sending START */
    ret = i2c_start(obj);
    if (I2C_OK != ret) {
        return ret;
    }

    /* send slave address */
    i2c_master_addressing(obj->i2c, address, I2C_TRANSMITTER);

    /* wait until I2C_FLAG_ADDSEND flag is set */
    ret = i2c_wait_addr(obj);

    for (count = 0; count < length; count++) {
        if (I2C_OK != ret) {
            break;
        }
        ret = i2c_byte_write(obj, data[count], (count == length - 1));
    }
    /* if not sequential write, then send stop */
    if (stop) {
        i2c_stop(obj);
    }
    return ret;
}

/** Read one byte
 *
 * @param obj  The I2C object
 * @param last Acknowledge
 * @return The read byte
 */
static int i2c_byte_read(i2c_t *obj, int last)
{
    struct i2c_s *obj_s = I2C_S(obj);

    if (last) {
        /* disable acknowledge */
        i2c_ack_config(obj_s->i2c, I2C_ACK_DISABLE);
    } else {
        /* enable acknowledge */
        i2c_ack_config(obj_s->i2c, I2C_ACK_ENABLE);
    }

    /* wait until the byte is received */
    uint32_t timeout = WIRE_I2C_FLAG_TIMEOUT_BYTE_RECEIVED;
    while ((i2c_flag_get(obj_s->i2c, I2C_FLAG_RBNE)) == RESET) {
        if ((timeout--) == 0) {
            return -1;
        }
    }
    return (int)I2C_DATA(obj_s->i2c);
}

/** read bytes in master mode at a given address
 *
 * @param obj     The I2C object
 * @param address 7-bit address (last bit is 1)
 * @param data    The buffer for receiving
 * @param length  Number of bytes to read
 * @param stop    Stop to be generated after the transfer is done
 * @return status
 */
/*
 * This contains a workaround for a hardware erratum:
 *
 * If RBNE is set, but BTC is cleared, and the read of I2C_DATA is delayed
 * (e.g., by an interrupt) long enough that BTC becomes set, but I2C_STAT0
 * isn't read again again after BTC is set, the read of I2C_DATA won't reset
 * BTC.
 *
 * This means that BTC remains set, and the hardware copies the shift register
 * into I2C_DATA as usual, but the hardware stretches the clock until BTC is
 * cleared. This causes software to read an incorrect duplicate byte.
 *
 * Example: Byte I arrives. Hardware sets RBNE. Software reads I2C_STAT0. Byte
 * I+1 arrives. Hardware sets BTC and stretches clock. Software reads byte I
 * from I2C_DATA. Hardware copies byte I+1 to I2C_DATA, but does not clear BTC,
 * because I2C_STAT0 wasn't read after BTC was set. RBNE remains set, because
 * there is still valid data in I2C_DATA. In its next iteration, software reads
 * I2C_STAT0. Both RBNE and BTC remain set. Software reads byte I+1 from
 * I2C_DATA, causing hardware to clear both RBNE and BTC. This resumes the
 * clock, and hardware copies the shift register to I2C_DATA as usual.
 *
 * However, this copy apparently contains another copy of byte I+1, instead of
 * the expected I+2, because the hardware stretched the clock when BTC remained
 * set; therefore, it has not yet read byte I+2.
 */
i2c_status_enum i2c_master_receive(i2c_t *obj, uint8_t address, uint8_t *data, uint16_t length,
                                   int stop)
{
    i2c_status_enum ret = I2C_OK;
    uint32_t count = 0;

    if (1 == length) {
        /* Reset ACK control to current byte */
        i2c_ackpos_config(obj->i2c, I2C_ACKPOS_CURRENT);
        /* disable acknowledge */
        i2c_ack_config(obj->i2c, I2C_ACK_DISABLE);
        /* send a stop condition to I2C bus*/
    } else if (2 == length) {
        /* send a NACK for the next data byte which will be received into the shift register */
        i2c_ackpos_config(obj->i2c, I2C_ACKPOS_NEXT);
        /* disable acknowledge */
        i2c_ack_config(obj->i2c, I2C_ACK_DISABLE);
    } else {
        /* Reset ACK control to current byte */
        i2c_ackpos_config(obj->i2c, I2C_ACKPOS_CURRENT);
        /* enable acknowledge */
        i2c_ack_config(obj->i2c, I2C_ACK_ENABLE);
    }
    ret = i2c_start(obj);
    if (I2C_OK != ret) {
        return ret;
    }
    /* send slave address */
    i2c_master_addressing(obj->i2c, address, I2C_RECEIVER);
    ret = i2c_wait_addr(obj);

    for (count = 0; count < length; count++) {
        if (ret != I2C_OK) {
            break;
        }
        if ((uint32_t)length - count >= 2) {
            /* Wait for both data register and shift register to be full */
            /*
             * Always do this, to work around erratum mentioned above.
             * Yes, this delays reading of the first byte, and might slightly
             * increase clock stretching, but it avoids the erratum.
             */
            ret = i2c_wait_flag(obj, I2C_STAT0_BTC, WIRE_I2C_FLAG_TIMEOUT_DATA_ACK);
        } else {
            ret = i2c_wait_flag(obj, I2C_STAT0_RBNE, WIRE_I2C_FLAG_TIMEOUT_BYTE_RECEIVED);
        }
        if (length > 2 && count == (uint32_t)length - 3) {
            /* If 3 bytes remain, disable ACK before reading anything more */
            /* Lengths less than 3 had NACK configuration done earlier */
            i2c_ack_config(obj->i2c, I2C_ACK_DISABLE);
        }

        if (ret == I2C_OK) {
            data[count] = i2c_data_receive(obj->i2c);
        }
    }
    /* if not sequential read, then send stop */
    if (stop) {
        i2c_stop(obj);
    }
    return ret;
}

/** Checks if target device is ready for communication
 *
 * @param obj     The I2C object
 * @param address 7-bit address (last bit is 1)
 * @return status
 */
i2c_status_enum i2c_wait_standby_state(i2c_t *obj, uint8_t address)
{
    i2c_status_enum status = I2C_OK;

    /* Don't wait on BUSY; peripheral does that before sending START */
    status = i2c_start(obj);
    if (I2C_OK != status) {
        return status;
    }

    /* send slave address to I2C bus */
    i2c_master_addressing(obj->i2c, address, I2C_TRANSMITTER);
    status = i2c_wait_addr(obj);
    // On failure to send a stop, return the timeout
    if (i2c_stop(obj) != I2C_OK) {
        return I2C_TIMEOUT;
    }
    return status;
}

/** sets function called before a slave read operation
 *
 * @param obj      The I2C object
 * @param function Callback function to use
 */
void i2c_attach_slave_rx_callback(i2c_t *obj, void (*function)(uint8_t *, int))
{
    if (obj == NULL) {
        return;
    }
    /* Exit if a reception is already on-going */
    if (function == NULL) {
        return;
    }
    obj->slave_receive_callback = function;
}

/** sets function called before a slave write operation
 *
 * @param obj      The I2C object
 * @param function Callback function to use
 */
void i2c_attach_slave_tx_callback(i2c_t *obj, void (*function)(void))
{
    if (obj == NULL) {
        return;
    }
    /* Exit if a reception is already on-going */
    if (function == NULL) {
        return;
    }
    obj->slave_transmit_callback = function;
}

/** Write bytes to master
 *
 * @param obj    The I2C object
 * @param data   The buffer for transfer
 * @param length Number of bytes to read
 * @return status
 */
i2c_status_enum i2c_slave_write_buffer(i2c_t *obj, uint8_t *data, uint16_t length)
{
    struct i2c_s *obj_s = I2C_S(obj);
    uint8_t i = 0;
    i2c_status_enum ret = I2C_OK;

    if (length > I2C_BUFFER_SIZE) {
        ret = I2C_DATA_TOO_LONG;
    } else {
        /* check the communication status */
        for (i = 0; i < length; i++) {
            *obj_s->tx_buffer_ptr++ = *(data + i);
        }
        obj_s->tx_count = length;
        obj_s->tx_buffer_ptr = obj_s->tx_buffer_ptr - length;
    }
    return ret;
}


#ifdef I2C0
/** This function handles I2C interrupt handler
 *
 * @param i2c_periph The I2C peripheral
 */
static void i2c_irq(struct i2c_s *obj_s)
{
    if (i2c_interrupt_flag_get(I2C0, I2C_INT_FLAG_ADDSEND)) {
        /* clear the ADDSEND bit */
        i2c_interrupt_flag_clear(I2C0, I2C_INT_FLAG_ADDSEND);
        //memset(_rx_Buffer, _rx_count, 0);
        obj_s->rx_count = 0;
        if (i2c_flag_get(I2C0, GD32_I2C_FLAG_IS_TRANSMTR_OR_RECVR)) {
            obj_s->slave_transmit_callback();
        }
    } else if ((i2c_interrupt_flag_get(I2C0, I2C_INT_FLAG_TBE)) &&
               (!i2c_interrupt_flag_get(I2C0, I2C_INT_FLAG_AERR))) {
        /* Send a data byte */
        if (obj_s->tx_count > 0) {
            i2c_data_transmit(I2C0, *obj_s->tx_buffer_ptr++);
            obj_s->tx_count--;
        }
    } else if (i2c_interrupt_flag_get(I2C0, I2C_INT_FLAG_RBNE)) {
        /* if reception data register is not empty ,I2C1 will read a data from I2C_DATA */
        *obj_s->rx_buffer_ptr++ = i2c_data_receive(I2C0);
        obj_s->rx_count++;
    } else if (i2c_interrupt_flag_get(I2C0, I2C_INT_FLAG_STPDET)) {
        /* clear the STPDET bit */
        i2c_enable(I2C0);
        if (!i2c_flag_get(I2C0, GD32_I2C_FLAG_IS_TRANSMTR_OR_RECVR)) {
            obj_s->rx_buffer_ptr = obj_s->rx_buffer_ptr - obj_s->rx_count ;
            obj_s->slave_receive_callback(obj_s->rx_buffer_ptr, obj_s->rx_count);
        }
    }
}

/** Handle I2C0 event interrupt request
 *
 */
void I2C0_EV_IRQHandler(void)
{
    i2c_irq(obj_s_buf[I2C0_INDEX]);
}

/** handle I2C0 error interrupt request
 *
 */
void I2C0_ER_IRQHandler(void)
{
    /* no acknowledge received */
    if (i2c_interrupt_flag_get(I2C0, I2C_INT_FLAG_AERR)) {
        i2c_interrupt_flag_clear(I2C0, I2C_INT_FLAG_AERR);
    }

    /* SMBus alert */
    if (i2c_interrupt_flag_get(I2C0, I2C_INT_FLAG_SMBALT)) {
        i2c_interrupt_flag_clear(I2C0, I2C_INT_FLAG_SMBALT);
    }

    /* bus timeout in SMBus mode */
    if (i2c_interrupt_flag_get(I2C0, I2C_INT_FLAG_SMBTO)) {
        i2c_interrupt_flag_clear(I2C0, I2C_INT_FLAG_SMBTO);
    }

    /* over-run or under-run when SCL stretch is disabled */
    if (i2c_interrupt_flag_get(I2C0, I2C_INT_FLAG_OUERR)) {
        i2c_interrupt_flag_clear(I2C0, I2C_INT_FLAG_OUERR);
    }

    /* arbitration lost */
    if (i2c_interrupt_flag_get(I2C0, I2C_INT_FLAG_LOSTARB)) {
        i2c_interrupt_flag_clear(I2C0, I2C_INT_FLAG_LOSTARB);
    }

    /* bus error */
    if (i2c_interrupt_flag_get(I2C0, I2C_INT_FLAG_BERR)) {
        i2c_interrupt_flag_clear(I2C0, I2C_INT_FLAG_BERR);
    }

    /* CRC value doesn't match */
    if (i2c_interrupt_flag_get(I2C0, I2C_INT_FLAG_PECERR)) {
        i2c_interrupt_flag_clear(I2C0, I2C_INT_FLAG_PECERR);
    }
}
#endif

#ifdef I2C1

/** Handle I2C1 event interrupt request
 *
 */
void I2C1_EV_IRQHandler(void)
{
    i2c_irq(obj_s_buf[I2C1_INDEX]);
}

/** handle I2C1 error interrupt request
 *
 */
void I2C1_ER_IRQHandler(void)
{
    /* no acknowledge received */
    if (i2c_interrupt_flag_get(I2C1, I2C_INT_FLAG_AERR)) {
        i2c_interrupt_flag_clear(I2C1, I2C_INT_FLAG_AERR);
    }

    /* SMBus alert */
    if (i2c_interrupt_flag_get(I2C1, I2C_INT_FLAG_SMBALT)) {
        i2c_interrupt_flag_clear(I2C1, I2C_INT_FLAG_SMBALT);
    }

    /* bus timeout in SMBus mode */
    if (i2c_interrupt_flag_get(I2C1, I2C_INT_FLAG_SMBTO)) {
        i2c_interrupt_flag_clear(I2C1, I2C_INT_FLAG_SMBTO);
    }

    /* over-run or under-run when SCL stretch is disabled */
    if (i2c_interrupt_flag_get(I2C1, I2C_INT_FLAG_OUERR)) {
        i2c_interrupt_flag_clear(I2C1, I2C_INT_FLAG_OUERR);
    }

    /* arbitration lost */
    if (i2c_interrupt_flag_get(I2C1, I2C_INT_FLAG_LOSTARB)) {
        i2c_interrupt_flag_clear(I2C1, I2C_INT_FLAG_LOSTARB);
    }

    /* bus error */
    if (i2c_interrupt_flag_get(I2C1, I2C_INT_FLAG_BERR)) {
        i2c_interrupt_flag_clear(I2C1, I2C_INT_FLAG_BERR);
    }

    /* CRC value doesn't match */
    if (i2c_interrupt_flag_get(I2C1, I2C_INT_FLAG_PECERR)) {
        i2c_interrupt_flag_clear(I2C1, I2C_INT_FLAG_PECERR);
    }
}

void i2c_set_clock(i2c_t *obj, uint32_t clock_hz)
{
    i2c_clock_config(obj->i2c, clock_hz, I2C_DTCY_2);
}

#endif
#ifdef __cplusplus
}
#endif
