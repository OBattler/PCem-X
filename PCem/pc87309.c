/*
	National Semiconductors PC87309 Super I/O Chip
	Used by Tyan AMI 440BX
*/

#include "ibm.h"

#include "fdc.h"
#include "ide.h"
#include "io.h"
#include "lpt.h"
#include "mouse_serial.h"
#include "serial.h"
#include "pc87309.h"

static int pc87309_locked;
static int pc87309_curreg = 0;
static uint8_t pc87309_regs[48];
static uint8_t pc87309_ld_regs[7][256];

static uint8_t tries;

static uint16_t ld0_valid_ports[2] = {0x3F2, 0x372};
static uint16_t ld1_valid_ports[3] = {0x3BC, 0x378, 0x278};
static uint16_t ld2_valid_ports[9] = {0x3F8, 0x2F8, 0x338, 0x3E8, 0x2E8, 0x220, 0x238, 0x2E0, 0x228};
static uint16_t ld2_valid_ports2[9] = {0x3F8, 0x2F8, 0x338, 0x3E8, 0x2E8, 0x220, 0x238, 0x2E0, 0x228};
static uint16_t ld3_valid_ports[9] = {0x3F8, 0x2F8, 0x338, 0x3E8, 0x2E8, 0x220, 0x238, 0x2E0, 0x228};

static uint8_t is_in_array(uint16_t *port_array, uint8_t max, uint16_t port)
{
	uint8_t i = 0;

	for (i = 0; i < max; i++)
	{
		if (port_array[i] == port)  return 1;
	}
	return 0;
}

static uint16_t make_port(uint8_t ld)
{
	uint16_t r0 = pc87309_ld_regs[ld][0x60];
	uint16_t r1 = pc87309_ld_regs[ld][0x61];

	uint16_t p = (r0 << 8) + r1;

	switch(ld)
	{
		case 0:
			p &= 0xFFC;
			p |= 2;
			if ((p < 0) || (p > 0x3FF))  p = 0x3F2;
			if (!(is_in_array(ld0_valid_ports, 2, p)))  p = 0x3F2;
			break;
		case 1:
			if ((p < 0x0) || (p > 0x3FF))  p = 0x378;
			if (!(is_in_array(ld1_valid_ports, 3, p)))  p = 0x378;
			break;
		case 2:
			if ((p < 0x0) || (p > 0x3FF))  p = 0x3F8;
			if (!(is_in_array(ld2_valid_ports, 9, p)))  p = 0x3F8;
			break;
		case 3:
			if ((p < 0x0) || (p > 0x3FF))  p = 0x2F8;
			if (!(is_in_array(ld3_valid_ports, 9, p)))  p = 0x2F8;
			break;
	}

	pc87309_ld_regs[ld][0x60] = (p >> 8);
	pc87309_ld_regs[ld][0x61] = (p & 0xFF);

	return p;
}

/* uint16_t make_port2(uint8_t ld)
{
	uint16_t r0 = pc87309_ld_regs[ld][0x62];
	uint16_t r1 = pc87309_ld_regs[ld][0x63];

	uint16_t p = (r0 << 8) + r1;

	switch(ld)
	{
		case 2:
			if ((p < 0) || (p > 0x3F8))  p = 0x3E8;
			if (!(is_in_array(ld2_valid_ports2, 9, p)))  p = 0x3E8;
			break;
	}

	pc87309_ld_regs[ld][0x62] = (p >> 8);
	pc87309_ld_regs[ld][0x63] = (p & 0xFF);

	return p;
} */

