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

	usbd_init(&usbd, desc, class_core);
}

void usb_connect()
{
	nvic_config();
	usbd_connect(&usbd);
	while (usbd.cur_status != USBD_CONFIGURED) {}
}

static usbd_ep_ram* usb_ep_ram(uint8_t ep)
{
	usbd_ep_ram *btable_ep = (usbd_ep_ram*)(USBD_RAM + 2 * (BTABLE_OFFSET & 0xFFF8));
	return &btable_ep[ep];
}

uint8_t* usb_ep_rx_addr(uint8_t ep)
{
	usbd_ep_ram* ep_ram = usb_ep_ram(ep);
	return (uint8_t*)(ep_ram->rx_addr * 2U + USBD_RAM);
}

uint32_t* usb_ep_rx_count(uint8_t ep)
{
	usbd_ep_ram* ep_ram = usb_ep_ram(ep);
	return &ep_ram->rx_count;
}

uint8_t* usb_ep_tx_addr(uint8_t ep)
{
	usbd_ep_ram* ep_ram = usb_ep_ram(ep);
	return (uint8_t*)(ep_ram->tx_addr * 2U + USBD_RAM);
}

uint32_t* usb_ep_tx_count(uint8_t ep)
{
	usbd_ep_ram* ep_ram = usb_ep_ram(ep);
	return &ep_ram->tx_count;
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
