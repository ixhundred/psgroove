/*
    PSGroove Exploit

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include <avr/io.h>
#include <avr/wdt.h>
#include <avr/power.h>
#include <avr/interrupt.h>
#include <avr/pgmspace.h>
#include <util/delay.h>
#include <string.h>
#include <stdio.h>

#include "config.h"
#include "psgroove.h"

#if VERSION == 1
	#include "descriptor.h"
//	#error "include psgroove"
#else
	#include "descriptor-hermes.h"
//	#error "include hermes"
#endif
#include "usb_utils.h"
#include "oddebug.h"

void setLed(int color) {
   RED_PORT &= ~(1 << RED_BIT);
   GREEN_PORT &= ~(1 << GREEN_BIT);
   if (color & RED) {
      RED_PORT |= 1 << RED_BIT;
   }
   if (color & GREEN) {
      GREEN_PORT |= 1 << GREEN_BIT;
   }
}

uint16_t port_status[6] = { PORT_EMPTY, PORT_EMPTY, PORT_EMPTY, PORT_EMPTY, PORT_EMPTY, PORT_EMPTY };
uint16_t port_change[6] = { C_PORT_NONE, C_PORT_NONE, C_PORT_NONE, C_PORT_NONE, C_PORT_NONE, C_PORT_NONE };

enum { 
	init,
	wait_hub_ready,
	hub_ready,
	p1_wait_reset,
	p1_wait_enumerate,
	p1_ready,
	p2_wait_reset,
	p2_wait_enumerate,
	p2_ready,
	p3_wait_reset,
	p3_wait_enumerate,
	p3_ready,
	p2_wait_disconnect,
	p4_wait_connect,
	p4_wait_reset,
	p4_wait_enumerate,
	p4_ready,
	p5_wait_reset,
	p5_wait_enumerate,
	p5_challenged,
	p5_responded,
	p3_wait_disconnect,
	p3_disconnected,
	p5_wait_disconnect,
	p5_disconnected,
	p4_wait_disconnect,
	p4_disconnected,
	p1_wait_disconnect,
	p1_disconnected,
	p6_wait_reset,
	p6_wait_enumerate,
	done,
} state = init;

uint8_t hub_int_response = 0x00;
uint8_t hub_int_force_data0 = 0;
int last_port_conn_clear = 0;
int last_port_reset_clear = 0;

// Need a copy in ram to work with usbFunctionDescriptor()
uchar HUB_Hub_Descriptor_ram[sizeof(HUB_Hub_Descriptor)];

uchar port_addr[7] = { 0, 0, 0, 0, 0, 0, 0 };
uchar connected_ports[7] = { 1, 0, 0, 0, 0, 0, 0 };
int8_t port_cur = -1;

extern uchar usbDeviceAddr;
extern uchar usbNewDeviceAddr;
extern uchar usbRxLen;
extern uchar usbTxLen;

void usbSetAddr(uchar addr)
{
   uchar msg[] = {port_cur, addr};
   DBG2(0x03, msg, 2);
   if(port_addr[port_cur] == 0)
      port_addr[port_cur] = addr;
}

void switch_port(uchar port)
{
	if (port_cur == port) return;
	port_cur = port;
   usbRxLen = 0;
   usbTxLen = 0;
   usbTxLen1 = USBPID_NAK;
   usbNewDeviceAddr = port_addr[port];
	usbDeviceAddr = port_addr[port] << 1;
}

volatile uint8_t expire = 0; /* counts down every 10 milliseconds */
ISR(TIMER1_OVF_vect) 
{ 
	uint16_t rate = (uint16_t) -(F_CPU / 64 / 100);
	TCNT1H = rate >> 8;
	TCNT1L = rate & 0xff;
	if (expire > 0)
		expire--;
}

void SetupLEDs()
{
   GREEN_DDR |= (1 << GREEN_BIT);
   RED_DDR |= (1 << RED_BIT);
   setLed(NONE);
}

void SetupHardware(void)
{
   usbDeviceConnect();
   odDebugInit();
	/* Disable watchdog if enabled by bootloader/fuses */
	MCUSR &= ~(1 << WDRF);
	wdt_disable();

	/* Disable clock division */
	clock_prescale_set(clock_div_1);

	/* Setup timer */
	TCCR1B = 0x03;  /* timer rate clk/64 */
	TIMSK1 = 0x01;

	/* Hardware Initialization */
	SetupLEDs();
   usbInit();
	sei(); 
}

