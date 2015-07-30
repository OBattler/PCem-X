/*
	SMSC SMC FDC37C665 Super I/O Chip
	Used by Batman's Revenge
*/

#include "ibm.h"

#include "fdc.h"
#include "io.h"
#include "lpt.h"
#include "mouse_serial.h"
#include "serial.h"
#include "fdc37c665.h"

static int fdc37c665_locked;
static int fdc37c665_curreg = 0;
static uint8_t fdc37c665_regs[16];
static uint8_t tries;

void fdc37c665_write(uint16_t port, uint8_t val, void *priv)
{
	uint8_t index = (port & 1) ? 0 : 1;
	uint8_t valxor = 0;
        int temp;
        pclog("fdc37c665_write : port=%04x reg %02X = %02X locked=%i\n", port, fdc37c665_curreg, val, fdc37c665_locked);

	if (index)
	{
		if ((val == 0x55) && !fdc37c665_locked)
		{
			if (tries)
			{
				fdc37c665_locked = 1;
				tries = 0;
			}
			else
			{
				tries++;
			}
		}
		else
		{
			if (fdc37c665_locked)
			{
				if (val < 16)  fdc37c665_curreg = val;
				if (val == 0xaa)  fdc37c665_locked = 0;
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
		if (fdc37c665_locked)
		{
			valxor = val ^ fdc37c665_regs[fdc37c665_curreg];
			fdc37c665_regs[fdc37c665_curreg] = val;
			goto process_value;
		}
	}
	return;

process_value:
	switch(fdc37c665_curreg)
	{
		case 0:
			if (valxor & 0x18)
			{
				fdc_remove();
				if ((val & 0x18) == 0x18)  fdc_add_ex((fdc37c665_regs[5] & 1) ? 0x370 : 0x3f0, 1);
			}
			break;
		case 1:
			if (valxor & 3)
			{
				lpt1_remove();
				lpt2_remove();
        	                temp = val & 3;
                	        switch (temp)
	                        {
	                                case 1: lpt1_init(0x3bc); break;
        	                        case 2: lpt1_init(0x378); break;
                	                case 3: lpt1_init(0x278); break;
                        	}
			}
			if (valxor & 0x60)  goto serial_set;
			break;
		case 2:
serial_set:
			if (!(fdc37c665_regs[2] & 4))
				serial1_remove();
			else
			{
				temp = fdc37c665_regs[2] & 3;
        	                switch (temp)
                	        {
                        	        case 0: serial1_set(0x3f8, 4); break;
	                                case 1: serial1_set(0x2f8, 4); break;
        	                        case 2:
						switch (fdc37c665_regs[1] & 0x60)
						{
							case 0x00: serial1_set(0x338, 4); break;
							case 0x20: serial1_set(0x3e8, 4); break;
							case 0x40: serial1_set(0x2e8, 4); break;
							case 0x60: serial1_set(0x220, 4); break;
						}
						break;
					case 3:
						switch (fdc37c665_regs[1] & 0x60)
						{
							case 0x00: serial1_set(0x238, 4); break;
							case 0x20: serial1_set(0x2e8, 4); break;
							case 0x40: serial1_set(0x2e0, 4); break;
							case 0x60: serial1_set(0x228, 4); break;
						}
						break;
	                        }
				if (romset == ROM_430FX)  mouse_serial_init();
			}
			if (!(fdc37c665_regs[2] & 0x40))
				serial2_remove();
			else
			{
				temp = fdc37c665_regs[2] & 0x30;
        	                switch (temp)
	                        {
                	                case 0x00: serial2_set(0x3f8, 3); break;
        	                        case 0x10: serial2_set(0x2f8, 3); break;
	                                case 0x20:
						switch (fdc37c665_regs[1] & 0x60)
						{
							case 0x00: serial2_set(0x338, 3); break;
							case 0x20: serial2_set(0x3e8, 3); break;
							case 0x40: serial2_set(0x2e8, 3); break;
							case 0x60: serial2_set(0x220, 3); break;
						}
						break;
					case 0x30:
						switch (fdc37c665_regs[1] & 0x60)
						{
							case 0x00: serial2_set(0x238, 3); break;
							case 0x20: serial2_set(0x2e8, 3); break;
							case 0x40: serial2_set(0x2e0, 3); break;
							case 0x60: serial2_set(0x228, 3); break;
						}
						break;
                	        }
			}
			break;
		case 3:
			if (valxor & 2)  fdc_os2 = (val & 2) ? 1 : 0;
			break;
		case 5:
			if (valxor & 1)
			{
				fdc_remove();
				if ((fdc37c665_regs[0] & 0x18) == 0x18)  fdc_add_ex((val & 1) ? 0x370 : 0x3f0, 1);
			}
			if (valxor & 0x18)  densel_force = (val & 0x18) >> 3;
			if (valxor & 0x20)  fdc_setswap((val & 0x20) >> 5);
			break;
	}
}

uint8_t fdc37c665_read(uint16_t port, void *priv)
{
        pclog("fdc37c665_read : port=%04x reg %02X locked=%i\n", port, fdc37c665_curreg, fdc37c665_locked);
	uint8_t index = (port & 1) ? 0 : 1;

	if (!fdc37c665_locked)
	{
		return fdc_read(port, priv);
	}

	if (index)
		return fdc37c665_curreg;
	else
	{
		pclog("0x03F1: %02X\n", fdc37c665_regs[fdc37c665_curreg]);
		return fdc37c665_regs[fdc37c665_curreg];
	}
}

void fdc37c665_init()
{
	fdc_remove_stab();
	/* Parallel port should be LPT 1, not LPT 2. */
	// fdc37c665_regs[0] = 0x3B;
	fdc37c665_regs[0] = 0x3A;
	fdc37c665_regs[1] = 0x9F;
	fdc37c665_regs[2] = 0xDC;
	fdc37c665_regs[3] = 0x78;
	fdc37c665_regs[4] = 0;
	fdc37c665_regs[5] = 0;
	fdc37c665_regs[6] = 0xFF;
	fdc37c665_regs[7] = 0;
	fdc37c665_regs[8] = 0;
	fdc37c665_regs[9] = 0;
	fdc37c665_regs[0xA] = 0;
	fdc37c665_regs[0xB] = 0;
	fdc37c665_regs[0xC] = 0;
	fdc37c665_regs[0xD] = 0x65;
	fdc37c665_regs[0xE] = 1;
	fdc37c665_regs[0xF] = 0;
	densel_polarity = 1;
	densel_force = 0;
	fdc_setswap(0);
	serial1_set(0x3f8, 4);
	serial2_set(0x2f8, 3);
        io_sethandler(0x3f0, 0x0002, fdc37c665_read, NULL, NULL, fdc37c665_write, NULL, NULL,  NULL);
        fdc37c665_locked = 0;
}
