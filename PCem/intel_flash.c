#include <stdlib.h>
#include "ibm.h"
#include "device.h"
#include "mem.h"

enum
{
        CMD_READ_ARRAY = 0xff,
        CMD_IID = 0x90,
        CMD_READ_STATUS = 0x70,
        CMD_CLEAR_STATUS = 0x50,
        CMD_ERASE_SETUP = 0x20,
        CMD_ERASE_CONFIRM = 0xd0,
        CMD_ERASE_SUSPEND = 0xb0,
        CMD_PROGRAM_SETUP = 0x40
};

typedef struct flash_t
{
        uint8_t command, status;
        mem_mapping_t read_mapping, write_mapping;
} flash_t;

static flash_t flash;

static mem_mapping_t ext_read_mapping, ext_write_mapping;

static char *path;
static char *fn;

static uint8_t flash_read(uint32_t addr, void *p)
{
        flash_t *flash = (flash_t *)p;
	int is_bxb = ((romset == ROM_ACERV35N) || (romset == ROM_ACERV12LC));
#ifndef RELEASE_BUILD
        pclog("flash_read : addr=%08x command=%02x %04x:%08x\n", addr, flash->command, CS, pc);
#endif
        switch (flash->command)
        {
                case CMD_IID:
                if (addr & 1)
                        return (is_bxb ? 0x95 : 0x94);
                return 0x89;
                
                default:
                return flash->status;
        }
}