void pc87309_write(uint16_t port, uint8_t val, void *priv)
{
	uint8_t index = (port & 1) ? 0 : 1;
	uint8_t valxor = 0;
	uint16_t ld_port = 0;
	uint16_t ld_port2 = 0;
        int temp;
        pclog("pc87309_write : port=%04x reg %02X = %02X locked=%i\n", port, pc87309_curreg, val, pc87309_locked);

	if (index)
	{
		if ((val == 0x55) && !pc87309_locked)
		{
			if (tries)
			{
				pc87309_locked = 1;
				tries = 0;
			}
			else
			{
				tries++;
			}
		}
		else
		{
			if (pc87309_locked)
			{
				if (val == 0xaa)
				{
					pc87309_locked = 1;
					return;
				}
				pc87309_curreg = val;
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
		if (pc87309_locked)
		{
			if (pc87309_curreg < 48)
			{
				valxor = val ^ pc87309_regs[pc87309_curreg];
				pc87309_regs[pc87309_curreg] = val;
			}
			else
			{
				valxor = val ^ pc87309_ld_regs[pc87309_regs[7]][pc87309_curreg];
				if ((pc87309_curreg & 0xF0 == 0x70) && (pc87309_regs[7] < 4))  return;
				/* Block writes to IDE configuration. */
				if (pc87309_regs[7] == 1)  return;
				if (pc87309_regs[7] == 2)  return;
				if (pc87309_regs[7] > 3)  return;
				pc87309_ld_regs[pc87309_regs[7]][pc87309_curreg] = val;
				goto process_value;
			}
		}
	}
	return;

process_value:
	switch(pc87309_regs[7])
	{
		case 0:
			/* FDC */
			switch(pc87309_curreg)
			{
				case 0x30:
					/* Activate */
					if (valxor)
					{
						if (!val)
							fdc_remove();
						else
						{
							ld_port = make_port(0);
							fdc_add_ex(ld_port, 1);
						}
					}
					break;
				case 0x60:
				case 0x61:
					if (valxor && pc87309_ld_regs[0][0x30])
					{
						fdc_remove();
						ld_port = make_port(0);
						pc87309_ld_regs[0][0x60] = make_port(0) >> 8;
						pc87309_ld_regs[0][0x61] = make_port(0) & 0xFF;
						fdc_add_ex(ld_port, 1);
					}
					break;
				case 0xF0:
					if (valxor & 0x20)  densel_polarity = (val & 0x20) ? 1 : 0;
					if (valxor & 0x40)  fdc_os2 = (val & 0x40) ? 1 : 0;
					break;
			}
			break;
		case 1:
			/* Parallel port */
			switch(pc87309_curreg)
			{
				case 0x30:
					/* Activate */
					if (valxor)
					{
						if (!val)
							lpt1_remove();
						else
						{
							ld_port = make_port(1);
							lpt1_init(ld_port);
						}
					}
					break;
				case 0x60:
				case 0x61:
					if (valxor && pc87309_ld_regs[3][0x30])
					{
						lpt1_remove();
						ld_port = make_port(3);
						lpt1_init(ld_port);
					}
					break;
			}
			break;
		case 2:
			/* Serial port 1 */
			switch(pc87309_curreg)
			{
				case 0x30:
					/* Activate */
					if (valxor)
					{
						if (!val)
							serial1_remove();
						else
						{
							ld_port = make_port(2);
							serial2_set(ld_port, pc87309_ld_regs[2][0x70]);
							mouse_serial_init();
						}
					}
					break;
				case 0x60:
				case 0x61:
				case 0x70:
					if (valxor && pc87309_ld_regs[2][0x30])
					{
						ld_port = make_port(2);
						serial2_set(ld_port, pc87309_ld_regs[2][0x70]);
						mouse_serial_init();
					}
					break;
			}
			break;
		case 3:
			/* Serial port 2 */
			switch(pc87309_curreg)
			{
				case 0x30:
					/* Activate */
					if (valxor)
					{
						if (!val)
							serial2_remove();
						else
						{
							ld_port = make_port(3);
							serial2_set(ld_port, pc87309_ld_regs[3][0x70]);
						}
					}
					break;
				case 0x60:
				case 0x61:
				case 0x70:
					if (valxor && pc87309_ld_regs[3][0x30])
					{
						ld_port = make_port(3);
						serial2_set(ld_port, pc87309_ld_regs[3][0x70]);
					}
					break;
			}
			break;
	}
}

uint8_t pc87309_read(uint16_t port, void *priv)
{
        pclog("pc87309_read : port=%04x reg %02X locked=%i\n", port, pc87309_curreg, pc87309_locked);
	uint8_t index = (port & 1) ? 0 : 1;

	if (!pc87309_locked)
	{
		return 0xff;
	}

	if (index)
		return pc87309_curreg;
	else
	{
		if (pc87309_curreg < 0x30)
		{
			pclog("0x03F1: %02X\n", pc87309_regs[pc87309_curreg]);
			return pc87309_regs[pc87309_curreg];
		}
		else
		{
			pclog("0x03F1 (CD=%02X): %02X\n", pc87309_regs[7], pc87309_ld_regs[pc87309_regs[7]][pc87309_curreg]);
			return pc87309_ld_regs[pc87309_regs[7]][pc87309_curreg];
		}
	}
}

void pc87309_init()
{
	int i = 0;

	fdc_remove_stab();

	lpt2_remove();
	lpt1_remove();
	lpt1_init(0x278);

	for (i = 0; i < 0x30; i++)
	{
		pc87309_regs[i] = 0;
	}

	pc87309_regs[0x20] = 0xE0;

	for (i = 0; i < 10; i++)
	{
		memset(pc87309_ld_regs[i], 0, 256);
	}

	/* Logical device 0: FDC */
	pc87309_ld_regs[0][0x30] = 1;
	pc87309_ld_regs[0][0x60] = 3;
	pc87309_ld_regs[0][0x61] = 0xF2;
	pc87309_ld_regs[0][0x70] = 6;
	pc87309_ld_regs[0][0x71] = 3;
	pc87309_ld_regs[0][0x74] = 2;
	pc87309_ld_regs[0][0x75] = 4;
	pc87309_ld_regs[0][0xF0] = 0x20;

	/* Logical device 1: Parallel Port */
	pc87309_ld_regs[1][0x30] = 1;
	pc87309_ld_regs[1][0x60] = 2;
	pc87309_ld_regs[1][0x61] = 0x78;
	pc87309_ld_regs[1][0x70] = 7;
	pc87309_ld_regs[1][0x74] = 4;
	pc87309_ld_regs[1][0x75] = 4;
	pc87309_ld_regs[1][0xF0] = 0xF2;

	/* Logical device 2: Serial Port 2 */
	pc87309_ld_regs[2][0x30] = 1;
	pc87309_ld_regs[2][0x60] = 2;
	pc87309_ld_regs[2][0x61] = 0xf8;
	pc87309_ld_regs[2][0x70] = 3;
	pc87309_ld_regs[2][0x71] = 3;
	pc87309_ld_regs[2][0x74] = 4;
	pc87309_ld_regs[2][0x75] = 4;
	pc87309_ld_regs[2][0xF0] = 2;

	/* Logical device 3: Serial Port 1 */
	pc87309_ld_regs[3][0x30] = 1;
	pc87309_ld_regs[3][0x60] = 3;
	pc87309_ld_regs[3][0x61] = 0xf8;
	pc87309_ld_regs[3][0x70] = 4;
	pc87309_ld_regs[3][0x71] = 3;
	pc87309_ld_regs[3][0x74] = 4;
	pc87309_ld_regs[3][0x75] = 4;
	pc87309_ld_regs[3][0xF0] = 2;

	/* Logical device 4: Power Management */
	pc87309_ld_regs[4][0x74] = 4;
	pc87309_ld_regs[4][0x75] = 4;

	/* Logical device 5: KBC for Mouse */
	pc87309_ld_regs[5][0x30] = 1;
	pc87309_ld_regs[5][0x70] = 0xC;
	pc87309_ld_regs[5][0x71] = 2;
	pc87309_ld_regs[5][0x74] = 4;
	pc87309_ld_regs[5][0x75] = 4;

	/* Logical device 6: KBC for Keyboard */
	pc87309_ld_regs[6][0x30] = 1;
	pc87309_ld_regs[6][0x61] = 0x60;
	pc87309_ld_regs[6][0x63] = 0x64;
	pc87309_ld_regs[6][0x70] = 1;
	pc87309_ld_regs[6][0x71] = 2;
	pc87309_ld_regs[6][0x74] = 4;
	pc87309_ld_regs[6][0x75] = 4;
	pc87309_ld_regs[6][0xF0] = 0x40;

	densel_polarity = 1;
	densel_force = 0;
	fdc_setswap(0);
        io_sethandler(0x2e, 0x0002, pc87309_read, NULL, NULL, pc87309_write, NULL, NULL,  NULL);
        pc87309_locked = 1;
}
