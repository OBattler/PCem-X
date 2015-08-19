#include "ibm.h"

#include "colorbook_io.h"

static uint8_t colorbook_regs[2];
static uint8_t tries;

void colorbook_io_write(uint16_t port, uint8_t val, void *priv)
{
        // pclog("colorbook_io_write : port=%04x = %02X\n", port, val);

	colorbook_regs[port & 3] = val;
	return;
}

uint8_t colorbook_io_read(uint16_t port, void *priv)
{
        // pclog("colorbook_io_read : port=%04x = %02X\n", port, colorbook_regs[port & 1]);

	return colorbook_regs[port & 3];
}

void colorbook_io_init()
{
        io_sethandler(0x24, 0x0004, colorbook_io_read, NULL, NULL, colorbook_io_write, NULL, NULL,  NULL);
}