static void flash_write(uint32_t addr, uint8_t val, void *p)
{
        flash_t *flash = (flash_t *)p;
	int is_ami = ((romset == ROM_REVENGE) || (romset == ROM_PLATO) || (romset == ROM_ENDEAVOR));
	int has_dmi = ((romset == ROM_430HX) || (romset == ROM_430VX) || (romset == ROM_430TX) || (romset == ROM_440FX));
	int is_bxb = ((romset == ROM_ACERV35N) || (romset == ROM_ACERV12LC));
#ifndef RELEASE_BUILD
        pclog("flash_write : addr=%08x val=%02x command=%02x %04x:%08x\n", addr, val, flash->command, CS, pc);        
#endif
        switch (flash->command)
        {
                case CMD_ERASE_SETUP:
                if (val == CMD_ERASE_CONFIRM)
                {
#ifndef RELEASE_BUILD
                        pclog("flash_write: erase %05x\n", addr & 0x1ffff);
#endif
			if (is_ami)
			{
	                        if ((addr & 0x1f000) == 0x0d000)
	                                memset(&rom[0x0d000], 0xff, 0x1000);
	                        if ((addr & 0x1f000) == 0x0c000)
	                                memset(&rom[0x0c000], 0xff, 0x1000);
			}
			else
			{
				if (is_bxb)
				{
		                        if ((addr & 0x1f000) == 0x2000)
       			                        memset(&rom[0x2000], 0xff, 0x1000);
		                        if ((addr & 0x1f000) == 0x3000)
        		                        memset(&rom[0x3000], 0xff, 0x1000);
	        	                if ((addr & 0x1f000) > 0x3fff)
        	        	        {
                	        	        memset(rom + 0x4000, 0xff, 0x1c000);
	                        	}
				}
				else
				{
					if (has_dmi)
					{
			                        if ((addr & 0x1f000) == 0x1c000)
        			                        memset(&rom[0x1c000], 0xff, 0x1000);
					}
		                        if ((addr & 0x1f000) == 0x1d000)
        		                        memset(&rom[0x1d000], 0xff, 0x1000);
	        	                if ((addr & 0x1f000) < 0x1c000)
        	        	        {
                	        	        memset(rom, 0xff, 0x1c000);
	                        	}
				}
			}
       	                flash->status = 0x80;
                }
                flash->command = CMD_READ_STATUS;
                break;
                
                case CMD_PROGRAM_SETUP:
#ifndef RELEASE_BUILD
                pclog("flash_write: program %05x %02x\n", addr & 0x1ffff, val);
#endif
		if (is_ami)
		{
			if ((addr & 0x1e000) == 0xc000)
			{
	                        rom[addr & 0x1ffff] = val;
			}
		}
		else
		{
			if (is_bxb)
			{
				if ((addr & 0x1f000) == 0x2000)
				{
	        	                rom[addr & 0x1ffff] = val;
				}
				if ((addr & 0x1f000) == 0x3000)
				{
	        	                rom[addr & 0x1ffff] = val;
				}
			}
			else
			{
				if (has_dmi)
				{
					if ((addr & 0x1f000) == 0x1c000)
					{
		        	                rom[addr & 0x1ffff] = val;
					}
				}
				if ((addr & 0x1f000) == 0x1d000)
				{
	        	                rom[addr & 0x1ffff] = val;
				}
			}
		}
                flash->command = CMD_READ_STATUS;
                flash->status = 0x80;
                break;
                
                default:
                flash->command = val;
                switch (val)
                {
                        case CMD_CLEAR_STATUS:
                        flash->status = 0x80;
                        break;
                                
                        case CMD_IID:
                        case CMD_READ_STATUS:
                        mem_mapping_disable(&bios_mapping[0]);
                        mem_mapping_disable(&bios_mapping[1]);
                        mem_mapping_disable(&bios_mapping[2]);
                        mem_mapping_disable(&bios_mapping[3]);
                        mem_mapping_disable(&bios_mapping[4]);
                        mem_mapping_disable(&bios_mapping[5]);
                        mem_mapping_disable(&bios_mapping[6]);
                        mem_mapping_disable(&bios_mapping[7]);

                        mem_mapping_disable(&bios_high_mapping[0]);
                        mem_mapping_disable(&bios_high_mapping[1]);
                        mem_mapping_disable(&bios_high_mapping[2]);
                        mem_mapping_disable(&bios_high_mapping[3]);
                        mem_mapping_disable(&bios_high_mapping[4]);
                        mem_mapping_disable(&bios_high_mapping[5]);
                        mem_mapping_disable(&bios_high_mapping[6]);
                        mem_mapping_disable(&bios_high_mapping[7]);

                        mem_mapping_enable(&flash->read_mapping);                        
                        mem_mapping_enable(&ext_read_mapping);                        
                        break;
                        
                        case CMD_READ_ARRAY:
			if ((is_ami || is_bxb) || (romset == ROM_430HX) || (romset == ROM_430TX))
			{
                        	mem_mapping_enable(&bios_mapping[0]);
                	        mem_mapping_enable(&bios_mapping[1]);
        	                mem_mapping_enable(&bios_mapping[2]);
	                        mem_mapping_enable(&bios_mapping[3]);
			}
			else
			{
                        	mem_mapping_disable(&bios_mapping[0]);
                	        mem_mapping_disable(&bios_mapping[1]);
        	                mem_mapping_disable(&bios_mapping[2]);
	                        mem_mapping_disable(&bios_mapping[3]);
			}
                        mem_mapping_enable(&bios_mapping[4]);
                        mem_mapping_enable(&bios_mapping[5]);
                        mem_mapping_enable(&bios_mapping[6]);
                        mem_mapping_enable(&bios_mapping[7]);

			if (!is_ami)
			{
	                       	mem_mapping_enable(&bios_high_mapping[0]);
        	       	        mem_mapping_enable(&bios_high_mapping[1]);
       	        	        mem_mapping_enable(&bios_high_mapping[2]);
                        	mem_mapping_enable(&bios_high_mapping[3]);
	                        mem_mapping_enable(&bios_high_mapping[4]);
        	                mem_mapping_enable(&bios_high_mapping[5]);
                	        mem_mapping_enable(&bios_high_mapping[6]);
                        	mem_mapping_enable(&bios_high_mapping[7]);
			}

                        mem_mapping_disable(&flash->read_mapping);
                        mem_mapping_disable(&ext_read_mapping);
                        break;
                }
        }
}

