/*
	SMSC SMC FDC37C669 Super I/O Chip
	Used by the 430TX
*/

#include "ibm.h"

#include "fdc.h"
#include "io.h"
#include "lpt.h"
#include "mouse_serial.h"
#include "serial.h"
#include "fdc37c669.h"

static int fdc37c669_locked;
static int fdc37c669_rw_locked = 0;
static int fdc37c669_curreg = 0;
static uint8_t fdc37c669_regs[42];
static uint8_t tries;

static uint16_t fdc_valid_ports[2] = {0x3F0, 0x370};
static uint16_t ide_valid_ports[2] = {0x1F0, 0x170};
static uint16_t ide_as_valid_ports[2] = {0x3F6, 0x376};
static uint16_t lpt1_valid_ports[3] = {0x3BC, 0x378, 0x278};
static uint16_t com1_valid_ports[9] = {0x3F8, 0x2F8, 0x338, 0x3E8, 0x2E8, 0x220, 0x238, 0x2E0, 0x228};
static uint16_t com2_valid_ports[9] = {0x3F8, 0x2F8, 0x338, 0x3E8, 0x2E8, 0x220, 0x238, 0x2E0, 0x228};

static uint8_t is_in_array(uint16_t *port_array, uint8_t max, uint16_t port)
{
	uint8_t i = 0;

	for (i = 0; i < max; i++)
	{
		if (port_array[i] == port)  return 1;
	}
	return 0;
}

static uint16_t make_port(uint8_t reg)
{
	uint16_t p = 0;

	switch(reg)
	{
		case 0x20:
			p = ((uint16_t) (fdc37c669_regs[reg] & 0xfc)) << 2;
			p &= 0xFF0;
			if ((p < 0x100) || (p > 0x3F0))  p = 0x3F0;
			if (!(is_in_array(fdc_valid_ports, 2, p)))  p = 0x3F0;
			fdc37c669_regs[reg] = ((p >> 2) & 0xfc) | (fdc37c669_regs[reg] & 3);
			break;
		case 0x21:
			p = ((uint16_t) (fdc37c669_regs[reg] & 0xfc)) << 2;
			p &= 0xFF0;
			if ((p < 0x100) || (p > 0x3F0))  p = 0x1F0;
			if (!(is_in_array(ide_valid_ports, 2, p)))  p = 0x1F0;
			fdc37c669_regs[reg] = ((p >> 2) & 0xfc) | (fdc37c669_regs[reg] & 3);
			break;
		case 0x22:
			p = ((uint16_t) (fdc37c669_regs[reg] & 0xfc)) << 2;
			p &= 0xFF0;
			if ((p < 0x106) || (p > 0x3F6))  p = 0x3F6;
			if (!(is_in_array(ide_as_valid_ports, 2, p)))  p = 0x3F6;
			fdc37c669_regs[reg] = ((p >> 2) & 0xfc) | (fdc37c669_regs[reg] & 3);
			break;
		case 0x23:
			p = ((uint16_t) (fdc37c669_regs[reg] & 0xff)) << 2;
			p &= 0xFFC;
			if ((p < 0x100) || (p > 0x3F8))  p = 0x378;
			if (!(is_in_array(lpt1_valid_ports, 3, p)))  p = 0x378;
			fdc37c669_regs[reg] = (p >> 2);
			break;
		case 0x24:
			p = ((uint16_t) (fdc37c669_regs[reg] & 0xfe)) << 2;
			p &= 0xFF8;
			if ((p < 0x100) || (p > 0x3F8))  p = 0x3F8;
			if (!(is_in_array(com1_valid_ports, 9, p)))  p = 0x3F8;
			fdc37c669_regs[reg] = ((p >> 2) & 0xfe) | (fdc37c669_regs[reg] & 1);
			break;
		case 0x25:
			p = ((uint16_t) (fdc37c669_regs[reg] & 0xfe)) << 2;
			p &= 0xFF8;
			if ((p < 0x100) || (p > 0x3F8))  p = 0x2F8;
			if (!(is_in_array(com2_valid_ports, 9, p)))  p = 0x2F8;
			fdc37c669_regs[reg] = ((p >> 2) & 0xfe) | (fdc37c669_regs[reg] & 1);
			break;
	}

	return p;
}

