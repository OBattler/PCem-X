#include "ibm.h"

#include "fdc.h"
#include "io.h"
#include "lpt.h"
#include "mouse_serial.h"
#include "serial.h"
#include "cpqio.h"

typedef struct
{
	uint8_t turbo;
	uint8_t dip;

	uint8_t mm58167_reg[16];
} cpqio_t;

cpqio_t cpqio;

uint8_t cpqio_fdd2_enabled()
{
	if (cpqio.dip & 0xC0 == 0xC0)
		return 0;
	else
		return 1;
}

void cpqio_write(uint16_t port, uint8_t val, void *priv)
{
        int temp;

	switch (port)
	{
		case 0xCF:
			cpqio.turbo = val & 1;
			break;
	}
}

uint8_t cpqio_read(uint16_t port, void *priv)
{
        int temp;

	switch (port)
	{
		case 0x62:
			return (cpqio.turbo & 1);
		case 0x3ba:
			pclog("PORT 0x03BA: READ DIP SWITCHES\n");
			return (cpqio.dip);
	}
}

void cpqio_init()
{
	cpqio.dip = 0x9C;
        io_sethandler(0x0062, 0x0001, cpqio_read, NULL, NULL, NULL, NULL, NULL,  NULL);
        io_sethandler(0x00cf, 0x0001, NULL, NULL, NULL, cpqio_write, NULL, NULL,  NULL);
        io_sethandler(0x03ba, 0x0001, cpqio_read, NULL, NULL, NULL, NULL, NULL,  NULL);
}