static void configure_path()
{
	switch(romset)
	{
		case ROM_REVENGE:
			path = "roms/revenge/";
			break;
		case ROM_430LX:
			path = "roms/430lx/";
			break;
		case ROM_ACERV12LC:
			path = "roms/acerv12lc/";
			break;
		case ROM_PLATO:
			path = "roms/plato/";
			break;
		case ROM_430NX:
			path = "roms/430nx/";
			break;
		case ROM_ENDEAVOR:
			path = "roms/endeavor/";
			break;
#ifdef BROKEN_CHIPSETS
		case ROM_APOLLO:
			path = "roms/apollo/";
			break;
#endif
		case ROM_430FX:
			path = "roms/430fx/";
			break;
		case ROM_430HX:
			path = "roms/430hx/";
			break;
		case ROM_ACERV35N:
			path = "roms/acerv35n/";
			break;
		case ROM_430VX:
			path = "roms/430vx/";
			break;
		case ROM_430TX:
			path = "roms/430tx/";
			break;
		case ROM_440FX:
			path = "roms/440fx/";
			break;
	}
}

void flash_1mbit_readfiles()
{
        FILE *f;
	int is_ami = ((romset == ROM_REVENGE) || (romset == ROM_PLATO) || (romset == ROM_ENDEAVOR));
	int has_dmi = ((romset == ROM_430HX) || (romset == ROM_430VX) || (romset == ROM_430TX) || (romset == ROM_440FX));
	int is_bxb = ((romset == ROM_ACERV35N) || (romset == ROM_ACERV12LC));

        memset(&rom[is_ami ? 0xd000 : (is_bxb ? 0x2000 : 0x1d000)], 0xFF, 0x1000);

	configure_path();
	fn = (char *) malloc(255);
	strcpy(fn, path);
	strcat(fn, "escd.bin");
        f = romfopen(fn, "rb");
        if (f)
        {
                fread(&rom[is_ami ? 0xd000 : (is_bxb ? 0x2000 : 0x1d000)], 0x1000, 1, f);
                fclose(f);
        }
	if (has_dmi || is_bxb)
	{
		strcpy(fn, path);
		strcat(fn, "dmi.bin");
	        f = romfopen(fn, "rb");
	        if (f)
	        {
	                fread(&rom[is_bxb ? 0x3000 : 0x1c000], 0x1000, 1, f);
	                fclose(f);
	        }
	}
}

int closed = 1;