void fdc37c669_write(uint16_t port, uint8_t val, void *priv)
{
	uint8_t index = (port & 1) ? 0 : 1;
	uint8_t valxor = 0;
	uint8_t max = 42;
        int temp;
        pclog("fdc37c669_write : port=%04x reg %02X = %02X locked=%i\n", port, fdc37c669_curreg, val, fdc37c669_locked);

	if (index)
	{
		if ((val == 0x55) && !fdc37c669_locked)
		{
			if (tries)
			{
				fdc37c669_locked = 1;
				tries = 0;
			}
			else
			{
				tries++;
			}
		}
		else
		{
			if (fdc37c669_locked)
			{
				if (val < max)  fdc37c669_curreg = val;
				if (val == 0xaa)  fdc37c669_locked = 0;
			}
			else
			{
				if (tries)
					tries = 0;
			}
		}
	}
	else
	{
		if (fdc37c669_locked)
		{
			if ((fdc37c669_curreg < 0x18) && (fdc37c669_rw_locked))  return;
			if ((fdc37c669_curreg >= 0x26) && (fdc37c669_curreg <= 0x27))  return;
			if (fdc37c669_curreg == 0x29)  return;
			valxor = val ^ fdc37c669_regs[fdc37c669_curreg];
			fdc37c669_regs[fdc37c669_curreg] = val;
			goto process_value;
		}
	}
	return;

process_value:
	switch(fdc37c669_curreg)
	{
		case 0:
			if (valxor & 3)
			{
				ide_pri_disable();
				if ((fdc37c669_regs[0] & 3) == 2)  ide_pri_enable_custom(make_port(0x21), make_port(0x22));
				break;
			}
			if (valxor & 8)
			{
				fdc_remove();
				if (fdc37c669_regs[0] & 8)  fdc_add_ex(make_port(0x20), 1);
			}
			break;
		case 1:
			if (valxor & 4)
			{
				lpt1_remove();
				if (fdc37c669_regs[1] & 4)  lpt1_init(make_port(0x23));
			}
			if (valxor & 7)
			{
				fdc37c669_rw_locked = (val & 8) ? 0 : 1;
			}
			break;
		case 2:
			if (valxor & 8)
			{
				serial1_remove();
				if (fdc37c669_regs[2] & 8)  serial1_set(make_port(0x24), (fdc37c669_regs[0x28] & 0xF0) >> 8);
			}
			if (valxor & 0x80)
			{
				serial2_remove();
				if (fdc37c669_regs[2] & 0x80)  serial2_set(make_port(0x25), fdc37c669_regs[0x28] & 0xF);
			}
			break;
		case 3:
			if (valxor & 2)  fdc_os2 = (val & 2) ? 1 : 0;
			break;
		case 5:
			if (valxor & 0x18)  densel_force = (val & 0x18) >> 3;
			if (valxor & 0x20)  fdc_setswap((val & 0x20) >> 5);
			break;
		case 0xB:
			if (valxor & 3)  drt[0] = (val & 3);
			if (valxor & 0xC)  drt[1] = (val & 0xC) >> 2;
			break;
		case 0x20:
			if (valxor & 0xfc)
			{
				fdc_remove();
				if (fdc37c669_regs[0] & 8)  fdc_add_ex(make_port(0x20), 1);
			}
			break;
		case 0x21:
		case 0x22:
			if (valxor & 0xfc)
			{
				ide_pri_disable();
				if ((fdc37c669_regs[0] & 3) == 2)  ide_pri_enable_custom(make_port(0x21), make_port(0x22));
			}
			break;
		case 0x23:
			if (valxor)
			{
				lpt1_remove();
				if (fdc37c669_regs[1] & 4)  lpt1_init(make_port(0x23));
			}
			break;
		case 0x24:
			if (valxor & 0xfe)
			{
				if (fdc37c669_regs[2] & 8)  serial1_set(make_port(0x24), (fdc37c669_regs[0x28] & 0xF0) >> 8);
			}
			break;
		case 0x25:
			if (valxor & 0xfe)
			{
				if (fdc37c669_regs[2] & 0x80)  serial2_set(make_port(0x25), fdc37c669_regs[0x28] & 0xF);
			}
			break;
		case 0x28:
			if (valxor & 0xf)
			{
				if (fdc37c669_regs[0x28] & 0xf == 0)  fdc37c669_regs[0x28] |= 0x3;
				if (fdc37c669_regs[2] & 0x80)  serial2_set(make_port(0x25), fdc37c669_regs[0x28] & 0xF);
			}
			if (valxor & 0xf0)
			{
				if (fdc37c669_regs[0x28] & 0xf0 == 0)  fdc37c669_regs[0x28] |= 0x40;
				if (fdc37c669_regs[2] & 8)  serial1_set(make_port(0x24), (fdc37c669_regs[0x28] & 0xF0) >> 8);
			}
			break;
	}
}

