/*
	Winbond W83977F Super I/O Chip
	Used by the Award 430TX
*/

#include "ibm.h"

#include "fdc.h"
#include "ide.h"
#include "io.h"
#include "lpt.h"
// #include "mouse_serial.h"
#include "serial.h"
#include "w83977f.h"

static int w83977f_locked;
static int w83977f_rw_locked = 0;
static int w83977f_curreg = 0;
static uint8_t w83977f_regs[48];
static uint8_t w83977f_ld_regs[9][256];

static uint8_t tries;

static uint16_t ld0_valid_ports[2] = {0x3F2, 0x372};
static uint16_t ld1_valid_ports[3] = {0x3BC, 0x378, 0x278};
static uint16_t ld2_valid_ports[9] = {0x3F8, 0x2F8, 0x338, 0x3E8, 0x2E8, 0x220, 0x238, 0x2E0, 0x228};
static uint16_t ld3_valid_ports[9] = {0x3F8, 0x2F8, 0x338, 0x3E8, 0x2E8, 0x220, 0x238, 0x2E0, 0x228};
static uint16_t ld6_valid_ports[9] = {0x3F8, 0x2F8, 0x338, 0x3E8, 0x2E8, 0x220, 0x238, 0x2E0, 0x228};

void w83977f_write(uint16_t port, uint8_t val, void *priv);
uint8_t w83977f_read(uint16_t port, void *priv);

static void w83977f_remap()
{
        io_removehandler(0x370, 0x0002, w83977f_read, NULL, NULL, w83977f_write, NULL, NULL,  NULL);
        io_removehandler(0x3f0, 0x0002, w83977f_read, NULL, NULL, w83977f_write, NULL, NULL,  NULL);
        io_sethandler((w83977f_regs[0x26] & 0x40) ? 0x370 : 0x3f0, 0x0002, w83977f_read, NULL, NULL, w83977f_write, NULL, NULL,  NULL);
	// pclog("W83977F mapped to %04X\n", (w83977f_regs[0x26] & 0x40) ? 0x370 : 0x3f0);
}

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
	uint16_t r0 = w83977f_ld_regs[ld][0x60];
	uint16_t r1 = w83977f_ld_regs[ld][0x61];

	uint16_t p = (r0 << 8) + r1;

	switch(ld)
	{
		case 0:
			p &= 0xFFC;
			if ((p < 0) || (p > 0x3FF))  p = 0x3F0;
			if (!(is_in_array(ld0_valid_ports, 2, p)))  p = 0x3F0;
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
		case 6:
			if ((p < 0) || (p > 0x3F8))  p = 0x3E8;
			if (!(is_in_array(ld6_valid_ports, 9, p)))  p = 0x3E8;
			break;
	}

	w83977f_ld_regs[ld][0x60] = (p >> 8);
	w83977f_ld_regs[ld][0x61] = (p & 0xFF);

	return p;
}