void panic(int led1, int led2)
{
   DBGMSG1("Panic!");
	for(;;) {
		_delay_ms(1000);
		setLed(led1);
		_delay_ms(1000);
		setLed(led2);
	}		
}

void HUB_Task(void)
{
	if (usbInterruptIsReady())
	{
		if (hub_int_response) {
			if (hub_int_force_data0) {
            resetDataToggle();
				hub_int_force_data0 = 0;
			}
         interruptWrite_Byte(hub_int_response);
         sendInterruptBuffer();
         DBGX2("Hub resp: ", &hub_int_response, 1);
			hub_int_response = 0x00;
		}
	}
}

const uint8_t PROGMEM jig_response[64] = {
	0x80, 0x00, 0x00, 0x00, 0x00, 0x3d, 0xee, 0x78, 0x80, 0x00, 0x00, 0x00, 0x00, 0x3d, 0xee, 0x88,
	0x80, 0x00, 0x00, 0x00, 0x00, 0x33, 0xe7, 0x20, 0xe8, 0x83, 0xff, 0xf0, 0xe8, 0x63, 0xff, 0xf8,
	0xe8, 0xa3, 0x00, 0x18, 0x38, 0x63, 0x10, 0x00, 0x7c, 0x04, 0x28, 0x00, 0x40, 0x82, 0xff, 0xf4,
	0x38, 0xc3, 0xf0, 0x20, 0x7c, 0xc9, 0x03, 0xa6, 0x4e, 0x80, 0x04, 0x20, 0x04, 0x00, 0x00, 0x00,
};

// Junk the jig challenge.
uchar usbFunctionWrite(uchar *data, uchar len)
{
   static int bytes_out = 0;
   bytes_out += 8;
   if (bytes_out >= 64) {
      expire = 50;
      state = p5_challenged;
      DBGMSG2("Finished challenge.");
      return 0;
   } else {
      return USB_NO_MSG;
   }
}

void JIG_Task(void)
{
	static uchar bytes_in = 0;
   if (usbInterruptIsReady() && state == p5_challenged && expire == 0) 
	{
		if (bytes_in < 64) {
         pUsbSetInterrupt(&jig_response[bytes_in], 8);
			bytes_in += 8;
			if (bytes_in >= 64) {
				state = p5_responded;
				expire = 15;
			}
		}
	}
}

void connect_port(int port)
{
   connected_ports[port] = 1;
	last_port_reset_clear = 0;
	hub_int_response |= (1 << port);
	port_status[port - 1] = PORT_FULL;
	port_change[port - 1] = C_PORT_CONN;
}

void disconnect_port(int port)
{
   connected_ports[port] = 0;
	last_port_conn_clear = 0;
	hub_int_response |= (1 << port);
	port_status[port - 1] = PORT_EMPTY;
	port_change[port - 1] = C_PORT_CONN;
}