void *intel_flash_init()
{
        FILE *f;
        flash_t *flash = malloc(sizeof(flash_t));
	int is_ami = ((romset == ROM_REVENGE) || (romset == ROM_PLATO) || (romset == ROM_ENDEAVOR));
	int has_dmi = ((romset == ROM_430HX) || (romset == ROM_430VX) || (romset == ROM_430TX) || (romset == ROM_440FX));
	int is_bxb = ((romset == ROM_ACERV35N) || (romset == ROM_ACERV12LC));
        memset(flash, 0, sizeof(flash_t));

        mem_mapping_add(&flash->read_mapping,
       	            0xe0000, 
               	    0x20000,
       	            flash_read, NULL, NULL,
                    NULL, NULL, NULL,
               	    NULL, MEM_MAPPING_EXTERNAL, (void *)flash);
        mem_mapping_add(&flash->write_mapping,
                    0xe0000, 
       	            0x20000,
       	            NULL, NULL, NULL,
                    flash_write, NULL, NULL,
       	            NULL, MEM_MAPPING_EXTERNAL, (void *)flash);
        mem_mapping_add(&ext_read_mapping,
       	            0xfffe0000, 
               	    0x20000,
       	            flash_read, NULL, NULL,
                    NULL, NULL, NULL,
               	    NULL, MEM_MAPPING_EXTERNAL, (void *)flash);
        mem_mapping_add(&ext_write_mapping,
                    0xfffe0000, 
       	            0x20000,
       	            NULL, NULL, NULL,
                    flash_write, NULL, NULL,
       	            NULL, MEM_MAPPING_EXTERNAL, (void *)flash);
        mem_mapping_disable(&flash->read_mapping);
        mem_mapping_disable(&ext_read_mapping);

	if (!is_ami)
	{
		/* Non-AMI BIOS'es talk to the flash in high RAM. */
		if (romset != ROM_440FX)
		{
			mem_mapping_disable(&flash->write_mapping);
		}
		else
		{
			mem_mapping_set_addr(&flash->write_mapping, 0xf0000, 0x10000);
		}
		mem_mapping_enable(&ext_write_mapping);
	}
	else
	{
		/* AMI BIOS'es talk to the flash in low RAM. */
		mem_mapping_enable(&flash->write_mapping);
		mem_mapping_set_addr(&flash->write_mapping, 0xe0000, 0x20000);
		mem_mapping_disable(&ext_write_mapping);
	}

        flash->command = CMD_READ_ARRAY;
        flash->status = 0;
        
        memset(&rom[is_ami ? 0xd000 : (is_bxb ? 0x2000 : 0x1d000)], 0xFF, 0x1000);

	configure_path();
	fn = (char *) malloc(255);
	strcpy(fn, path);
	strcat(fn, "escd.bin");
        f = romfopen(fn, "rb");
        if (f)
        {
                fread(&rom[is_ami ? 0xd000 : (is_bxb ? 0x2000 : 0x1d000)], 0x1000, 1, f);
                fclose(f);
        }
	if (has_dmi || is_bxb)
	{
		strcpy(fn, path);
		strcat(fn, "dmi.bin");
	        f = romfopen(fn, "rb");
	        if (f)
	        {
	                fread(&rom[is_bxb ? 0x3000 : 0x1c000], 0x1000, 1, f);
	                fclose(f);
	        }
	}

	closed = 0;
        
        return flash;
}

void intel_flash_close(void *p)
{
        FILE *f;
        flash_t *flash = (flash_t *)p;
	int is_ami = ((romset == ROM_REVENGE) || (romset == ROM_PLATO) || (romset == ROM_ENDEAVOR));
	int has_dmi = ((romset == ROM_430HX) || (romset == ROM_430VX) || (romset == ROM_430TX) || (romset == ROM_440FX));
	int is_bxb = ((romset == ROM_ACERV35N) || (romset == ROM_ACERV12LC));

	if (closed)  return;

	configure_path();
	fn = (char *) malloc(255);
	strcpy(fn, path);
	strcat(fn, "escd.bin");
        f = romfopen(fn, "wb");
        fwrite(&rom[is_ami ? 0xd000 : (is_bxb ? 0x2000 : 0x1d000)], 0x1000, 1, f);
        fclose(f);
#if 0
        f = romfopen("romkek.$$$", "wb");
        fwrite(&rom[0], 0x20000, 1, f);
        fclose(f);
#endif
	if (has_dmi || is_bxb)
	{
		strcpy(fn, path);
		strcat(fn, "dmi.bin");
	        f = romfopen(fn, "wb");
	        if (f)
	        {
	                fwrite(&rom[is_bxb ? 0x3000 : 0x1c000], 0x1000, 1, f);
	                fclose(f);
	        }
	}

	closed = 1;

        free(flash);
}

device_t intel_flash_device =
{
        "Intel 28F001BXT Flash BIOS",
        0,
        intel_flash_init,
        intel_flash_close,
        NULL,
        NULL,
        NULL,
        NULL,
        NULL
};

device_t intel_flash_b_device =
{
        "Intel 28F001BXB Flash BIOS",
        0,
        intel_flash_init,
        intel_flash_close,
        NULL,
        NULL,
        NULL,
        NULL,
        NULL
};