void w83977f_write(uint16_t port, uint8_t val, void *priv)
{
	uint8_t index = (port & 1) ? 0 : 1;
	uint8_t valxor = 0;
	uint16_t ld_port = 0;
	uint16_t ld_port2 = 0;
        int temp;

	if (index)
	{
	        // pclog("w83977f_write : port=%04x index = %02X locked=%i\n", port, val, w83977f_locked);
		if ((val == 0x87) && !w83977f_locked)
		{
			if (tries)
			{
				w83977f_locked = 1;
				tries = 0;
			}
			else
			{
				tries++;
			}
		}
		else
		{
			if (w83977f_locked)
			{
				if (val == 0xaa)
				{
					w83977f_locked = 0;
					return;
				}
				w83977f_curreg = val;
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
	        // pclog("w83977f_write : port=%04x reg %02X = %02X locked=%i\n", port, w83977f_curreg, val, w83977f_locked);
		if (w83977f_locked)
		{
			if (w83977f_rw_locked)  return;
			if (w83977f_curreg < 48)
			{
				valxor = val ^ w83977f_regs[w83977f_curreg];
				w83977f_regs[w83977f_curreg] = val;

				if (w83977f_curreg == 0x26)
				{
					if (valxor & 0x40)  w83977f_remap();
					if (valxor & 0x20)  w83977f_rw_locked = (val & 0x20) ? 1 : 0;
				}
			}
			else
			{
				valxor = val ^ w83977f_ld_regs[w83977f_regs[7]][w83977f_curreg];
				if ((w83977f_curreg & 0xF0 == 0x70) && (w83977f_regs[7] < 4))  return;
				/* Block writes to IDE configuration. */
				if (w83977f_regs[7] == 1)  return;
				if (w83977f_regs[7] == 2)  return;
				if (w83977f_regs[7] > 3)  return;
				w83977f_ld_regs[w83977f_regs[7]][w83977f_curreg] = val;
				goto process_value;
			}
		}
	}
	return;

process_value:
	switch(w83977f_regs[7])
	{
		case 0:
			/* FDC */
			switch(w83977f_curreg)
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
					if (valxor && w83977f_ld_regs[0][0x30])
					{
						fdc_remove();
						ld_port = make_port(0);
						w83977f_ld_regs[0][0x60] = make_port(0) >> 8;
						w83977f_ld_regs[0][0x61] = make_port(0) & 0xFF;
						fdc_add_ex(ld_port, 1);
					}
					break;
				case 0xF0:
					if (valxor & 0x10)  fdc_setswap((val & 0x10) ? 1 : 0);
					if (valxor & 0x01)  en3mode = (val & 0x01);
					break;
				case 0xF1:
					if (valxor & 0xC0)  boot_drive = (val & 0xC0) >> 6;
					if (valxor & 0x20)  densel_polarity_mid[1] = (val & 0x20) ? 0 : 1;
					if (valxor & 0x10)  densel_polarity_mid[0] = (val & 0x10) ? 0 : 1;
					if (valxor & 0x0C)  densel_force = (val & 0x0C) >> 2;
					if (valxor & 0x02)  diswr = (val & 0x02) >> 1;
					if (valxor & 0x01)  swwp = (val & 0x01);
					break;
				case 0xF2:
					if (valxor & 0x0C)  rwc_force[1] = (valxor & 0x0C) >> 2;
					if (valxor & 0x03)  rwc_force[0] = (valxor & 0x03);
					break;
				case 0xF4:
					if (valxor & 0x18)  drt[0] = (valxor & 0x18) >> 3;
					break;
				case 0xF5:
					if (valxor & 0x18)  drt[1] = (valxor & 0x18) >> 3;
					break;
			}
			break;
		case 1:
			/* Parallel port */
			switch(w83977f_curreg)
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
					if (valxor && w83977f_ld_regs[3][0x30])
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
			switch(w83977f_curreg)
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
							serial1_set(ld_port, w83977f_ld_regs[2][0x70]);
							// mouse_serial_init();
						}
					}
					break;
				case 0x60:
				case 0x61:
				case 0x70:
					if (valxor && w83977f_ld_regs[2][0x30])
					{
						ld_port = make_port(2);
						serial1_set(ld_port, w83977f_ld_regs[2][0x70]);
						// mouse_serial_init();
					}
					break;
			}
			break;
		case 3:
			/* Serial port 2 */
			switch(w83977f_curreg)
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
							serial2_set(ld_port, w83977f_ld_regs[3][0x70]);
						}
					}
					break;
				case 0x60:
				case 0x61:
				case 0x70:
					if (valxor && w83977f_ld_regs[3][0x30])
					{
						ld_port = make_port(3);
						serial2_set(ld_port, w83977f_ld_regs[3][0x70]);
					}
					break;
			}
			break;
	}
}

uint8_t w83977f_read(uint16_t port, void *priv)
{
        // pclog("w83977f_read : port=%04x reg %02X locked=%i\n", port, w83977f_curreg, w83977f_locked);
	uint8_t index = (port & 1) ? 0 : 1;

	if (!w83977f_locked)
	{
		return 0xff;
	}

	if (index)
		return w83977f_curreg;
	else
	{
		if (w83977f_curreg < 0x30)
		{
			// pclog("0x03F1: %02X\n", w83977f_regs[w83977f_curreg]);
			return w83977f_regs[w83977f_curreg];
		}
		else
		{
			// pclog("0x03F1 (CD=%02X): %02X\n", w83977f_regs[7], w83977f_ld_regs[w83977f_regs[7]][w83977f_curreg]);
			if ((w83977f_regs[7] == 0) && (w83977f_curreg == 0xF2))  return (rwc_force[0] | (rwc_force[1] << 2));
			return w83977f_ld_regs[w83977f_regs[7]][w83977f_curreg];
		}
	}
}