int main(void)
{
	SetupHardware();

	setLed(RED);

	state = init;
	switch_port(0);
   DBGMSG1("Ready.");

   // Copy the hub descriptor into ram, vusb's
   // usbFunctionSetup() callback can't handle stuff
   // from FLASH
   memcpy_P(HUB_Hub_Descriptor_ram, HUB_Hub_Descriptor, sizeof(HUB_Hub_Descriptor));

	for (;;)
	{
		if (port_cur == 0)
			HUB_Task();

		if (port_cur == 5)
			JIG_Task();

      usbPoll();

		// connect 1
		if (state == hub_ready && expire == 0)
		{
         DBG1(0x00, "\x1", 1);
         setLed(NONE);
			connect_port(1);
			state = p1_wait_reset;
		}
		
		if (state == p1_wait_reset && last_port_reset_clear == 1)
		{
         DBG1(0x00, "\x2", 1);
         setLed(RED);
			switch_port(1);
			state = p1_wait_enumerate;
		}

		// connect 2
		if (state == p1_ready && expire == 0)
		{
         DBG1(0x00, "\x3", 1);
         setLed(NONE);
			switch_port(0);
			connect_port(2);
			state = p2_wait_reset;
		}

		if (state == p2_wait_reset && last_port_reset_clear == 2)
		{
         DBG1(0x00, "\x4", 1);
         setLed(RED);
			switch_port(2);
			state = p2_wait_enumerate;
		}

		// connect 3
		if (state == p2_ready && expire == 0)
		{
         DBG1(0x00, "\x5", 1);
         setLed(NONE);
			switch_port(0);
			connect_port(3);
			state = p3_wait_reset;
		}

		if (state == p3_wait_reset && last_port_reset_clear == 3)
		{
         DBG1(0x00, "\x6", 1);
         setLed(RED);
			switch_port(3);
			state = p3_wait_enumerate;
		}

		// disconnect 2
		if (state == p3_ready && expire == 0)
		{
         DBG1(0x00, "\x7", 1);
         setLed(NONE);
			switch_port(0);
			disconnect_port(2);
			state = p2_wait_disconnect;
		}

		if (state == p2_wait_disconnect && last_port_conn_clear == 2)
		{
         DBG1(0x00, "\x8", 1);
         setLed(RED);
			state = p4_wait_connect;
			expire = 15;
		}

		// connect 4
		if (state == p4_wait_connect && expire == 0) 
		{
         DBG1(0x00, "\x9", 1);
         setLed(NONE);
			connect_port(4);
			state = p4_wait_reset;
		}

		if (state == p4_wait_reset && last_port_reset_clear == 4)
		{
         DBG1(0x00, "\x10", 1);
         setLed(RED);
			switch_port(4);
			state = p4_wait_enumerate;
		}

		// connect 5
		if (state == p4_ready && expire == 0)
		{
         DBG1(0x00, "\x11", 1);
         setLed(NONE);
			switch_port(0);
			/* When first connecting port 5, we need to
			   have the wrong data toggle for the PS3 to
			   respond */
			hub_int_force_data0 = 1;
			connect_port(5);
			state = p5_wait_reset;
		}

		if (state == p5_wait_reset && last_port_reset_clear == 5)
		{
         DBG1(0x00, "\x12", 1);
         setLed(RED);
			switch_port(5);
			state = p5_wait_enumerate;
		}

		// disconnect 3
		if (state == p5_responded && expire == 0)
		{
         DBG1(0x00, "\x13", 1);
         setLed(NONE);
			switch_port(0);
			/* Need wrong data toggle again */
			hub_int_force_data0 = 1;
			disconnect_port(3);
			state = p3_wait_disconnect;
		}

		if (state == p3_wait_disconnect && last_port_conn_clear == 3)
		{
         DBG1(0x00, "\x14", 1);
         setLed(RED);
			state = p3_disconnected;
			expire = 45;
		}

		// disconnect 5
		if (state == p3_disconnected && expire == 0)
		{
         DBG1(0x00, "\x15", 1);
         setLed(NONE);
			switch_port(0);
			disconnect_port(5);
			state = p5_wait_disconnect;
		}

		if (state == p5_wait_disconnect && last_port_conn_clear == 5)
		{
         DBG1(0x00, "\x16", 1);
         setLed(RED);
			state = p5_disconnected;
			expire = 20;
		}

		// disconnect 4
		if (state == p5_disconnected && expire == 0)
		{
         DBG1(0x00, "\x17", 1);
         setLed(NONE);
			switch_port(0);
			disconnect_port(4);
			state = p4_wait_disconnect;
		}

		if (state == p4_wait_disconnect && last_port_conn_clear == 4)
		{
         DBG1(0x00, "\x18", 1);
         setLed(RED);
			state = p4_disconnected;
			expire = 20;
		}

		// disconnect 1
		if (state == p4_disconnected && expire == 0)
		{
         DBG1(0x00, "\x19", 1);
         setLed(NONE);
			switch_port(0);
			disconnect_port(1);
			state = p1_wait_disconnect;
		}

		if (state == p1_wait_disconnect && last_port_conn_clear == 1)
		{
         DBG1(0x00, "\x20", 1);
         setLed(RED);
			state = p1_disconnected;
			expire = 20;
		}

		// connect 6
		if (state == p1_disconnected && expire == 0)
		{
         DBG1(0x00, "\x21", 1);
         setLed(NONE);
			switch_port(0);
			connect_port(6);
			state = p6_wait_reset;
		}

		if (state == p6_wait_reset && last_port_reset_clear == 6)
		{
         DBG1(0x00, "\x22", 1);
         setLed(RED);
			switch_port(6);
			state = p6_wait_enumerate;
		}

		// done
		if (state == done)
		{
			setLed(GREEN);
		}
	}
}

