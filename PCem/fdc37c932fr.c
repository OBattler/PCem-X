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
							ld_port = (fdc37c932fr_ld_regs[0][0x60] << 8) + fdc37c932fr_ld_regs[0][0x61];
							fdc_add_ex(ld_port, 1);
						}
					}
					break;
				case 0x60:
				case 0x61:
					if (valxor)
					{
						fdc_remove();
						ld_port = (fdc37c932fr_ld_regs[0][0x60] << 8) + fdc37c932fr_ld_regs[0][0x61];
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
							ld_port = (fdc37c932fr_ld_regs[1][0x60] << 8) + fdc37c932fr_ld_regs[1][0x61];
							ld_port2 = (fdc37c932fr_ld_regs[1][0x62] << 8) + fdc37c932fr_ld_regs[1][0x63];
							ide_pri_enable_custom(ld_port, ld_port2);
						}
					}
					break;
				case 0x60:
				case 0x61:
				case 0x62:
				case 0x63:
					if (valxor)
					{
						ide_pri_disable();
						ld_port = (fdc37c932fr_ld_regs[1][0x60] << 8) + fdc37c932fr_ld_regs[1][0x61];
						ld_port2 = (fdc37c932fr_ld_regs[1][0x62] << 8) + fdc37c932fr_ld_regs[1][0x63];
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
							ld_port = (fdc37c932fr_ld_regs[2][0x60] << 8) + fdc37c932fr_ld_regs[2][0x61];
							ld_port2 = (fdc37c932fr_ld_regs[2][0x62] << 8) + fdc37c932fr_ld_regs[2][0x63];
							ide_sec_enable_custom(ld_port, ld_port2);
						}
					}
					break;
				case 0x60:
				case 0x61:
				case 0x62:
				case 0x63:
					if (valxor)
					{
						ide_sec_disable();
						ld_port = (fdc37c932fr_ld_regs[2][0x60] << 8) + fdc37c932fr_ld_regs[2][0x61];
						ld_port2 = (fdc37c932fr_ld_regs[2][0x62] << 8) + fdc37c932fr_ld_regs[2][0x63];
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
							ld_port = (fdc37c932fr_ld_regs[3][0x60] << 8) + fdc37c932fr_ld_regs[3][0x61];
							lpt1_init(ld_port);
						}
					}
					break;
				case 0x60:
				case 0x61:
					if (valxor)
					{
						lpt1_remove();
						ld_port = (fdc37c932fr_ld_regs[3][0x60] << 8) + fdc37c932fr_ld_regs[3][0x61];
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
							ld_port = (fdc37c932fr_ld_regs[4][0x60] << 8) + fdc37c932fr_ld_regs[4][0x61];
							serial1_set(ld_port, fdc37c932fr_ld_regs[4][0x70]);
							mouse_serial_init();
						}
					}
					break;
				case 0x60:
				case 0x61:
				case 0x70:
					if (valxor)
					{
						ld_port = (fdc37c932fr_ld_regs[4][0x60] << 8) + fdc37c932fr_ld_regs[4][0x61];
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
							ld_port = (fdc37c932fr_ld_regs[5][0x60] << 8) + fdc37c932fr_ld_regs[5][0x61];
							serial2_set(ld_port, fdc37c932fr_ld_regs[5][0x70]);
						}
					}
					break;
				case 0x60:
				case 0x61:
				case 0x70:
					if (valxor)
					{
						ld_port = (fdc37c932fr_ld_regs[5][0x60] << 8) + fdc37c932fr_ld_regs[5][0x61];
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
        fdc37c932fr_locked = 0;
}
