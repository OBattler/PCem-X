/*
	National Semiconductors PC87306 Super I/O Chip
	Used by Intel Advanced/EV
*/

#include "ibm.h"

#include "fdc.h"
#include "io.h"
#include "lpt.h"
#include "mouse_serial.h"
#include "serial.h"
#include "pc87306.h"

static int pc87306_locked;
static int pc87306_curreg;
static uint8_t pc87306_regs[28];
static uint8_t tries;

void pc87306_write(uint16_t port, uint8_t val, void *priv)
{
	uint8_t index = (port & 1) ? 0 : 1;
        int temp;
        // pclog("pc87306_write : port=%04x reg %02X = %02X locked=%i\n", port, pc87306_curreg, val, pc87306_locked);

	if (index)
	{
		pc87306_curreg = val;
		tries = 0;
		return;
	}
	else
	{
		if (tries)
		{
			if (pc87306_curreg < 28)  pc87306_regs[pc87306_curreg] = val;
			tries = 0;
			goto process_value;
		}
		else
		{
			tries++;
			return;
		}
	}

process_value:
	switch(pc87306_curreg)
	{
		case 0:
			fdc_remove();
			if (val & 8)  fdc_add_ex(val & 0x20 ? 0x370 : 0x3f0, 0);
			lpt1_remove();
			lpt2_remove();
                        temp = pc87306_regs[1] & 3;
                        switch (temp)
                        {
                                case 0: lpt1_init(0x378); break;
                                case 1: lpt1_init(0x3bc); break;
                                case 2: lpt1_init(0x278); break;
                        }
			break;
		case 1:
                        temp = val & 3;
                        switch (temp)
                        {
                                case 0: lpt1_init(0x378); break;
                                case 1: lpt1_init(0x3bc); break;
                                case 2: lpt1_init(0x278); break;
                        }
			temp = (val >> 2) & 3;
                        switch (temp)
                        {
                                case 0: serial1_set(0x3f8, 4); break;
                                case 1: serial1_set(0x2f8, 4); break;
                                case 2:
					switch ((val >> 6) & 3)
					{
						case 0: serial1_set(0x3e8, 4); break;
						case 1: serial1_set(0x338, 4); break;
						case 2: serial1_set(0x2e8, 4); break;
						case 3: serial1_set(0x220, 4); break;
					}
					break;
				case 3:
					switch ((val >> 6) & 3)
					{
						case 0: serial1_set(0x2e8, 4); break;
						case 1: serial1_set(0x238, 4); break;
						case 2: serial1_set(0x2e0, 4); break;
						case 3: serial1_set(0x228, 4); break;
					}
					break;
                        }
			temp = (val >> 2) & 4;
                        switch (temp)
                        {
                                case 0: serial2_set(0x3f8, 3); break;
                                case 1: serial2_set(0x2f8, 3); break;
                                case 2:
					switch ((val >> 6) & 3)
					{
						case 0: serial2_set(0x3e8, 3); break;
						case 1: serial2_set(0x338, 3); break;
						case 2: serial2_set(0x2e8, 3); break;
						case 3: serial2_set(0x220, 3); break;
					}
					break;
				case 3:
					switch ((val >> 6) & 3)
					{
						case 0: serial2_set(0x2e8, 3); break;
						case 1: serial2_set(0x238, 3); break;
						case 2: serial2_set(0x2e0, 3); break;
						case 3: serial2_set(0x228, 3); break;
					}
					break;
                        }                        
                        mouse_serial_init();
			break;
		case 9:
			pclog("Setting DENSEL polarity to: %i (before: %i)\n", (val & 0x40 ? 1 : 0), densel_polarity);
			densel_polarity = val & 0x40 ? 1 : 0;
			break;
	}
}

uint8_t pc87306_read(uint16_t port, void *priv)
{
        // pclog("pc87306_read : port=%04x reg %02X locked=%i\n", port, pc87306_curreg, pc87306_locked);
	uint8_t index = (port & 1) ? 0 : 1;

	if (index)
		return pc87306_curreg;
	else
	{
	        if (pc87306_curreg >= 28)
			return 0xff;
		else
			return pc87306_regs[pc87306_curreg];
	}
}

void pc87306_init()
{
	pc87306_regs[9] = 0xFF;
	/*
		0 = 360 rpm @ 500 kbps for 3.5"
		1 = Default, 300 rpm @ 500,300,250,1000 kbps for 3.5"
	*/
	densel_polarity = 1;
	fdc_setswap(0);
        io_sethandler(0x02e, 0x0002, pc87306_read, NULL, NULL, pc87306_write, NULL, NULL,  NULL);
}