usbMsgLen_t usbFunctionDescriptor(struct usbRequest *rq)
{
	const uint8_t  DescriptorType   = rq->wValue.bytes[1];
	const uint8_t  DescriptorNumber = rq->wValue.bytes[0];
	const uint16_t  wLength = rq->wLength.word;

	usbMsgLen_t       Size    = 0;

	switch (DescriptorType)
	{
	case USBDESCR_DEVICE:
		switch (port_cur) {
		case 0:
			usbMsgPtr = (void *) HUB_Device_Descriptor;
			Size    = sizeof(HUB_Device_Descriptor);
			break;
		case 1:
			usbMsgPtr = (void *) port1_device_descriptor;
			Size    = sizeof(port1_device_descriptor);
			break;
		case 2:
			usbMsgPtr = (void *) port2_device_descriptor;
			Size    = sizeof(port2_device_descriptor);
			break;
		case 3:
			usbMsgPtr = (void *) port3_device_descriptor;
			Size    = sizeof(port3_device_descriptor);
			break;
		case 4:
			usbMsgPtr = (void *) port4_device_descriptor;
			Size    = sizeof(port4_device_descriptor);
			break;
		case 5:
			usbMsgPtr = (void *) port5_device_descriptor;
			Size    = sizeof(port5_device_descriptor);
			break;
		case 6:
			usbMsgPtr = (void *) port6_device_descriptor;
			Size    = sizeof(port6_device_descriptor);
			break;
		}
		break;
	case USBDESCR_CONFIG: 
		switch (port_cur) {
		case 0:
			usbMsgPtr = (void *) HUB_Config_Descriptor;
			Size    = sizeof(HUB_Config_Descriptor);
			break;
		case 1:
			// 4 configurations are the same.
			// For the initial 8-byte request, we give a different
			// length response than in the full request.
			if (DescriptorNumber < 4) {
				if (wLength == 8) {
					usbMsgPtr = (void *) port1_short_config_descriptor;
					Size    = sizeof(port1_short_config_descriptor);
				} else {
					usbMsgPtr = (void *) port1_config_descriptor;
					Size    = sizeof(port1_config_descriptor);
				}
				if (DescriptorNumber == 3 && wLength > 8) {
					state = p1_ready;
					expire = 20;
				}
			}
			break;
		case 2:
			// only 1 config
			usbMsgPtr = (void *) port2_config_descriptor;
			Size    = sizeof(port2_config_descriptor);
			state = p2_ready;
			expire = 15;
			break;
		case 3:
			// 2 configurations are the same
			usbMsgPtr = (void *) port3_config_descriptor;
			Size    = sizeof(port3_config_descriptor);
			if (DescriptorNumber == 1 && wLength > 8) {
				state = p3_ready;
				expire = 20;
			}
			break;
		case 4:
			// 3 configurations
			if (DescriptorNumber == 0) {
				usbMsgPtr = (void *) port4_config_descriptor_1;
				Size    = sizeof(port4_config_descriptor_1);
			} else if (DescriptorNumber == 1) {
				if (wLength == 8) {
					usbMsgPtr = (void *) port4_short_config_descriptor_2;
					Size    = sizeof(port4_short_config_descriptor_2);
				} else {
					usbMsgPtr = (void *) port4_config_descriptor_2;
					Size    = sizeof(port4_config_descriptor_2);
				}
			} else if (DescriptorNumber == 2) {
				usbMsgPtr = (void *) port4_config_descriptor_3;
				Size    = sizeof(port4_config_descriptor_3);
				if (wLength > 8) {
					state = p4_ready;
					expire = 20;  // longer seems to help this one?
				}
			}
			break;
		case 5:
			// 1 config
			usbMsgPtr = (void *) port5_config_descriptor;
			Size    = sizeof(port5_config_descriptor);
			break;
		case 6:
			// 1 config
			usbMsgPtr = (void *) port6_config_descriptor;
			Size    = sizeof(port6_config_descriptor);
			break;
		}
		break;
	}

   DBGMSG2("getDescriptorType and number");
   DBG2(0x01, &DescriptorType, 1);
   DBG2(0x01, &DescriptorNumber, 1);
	
	return Size;
}


