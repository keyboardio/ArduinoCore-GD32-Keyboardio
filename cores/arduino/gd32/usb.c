#include "usb.h"

usb_dev usbd;

static void rcu_config()
{
	uint32_t system_clock = rcu_clock_freq_get(CK_SYS);

	/* enable USB pull-up pin clock */
	rcu_periph_clock_enable(RCC_AHBPeriph_GPIO_PULLUP);

	if (48000000U == system_clock) {
		rcu_usb_clock_config(RCU_CKUSB_CKPLL_DIV1);
	} else if (72000000U == system_clock) {
		rcu_usb_clock_config(RCU_CKUSB_CKPLL_DIV1_5);
	} else if (96000000U == system_clock) {
		rcu_usb_clock_config(RCU_CKUSB_CKPLL_DIV2);
	} else if (120000000U == system_clock) {
		rcu_usb_clock_config(RCU_CKUSB_CKPLL_DIV2_5);
	} else {
		/* TODO: panic if the clock doesn’t match assertions. */
	}

	/* enable USB APB1 clock */
	rcu_periph_clock_enable(RCU_USBD);
}

static void gpio_config()
{
	/* configure usb pull-up pin */
	gpio_init(USB_PULLUP, GPIO_MODE_OUT_PP, GPIO_OSPEED_50MHZ, USB_PULLUP_PIN);
}

static void nvic_config()
{
	/* 2 bits for preemption priority, 2 bits for subpriority */
	nvic_priority_group_set(NVIC_PRIGROUP_PRE2_SUB2);

	/* enable the USB low priority interrupt */
	nvic_irq_enable((uint8_t)USBD_LP_CAN0_RX0_IRQn, 2U, 0U);

	/* enable the USB low priority interrupt */
	nvic_irq_enable((uint8_t)USBD_HP_CAN0_TX_IRQn, 1U, 0U);

	/* enable the USB wakeup interrupt */
	nvic_irq_enable((uint8_t)USBD_WKUP_IRQn, 1U, 0U);
}

void usb_init(usb_desc* desc, usb_class* class_core)
{
	rcu_config();
	gpio_config();

	// For PluggableUSB, ‘hid_desc’ needs to be replaced with the
	// configuration from PluggableUSB. ‘hid_class’ is a series of
	// callbacks.

	// cf file:~/Arduino/hardware/dev/ArduinoCore-GD32/system/GD32F30x_firmware/GD32F30x_usbd_library/class/device/hid/Source/standard_hid_core.c::usb_class hid_class = {
	usbd_init(&usbd, desc, class_core);

	/*
	 * PluggableUSB uses USB_SendControl to add descriptors to an
	 * in-progress buffer, which will only get sent when all
	 * interfaces are accounted for.
	 */

	/*
	 * Keep a wMaxPacketSize buffer for each endpoint and flush it
	 * when its full.
	 *
	 * ISR will need to call into custom routines which check
	 * PluggableUSB for configuration data. Doesn’t look like
	 * usbd_init is going to work as is.
	 *
	 * Instead, maybe call it with NULLs as the last two
	 * arguments, then, since usbd_isr() doesn’t use those
	 * structures directly, we can modify the
	 * transc_out/transc_in,ep_transc tables?
	 *
	 * May be more trouble than it’s worth, and just write our own
	 * isr, taking on the risks that entails.
	 */
}

void usb_connect() {
	nvic_config();
	usbd_connect(&usbd);
	while (usbd.cur_status != USBD_CONFIGURED) {}
}

void USBD_HP_CAN0_TX_IRQHandler()
{
	usbd_isr();
}

void USBD_LP_CAN0_RX0_IRQHandler()
{
	usbd_isr();
}

void USBD_WKUP_IRQHandler()
{
	exti_interrupt_flag_clear(EXTI_18);
}