uint8_t fdc37c669_read(uint16_t port, void *priv)
{
        pclog("fdc37c669_read : port=%04x reg %02X locked=%i\n", port, fdc37c669_curreg, fdc37c669_locked);
	uint8_t index = (port & 1) ? 0 : 1;

	if (!fdc37c669_locked)
	{
		return fdc_read(port, priv);
	}

	if (index)
		return fdc37c669_curreg;
	else
	{
		pclog("0x03F1: %02X\n", fdc37c669_regs[fdc37c669_curreg]);
		if ((fdc37c669_curreg < 0x18) && (fdc37c669_rw_locked))  return 0xff;
		return fdc37c669_regs[fdc37c669_curreg];
	}
}

void fdc37c669_init()
{
	fdc_remove_stab();
	lpt2_remove();
	// lpt1_remove();
	// lpt1_init(0x378);
	/* Parallel port should be LPT 1, not LPT 2. */
	// fdc37c669_regs[0] = 0x3B;
	fdc37c669_regs[0] = 0x28;
	fdc37c669_regs[1] = 0x9C;
	fdc37c669_regs[2] = 0x88;
	fdc37c669_regs[3] = 0x78;
	fdc37c669_regs[4] = 0;
	fdc37c669_regs[5] = 0;
	fdc37c669_regs[6] = 0xFF;
	fdc37c669_regs[7] = 0;
	fdc37c669_regs[8] = 0;
	fdc37c669_regs[9] = 0;
	fdc37c669_regs[0xA] = 0;
	fdc37c669_regs[0xB] = 0;
	fdc37c669_regs[0xC] = 0;
	fdc37c669_regs[0xD] = 3;
	fdc37c669_regs[0xE] = 2;
	fdc37c669_regs[0x1E] = 0x3C;
	fdc37c669_regs[0x20] = (0x3f0 >> 2) & 0xfc;
	fdc37c669_regs[0x21] = (0x1f0 >> 2) & 0xfc;
	fdc37c669_regs[0x22] = ((0x3f6 >> 2) & 0xfc) | 1;
	fdc37c669_regs[0x23] = (0x378 >> 2);
	fdc37c669_regs[0x24] = (0x3f8 >> 2) & 0xfe;
	fdc37c669_regs[0x25] = (0x2f8 >> 2) & 0xfe;
	fdc37c669_regs[0x26] = (2 << 4) | 4;
	fdc37c669_regs[0x27] = (6 << 4) | 7;
	fdc37c669_regs[0x28] = (4 << 4) | 3;

	densel_polarity = 1;
	densel_force = 0;
	fdc_setswap(0);
	serial1_set(0x3f8, 4);
	serial2_set(0x2f8, 3);
        io_sethandler(0x3f0, 0x0002, fdc37c669_read, NULL, NULL, fdc37c669_write, NULL, NULL,  NULL);
        fdc37c669_locked = 0;
        fdc37c669_rw_locked = 0;
}