usbMsgLen_t usbFunctionSetup(uchar data[8])
{
   usbRequest_t *rq = (usbRequest_t *) data;
   uchar msg[] = {usbDeviceAddr, usbNewDeviceAddr};
	if (port_cur == 6 && rq->bRequest == 0xAA) {
		/* holy crap, it worked! */
		state = done;
		return 0;
	}		
	
	if (port_cur == 5 && rq->bRequest == USBRQ_SET_INTERFACE)
	{
		/* can ignore this */
		return 0;
	}

   if (port_cur == 0 &&
       rq->bmRequestType == 0xA0 &&
       rq->bRequest == 0x06 &&
       rq->wValue.bytes[1] == 0x29) {
      usbMsgPtr = (void *) HUB_Hub_Descriptor_ram;
      return sizeof(HUB_Hub_Descriptor_ram);
   }

	if (port_cur == 0 &&
	    rq->bmRequestType == 0xA0 &&
	    rq->bRequest == 0x00 &&  // GET HUB STATUS
	    rq->wValue.word == 0x00 &&
	    rq->wIndex.word == 0x00 &&
	    rq->wLength.word == 0x04) {
      DBGMSG2("Get hub status.");
      outBuffer_Write_Word(0x0000);
      outBuffer_Write_Word(0x0000);
		return sendOutBuffer();
	}

	if (port_cur == 0 &&
	    rq->bmRequestType == 0xA3 &&  
	    rq->bRequest == 0x00 &&   //  GET PORT STATUS
	    rq->wValue.word == 0x00 &&
	    rq->wLength.word == 0x04) {
		uint8_t p = rq->wIndex.word;
		if (p < 1 || p > 6) return 0;
      DBGX2("Get port status: ", &p, 1);
      outBuffer_Write_Word(port_status[p - 1]);
      outBuffer_Write_Word(port_change[p - 1]);
		return sendOutBuffer();
	}

	if (port_cur == 0 &&
	    rq->bmRequestType == 0x23 &&
	    rq->bRequest == 0x03 && // SET_FEATURE
	    rq->wLength.word == 0x00) {
		uint8_t p = rq->wIndex.word;
		if (p < 1 || p > 6) return 0;
		
		switch(rq->wValue.word) {
		case 0x0008: // PORT_POWER
         DBGX2("Set port power: ", &p, 1);
			if (p == 6 && state == init) {
				/* after the 6th port is powered, wait a bit and continue */
				state = hub_ready;
				expire = 15;
			}
			break;
		case 0x0004: // PORT_RESET
         DBGX2("Set port reset: ", &p, 1);
			hub_int_response |= (1 << p);
			port_change[p - 1] |= C_PORT_RESET;
			break;
		}
		return 0;
	}

	if (port_cur == 0 &&
	    rq->bmRequestType == 0x23 &&
	    rq->bRequest == 0x01 && // CLEAR_FEATURE
	    rq->wLength.word == 0x00) {
		uint8_t p = rq->wIndex.word;
		if (p < 1 || p > 6) return 0;

		switch(rq->wValue.word) {
		case 0x0010: // C_PORT_CONNECTION
         DBGX2("Clear C_PORT_CONN: ", &p, 1);
			port_change[p - 1] &= ~C_PORT_CONN;
			last_port_conn_clear = p;
			break;
		case 0x0014: // C_PORT_RESET
         DBGX2("Clear C_PORT_RESET: ", &p, 1);
			port_change[p - 1] &= ~C_PORT_RESET;
			last_port_reset_clear = p;
			break;
		}
		return 0;
	}
   DBGX2("Unknown setup request:", rq, sizeof(usbRequest_t));
	panic(RED, GREEN);
   return 0;
}