void w83977f_init()
{
	int i = 0;

	fdc_remove_stab();

	lpt2_remove();

	for (i = 0; i < 0x30; i++)
	{
		w83977f_regs[i] = 0;
	}

	w83977f_regs[0x22] = 0xFF;
	w83977f_regs[0x24] = 0x86;

	for (i = 0; i < 9; i++)
	{
		memset(w83977f_ld_regs[i], 0, 256);
	}

	/* Logical device 0: FDC */
	w83977f_ld_regs[0][0x30] = 1;
	w83977f_ld_regs[0][0x60] = 3;
	w83977f_ld_regs[0][0x61] = 0xF0;
	w83977f_ld_regs[0][0x70] = 6;
	w83977f_ld_regs[0][0x71] = 2;
	w83977f_ld_regs[0][0x74] = 2;
	w83977f_ld_regs[0][0xF0] = 0x0E;
	w83977f_ld_regs[0][0xF1] = 0x00;
	// w83977f_ld_regs[0][0xF2] = 0xFF;
	w83977f_ld_regs[0][0xF2] = 0xF5;
	w83977f_ld_regs[0][0xF4] = 0x00;
	w83977f_ld_regs[0][0xF5] = 0x00;

	/* Logical device 1: Parallel Port */
	w83977f_ld_regs[1][0x30] = 1;
	w83977f_ld_regs[1][0x60] = 3;
	w83977f_ld_regs[1][0x61] = 0x78;
	w83977f_ld_regs[1][0x70] = 7;
	w83977f_ld_regs[1][0x71] = 1;
	w83977f_ld_regs[1][0x74] = 4;
	w83977f_ld_regs[1][0xF0] = 0x3F;

	/* Logical device 2: Serial Port 2 */
	w83977f_ld_regs[2][0x30] = 1;
	w83977f_ld_regs[2][0x60] = 3;
	w83977f_ld_regs[2][0x61] = 0xf8;
	w83977f_ld_regs[2][0x70] = 4;
	w83977f_ld_regs[2][0x71] = 2;

	/* Logical device 3: Serial Port 1 */
	w83977f_ld_regs[3][0x30] = 1;
	w83977f_ld_regs[3][0x60] = 2;
	w83977f_ld_regs[3][0x61] = 0xf8;
	w83977f_ld_regs[3][0x70] = 3;
	w83977f_ld_regs[3][0x71] = 2;

	/* Logical device 4: Real Time Clock */
	w83977f_ld_regs[4][0x30] = 1;
	w83977f_ld_regs[4][0x60] = 0;
	w83977f_ld_regs[4][0x61] = 0x70;

	/* Logical device 5: KBC */
	w83977f_ld_regs[5][0x30] = 1;
	w83977f_ld_regs[5][0x60] = 0;
	w83977f_ld_regs[5][0x61] = 0x60;
	w83977f_ld_regs[5][0x62] = 0;
	w83977f_ld_regs[5][0x63] = 0x64;
	w83977f_ld_regs[5][0x70] = 1;
	w83977f_ld_regs[5][0x71] = 2;
	w83977f_ld_regs[5][0x72] = 0x0C;
	w83977f_ld_regs[5][0x73] = 2;
	w83977f_ld_regs[5][0xF0] = 0x40;

	/* Logical device 6: IR */
	w83977f_ld_regs[6][0x71] = 2;
	w83977f_ld_regs[6][0x74] = 4;
	w83977f_ld_regs[6][0x75] = 4;

	/* Logical device 7: Auxiliary I/O Part I */
	w83977f_ld_regs[7][0x71] = 2;
	w83977f_ld_regs[7][0x73] = 2;
	w83977f_ld_regs[7][0xE0] = 1;
	w83977f_ld_regs[7][0xE1] = 1;
	w83977f_ld_regs[7][0xE2] = 1;
	w83977f_ld_regs[7][0xE3] = 1;
	w83977f_ld_regs[7][0xE4] = 1;
	w83977f_ld_regs[7][0xE5] = 1;
	w83977f_ld_regs[7][0xE6] = 1;
	w83977f_ld_regs[7][0xE7] = 1;

	/* Logical device 8: Auxiliary I/O Part II */
	w83977f_ld_regs[8][0x71] = 2;
	w83977f_ld_regs[8][0xE8] = 1;
	w83977f_ld_regs[8][0xE9] = 1;
	w83977f_ld_regs[8][0xEA] = 1;
	w83977f_ld_regs[8][0xEB] = 1;
	w83977f_ld_regs[8][0xEC] = 1;
	w83977f_ld_regs[8][0xED] = 1;

	densel_polarity = 1;
	densel_force = 0;
	fdc_setswap(0);
	rwc_force[0] = rwc_force[1] = 1;
	drt[0] = drt[1] = 0;
	densel_polarity_mid[0] = densel_polarity_mid[1] = 1;
        w83977f_remap();
        w83977f_locked = 0;
        w83977f_rw_locked = 0;
	en3mode = 0;
}
