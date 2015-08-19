/*
	SMSC SMC fdc37c932fr Super I/O Chip
	Used by Batman's Revenge
*/

#include "ibm.h"

#include "fdc.h"
#include "ide.h"
#include "io.h"
#include "lpt.h"
#include "mouse_serial.h"
#include "serial.h"
#include "fdc37c932fr.h"

static int fdc37c932fr_locked;
static int fdc37c932fr_curreg = 0;
static uint8_t fdc37c932fr_regs[48];
static uint8_t fdc37c932fr_ld_regs[10][256];

static uint8_t tries;

static uint16_t ld0_valid_ports[2] = {0x3F0, 0x370};
static uint16_t ld1_valid_ports[2] = {0x1F0, 0x170};
static uint16_t ld1_valid_ports2[2] = {0x3F6, 0x376};
static uint16_t ld2_valid_ports[2] = {0x170, 0x1F0};
static uint16_t ld2_valid_ports2[2] = {0x376, 0x3F6};
static uint16_t ld3_valid_ports[3] = {0x3BC, 0x378, 0x278};
static uint16_t ld4_valid_ports[9] = {0x3F8, 0x2F8, 0x338, 0x3E8, 0x2E8, 0x220, 0x238, 0x2E0, 0x228};
static uint16_t ld5_valid_ports[9] = {0x3F8, 0x2F8, 0x338, 0x3E8, 0x2E8, 0x220, 0x238, 0x2E0, 0x228};
static uint16_t ld5_valid_ports2[9] = {0x3F8, 0x2F8, 0x338, 0x3E8, 0x2E8, 0x220, 0x238, 0x2E0, 0x228};

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
	uint16_t r0 = fdc37c932fr_ld_regs[ld][0x60];
	uint16_t r1 = fdc37c932fr_ld_regs[ld][0x61];

	uint16_t p = (r0 << 8) + r1;

	switch(ld)
	{
		case 0:
			p &= 0xFF8;
			if ((p < 0x100) || (p > 0xFF8))  p = 0x3F0;
			if (!(is_in_array(ld0_valid_ports, 2, p)))  p = 0x3F0;
			break;
		case 1:
			p &= 0xFF8;
			if ((p < 0x100) || (p > 0xFF8))  p = 0x1F0;
			if (!(is_in_array(ld1_valid_ports, 2, p)))  p = 0x1F0;
			break;
		case 2:
			p &= 0xFF8;
			if ((p < 0x100) || (p > 0xFF8))  p = 0x170;
			if (!(is_in_array(ld2_valid_ports, 2, p)))  p = 0x170;
			break;
		case 3:
			p &= 0xFF8;
			if ((p < 0x100) || (p > 0xFF8))  p = 0x378;
			if (!(is_in_array(ld3_valid_ports, 3, p)))  p = 0x378;
			break;
		case 4:
			p &= 0xFF8;
			if ((p < 0x100) || (p > 0xFF8))  p = 0x3F8;
			if (!(is_in_array(ld4_valid_ports, 9, p)))  p = 0x3F8;
			break;
		case 5:
			p &= 0xFF8;
			if ((p < 0x100) || (p > 0xFF8))  p = 0x2F8;
			if (!(is_in_array(ld5_valid_ports, 9, p)))  p = 0x2F8;
			break;
	}

	fdc37c932fr_ld_regs[ld][0x60] = (p >> 8);
	fdc37c932fr_ld_regs[ld][0x61] = (p & 0xFF);

	return p;
}

uint16_t make_port2(uint8_t ld)
{
	uint16_t r0 = fdc37c932fr_ld_regs[ld][0x62];
	uint16_t r1 = fdc37c932fr_ld_regs[ld][0x63];

	uint16_t p = (r0 << 8) + r1;

	switch(ld)
	{
		case 1:
			p &= 0xFFF;
			if ((p < 0x100) || (p > 0xFF8))  p = 0x3F6;
			if (!(is_in_array(ld1_valid_ports2, 2, p)))  p = 0x3F6;
			break;
		case 2:
			p &= 0xFFF;
			if ((p < 0x100) || (p > 0xFF8))  p = 0x376;
			if (!(is_in_array(ld2_valid_ports2, 2, p)))  p = 0x376;
			break;
		case 5:
			p &= 0xFF8;
			if ((p < 0x100) || (p > 0xFF8))  p = 0x3E8;
			if (!(is_in_array(ld5_valid_ports2, 9, p)))  p = 0x3E8;
			break;
	}

	fdc37c932fr_ld_regs[ld][0x62] = (p >> 8);
	fdc37c932fr_ld_regs[ld][0x63] = (p & 0xFF);

	return p;
}

void fdc37c932fr_write(uint16_t port, uint8_t val, void *priv)
{
	uint8_t index = (port & 1) ? 0 : 1;
	uint8_t valxor = 0;
	uint16_t ld_port = 0;
	uint16_t ld_port2 = 0;
        int temp;
        pclog("fdc37c932fr_write : port=%04x reg %02X = %02X locked=%i\n", port, fdc37c932fr_curreg, val, fdc37c932fr_locked);

	if (index)
	{
		if ((val == 0x55) && !fdc37c932fr_locked)
		{
			if (tries)
			{
				fdc37c932fr_locked = 1;
				tries = 0;
			}
			else
			{
				tries++;
			}
		}
		else
		{
			if (fdc37c932fr_locked)
			{
				if (val == 0xaa)
				{
					fdc37c932fr_locked = 0;
					return;
				}
				fdc37c932fr_curreg = val;
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
		if (fdc37c932fr_locked)
		{
			if (fdc37c932fr_curreg < 48)
			{
				valxor = val ^ fdc37c932fr_regs[fdc37c932fr_curreg];
				fdc37c932fr_regs[fdc37c932fr_curreg] = val;
			}
			else
			{
				valxor = val ^ fdc37c932fr_ld_regs[fdc37c932fr_regs[7]][fdc37c932fr_curreg];
				if ((fdc37c932fr_curreg & 0xF0 == 0x70) && (fdc37c932fr_regs[7] < 4))  return;
				/* Block writes to IDE configuration. */
				if (fdc37c932fr_regs[7] == 1)  return;
				if (fdc37c932fr_regs[7] == 2)  return;
				if (fdc37c932fr_regs[7] > 5)  return;
				fdc37c932fr_ld_regs[fdc37c932fr_regs[7]][fdc37c932fr_curreg] = val;
				goto process_value;
			}
		}
	}
	return;

process_value:
	switch(fdc37c932fr_regs[7])
	{
		case 0:
			/* FDD */
			switch(fdc37c932fr_curreg)
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
					if (valxor && fdc37c932fr_ld_regs[0][0x30])
					{
						fdc_remove();
						ld_port = make_port(0);
						fdc37c932fr_ld_regs[0][0x60] = make_port(0) >> 8;
						fdc37c932fr_ld_regs[0][0x61] = make_port(0) & 0xFF;
						fdc_add_ex(ld_port, 1);
					}
					break;
				case 0xF0:
					if (valxor & 1)  fdc_os2 = (val & 1) ? 1 : 0;
					if (valxor & 0x10)  fdc_setswap((val & 0x10) >> 4);
					break;
				case 0xF1:
					if (valxor & 0xC)  densel_force = (val & 0xC) >> 2;
					if (valxor & 0x10)  densel_polarity_mid[0] = (val & 0x10) ? 0 : 1;
					if (valxor & 0x20)  densel_polarity_mid[1] = (val & 0x20) ? 0 : 1;
					break;
				case 0xF4:
					if (valxor & 0x18)  drt[0] = (val & 0x18) >> 3;
					break;
				case 0xF5:
					if (valxor & 0x18)  drt[1] = (val & 0x18) >> 3;
					break;
			}
			break;
		case 1:
			pclog("IDE 1 called\n");
			/* IDE 1 */
			switch(fdc37c932fr_curreg)
			{
				case 0x30:
					/* Activate */
					if (valxor)
					{
						if (!val)
							ide_pri_disable();
						else
						{
							ld_port = make_port(1);
							ld_port2 = make_port2(1);
							pclog("IDE 1 ports: %04X, %04X\n", ld_port, ld_port2);
							ide_pri_enable_custom(ld_port, ld_port2);
						}
					}
					break;
				case 0x60:
				case 0x61:
				case 0x62:
				case 0x63:
					if (valxor && fdc37c932fr_ld_regs[1][0x30])
					{
						ide_pri_disable();
						ld_port = make_port(1);
						ld_port2 = make_port2(1);
						pclog("IDE 1 ports: %04X, %04X\n", ld_port, ld_port2);
						ide_pri_enable_custom(ld_port, ld_port2);
					}
					break;
			}
			break;
		case 2:
			pclog("IDE 2 called\n");
			/* IDE 2 */
			switch(fdc37c932fr_curreg)
			{
				case 0x30:
					/* Activate */
					if (valxor)
					{
						if (!val)
							ide_sec_disable();
						else
						{
							ld_port = make_port(2);
							ld_port2 = make_port2(2);
							pclog("IDE 2 ports: %04X, %04X\n", ld_port, ld_port2);
							ide_sec_enable_custom(ld_port, ld_port2);
						}
					}
					break;
				case 0x60:
				case 0x61:
				case 0x62:
				case 0x63:
					if (valxor && fdc37c932fr_ld_regs[2][0x30])
					{
						ide_sec_disable();
						ld_port = make_port(2);
						ld_port2 = make_port2(2);
						pclog("IDE 2 ports: %04X, %04X\n", ld_port, ld_port2);
						ide_sec_enable_custom(ld_port, ld_port2);
					}
					break;
			}
			break;
		case 3:
			/* Parallel port */
			switch(fdc37c932fr_curreg)
			{
				case 0x30:
					/* Activate */
					if (valxor)
					{
						if (!val)
							lpt1_remove();
						else
						{
							ld_port = make_port(3);
							lpt1_init(ld_port);
						}
					}
					break;
				case 0x60:
				case 0x61:
					if (valxor && fdc37c932fr_ld_regs[3][0x30])
					{
						lpt1_remove();
						ld_port = make_port(3);
						lpt1_init(ld_port);
					}
					break;
			}
			break;
		case 4:
			/* Serial port 1 */
			switch(fdc37c932fr_curreg)
			{
				case 0x30:
					/* Activate */
					if (valxor)
					{
						if (!val)
							serial1_remove();
						else
						{
							ld_port = make_port(4);
							serial1_set(ld_port, fdc37c932fr_ld_regs[4][0x70]);
							mouse_serial_init();
						}
					}
					break;
				case 0x60:
				case 0x61:
				case 0x70:
					if (valxor && fdc37c932fr_ld_regs[4][0x30])
					{
						ld_port = make_port(4);
						serial1_set(ld_port, fdc37c932fr_ld_regs[4][0x70]);
						mouse_serial_init();
					}
					break;
			}
			break;
		case 5:
			/* Serial port 2 */
			switch(fdc37c932fr_curreg)
			{
				case 0x30:
					/* Activate */
					if (valxor)
					{
						if (!val)
							serial2_remove();
						else
						{
							ld_port = make_port(5);
							serial2_set(ld_port, fdc37c932fr_ld_regs[5][0x70]);
						}
					}
					break;
				case 0x60:
				case 0x61:
				case 0x70:
					if (valxor && fdc37c932fr_ld_regs[5][0x30])
					{
						ld_port = make_port(5);
						serial2_set(ld_port, fdc37c932fr_ld_regs[5][0x70]);
					}
					break;
			}
			break;
	}
}

uint8_t fdc37c932fr_read(uint16_t port, void *priv)
{
        pclog("fdc37c932fr_read : port=%04x reg %02X locked=%i\n", port, fdc37c932fr_curreg, fdc37c932fr_locked);
	uint8_t index = (port & 1) ? 0 : 1;

	if (!fdc37c932fr_locked)
	{
		return fdc_read(port, priv);
	}

	if (index)
		return fdc37c932fr_curreg;
	else
	{
		if (fdc37c932fr_curreg < 0x30)
		{
			pclog("0x03F1: %02X\n", fdc37c932fr_regs[fdc37c932fr_curreg]);
			return fdc37c932fr_regs[fdc37c932fr_curreg];
		}
		else
		{
			pclog("0x03F1 (CD=%02X): %02X\n", fdc37c932fr_regs[7], fdc37c932fr_ld_regs[fdc37c932fr_regs[7]][fdc37c932fr_curreg]);
			return fdc37c932fr_ld_regs[fdc37c932fr_regs[7]][fdc37c932fr_curreg];
		}
	}
}

uint8_t ap5t_read(uint16_t port, void *priv)
{
	return 0xfe;
}

void fdc37c932fr_init()
{
	int i = 0;

	fdc_remove_stab();

	lpt2_remove();

	fdc37c932fr_regs[3] = 3;
	fdc37c932fr_regs[0x20] = 3;
	fdc37c932fr_regs[0x21] = 1;
	fdc37c932fr_regs[0x24] = 4;
	fdc37c932fr_regs[0x26] = 0xF0;
	fdc37c932fr_regs[0x27] = 3;

	for (i = 0; i < 10; i++)
	{
		memset(fdc37c932fr_ld_regs[i], 0, 256);
	}

	/* Logical device 0: FDD */
	fdc37c932fr_ld_regs[0][0x30] = 1;
	fdc37c932fr_ld_regs[0][0x60] = 3;
	fdc37c932fr_ld_regs[0][0x61] = 0xF0;
	fdc37c932fr_ld_regs[0][0x70] = 6;
	fdc37c932fr_ld_regs[0][0x74] = 2;
	fdc37c932fr_ld_regs[0][0xF0] = 0xE;
	fdc37c932fr_ld_regs[0][0xF2] = 0xFF;

	/* Logical device 1: IDE1 */
	fdc37c932fr_ld_regs[1][0x30] = 1;
	fdc37c932fr_ld_regs[1][0x60] = 1;
	fdc37c932fr_ld_regs[1][0x61] = 0xF0;
	fdc37c932fr_ld_regs[1][0x62] = 3;
	fdc37c932fr_ld_regs[1][0x63] = 0xF6;
	fdc37c932fr_ld_regs[1][0x70] = 0xE;
	fdc37c932fr_ld_regs[1][0xF0] = 0xC;

	/* Logical device 2: IDE2 */
	fdc37c932fr_ld_regs[2][0x30] = 1;
	fdc37c932fr_ld_regs[2][0x60] = 1;
	fdc37c932fr_ld_regs[2][0x61] = 0x70;
	fdc37c932fr_ld_regs[2][0x62] = 3;
	fdc37c932fr_ld_regs[2][0x63] = 0x76;
	fdc37c932fr_ld_regs[2][0x70] = 0xF;

	/* Logical device 3: Parallel Port */
	fdc37c932fr_ld_regs[3][0x30] = 1;
	fdc37c932fr_ld_regs[3][0x60] = 3;
	fdc37c932fr_ld_regs[3][0x61] = 0x78;
	fdc37c932fr_ld_regs[3][0x70] = 7;
	fdc37c932fr_ld_regs[3][0x74] = 4;
	fdc37c932fr_ld_regs[3][0xF0] = 0x3C;

	/* Logical device 4: Serial Port 1 */
	fdc37c932fr_ld_regs[4][0x30] = 1;
	fdc37c932fr_ld_regs[4][0x60] = 3;
	fdc37c932fr_ld_regs[4][0x61] = 0xf8;
	fdc37c932fr_ld_regs[4][0x70] = 4;
	fdc37c932fr_ld_regs[4][0xF0] = 3;

	/* Logical device 5: Serial Port 2 */
	fdc37c932fr_ld_regs[5][0x30] = 1;
	fdc37c932fr_ld_regs[5][0x60] = 2;
	fdc37c932fr_ld_regs[5][0x61] = 0xf8;
	fdc37c932fr_ld_regs[5][0x70] = 3;
	fdc37c932fr_ld_regs[5][0x74] = 4;
	fdc37c932fr_ld_regs[5][0xF1] = 2;
	fdc37c932fr_ld_regs[5][0xF2] = 3;

	/* Logical device 6: RTC */
	fdc37c932fr_ld_regs[6][0x63] = 0x70;
	fdc37c932fr_ld_regs[6][0xF4] = 3;

	/* Logical device 7: Keyboard */
	fdc37c932fr_ld_regs[7][0x30] = 1;
	fdc37c932fr_ld_regs[7][0x61] = 0x60;
	fdc37c932fr_ld_regs[7][0x70] = 1;

	/* Logical device 8: AUX I/O */

	/* Logical device 9: ACCESS.bus */

	densel_polarity_mid[0] = 1;
	densel_polarity_mid[1] = 1;
	densel_force = 0;
	fdc_setswap(0);
        io_sethandler(0x3f0, 0x0002, fdc37c932fr_read, NULL, NULL, fdc37c932fr_write, NULL, NULL,  NULL);
        io_sethandler(0x92, 0x0001, ap5t_read, NULL, NULL, NULL, NULL, NULL,  NULL);
        fdc37c932fr_locked = 0;
}
