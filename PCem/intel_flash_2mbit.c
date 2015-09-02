#ifdef BROKEN_CHIPSETS
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
        CMD_PROGRAM_SETUP = 0x40,
	CMD_PROGRAM_ALT_SETUP = 0x10
};

typedef struct flash_t
{
        uint8_t command, status;
        mem_mapping_t read_mapping, write_mapping;
} flash_t;

static flash_t flash;

static char *path;
static char *fn;

static uint8_t flash_read(uint32_t addr, void *p)
{
        flash_t *flash = (flash_t *)p;
//        pclog("flash_read : addr=%08x command=%02x %04x:%08x\n", addr, flash->command, CS, pc);
        switch (flash->command)
        {
                case CMD_IID:
                if (addr & 1)
                        return 0x94;
                return 0x89;
                
                default:
                return flash->status;
        }
}

static void flash_write(uint32_t addr, uint8_t val, void *p)
{
        flash_t *flash = (flash_t *)p;
//        pclog("flash_write : addr=%08x val=%02x command=%02x %04x:%08x\n", addr, val, flash->command, CS, pc);        
        switch (flash->command)
        {
                case CMD_ERASE_SETUP:
                if (val == CMD_ERASE_CONFIRM)
                {
//                        pclog("flash_write: erase %05x\n", addr & 0x1ffff);
                        if (((addr & 0x3f000) == 0x3a000) || ((addr & 0x3f000) == 0x3b000))
       	                        memset(&rom[0x3a000], 0xff, 0x2000);
                        if ((addr & 0x3f000) < 0x3a000 || (addr & 0x3f000) >= 0x3c000)
       	                {
               	                memset(rom, 0xff, 0x3a000);
                       	        memset(&rom[0x3c000], 0xff, 0x4000);
                        }
       	                flash->status = 0x80;
                }
                flash->command = CMD_READ_STATUS;
                break;
                
                case CMD_PROGRAM_SETUP:
                case CMD_PROGRAM_ALT_SETUP:
//                pclog("flash_write: program %05x %02x\n", addr & 0x1ffff, val);
                if ((addr & 0x1e000) != 0x0e000)
                        rom[addr & 0x1ffff] = val;
                flash->command = CMD_READ_STATUS;
                flash->status = 0x80;
                break;
                
                default:
                flash->command = val;
                switch (val)
                {
                        case CMD_CLEAR_STATUS:
                        flash->status = 0;
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
                       	mem_mapping_disable(&bios_mapping[8]);
               	        mem_mapping_disable(&bios_mapping[9]);
       	                mem_mapping_disable(&bios_mapping[10]);
                        mem_mapping_disable(&bios_mapping[11]);
                       	mem_mapping_disable(&bios_mapping[12]);
               	        mem_mapping_disable(&bios_mapping[13]);
       	                mem_mapping_disable(&bios_mapping[14]);
                        mem_mapping_disable(&bios_mapping[15]);
                        mem_mapping_enable(&flash->read_mapping);                        
                        break;
                        
                        case CMD_READ_ARRAY:
                        mem_mapping_enable(&bios_mapping[0]);
                        mem_mapping_enable(&bios_mapping[1]);
                        mem_mapping_enable(&bios_mapping[2]);
                        mem_mapping_enable(&bios_mapping[3]);
                        mem_mapping_enable(&bios_mapping[4]);
                        mem_mapping_enable(&bios_mapping[5]);
                        mem_mapping_enable(&bios_mapping[6]);
                        mem_mapping_enable(&bios_mapping[7]);
                       	mem_mapping_enable(&bios_mapping[8]);
               	        mem_mapping_enable(&bios_mapping[9]);
       	                mem_mapping_enable(&bios_mapping[10]);
                        mem_mapping_enable(&bios_mapping[11]);
                       	mem_mapping_enable(&bios_mapping[12]);
               	        mem_mapping_enable(&bios_mapping[13]);
       	                mem_mapping_enable(&bios_mapping[14]);
                        mem_mapping_enable(&bios_mapping[15]);
                        mem_mapping_disable(&flash->read_mapping);
                        break;
                }
        }
}

static void configure_path()
{
	switch(romset)
	{
		case ROM_440BX:
			path = "roms/440bx/";
			break;
		case ROM_VPC2007:
			path = "roms/vpc2007/";
			break;
	}
}

void flash_2mbit_readfiles()
{
        FILE *f;

        memset(&rom[0x3a000], 0xFF, 0x2000);

	configure_path();
	fn = (char *) malloc(255);
	strcpy(fn, path);
	strcat(fn, "escd.bin");
        f = romfopen(fn, "rb");
        if (f)
        {
                fread(&rom[0x3a000], 0x2000, 1, f);
                fclose(f);
        }
}

void *intel_flash_2mbit_init()
{
        FILE *f;
        flash_t *flash = malloc(sizeof(flash_t));
        memset(flash, 0, sizeof(flash_t));

        mem_mapping_add(&flash->read_mapping,
       	            0xc0000, 
               	    0x40000,
       	            flash_read, NULL, NULL,
                    NULL, NULL, NULL,
               	    NULL, MEM_MAPPING_EXTERNAL, (void *)flash);
        mem_mapping_add(&flash->write_mapping,
                    0xc0000, 
       	            0x40000,
       	            NULL, NULL, NULL,
                    flash_write, NULL, NULL,
       	            NULL, MEM_MAPPING_EXTERNAL, (void *)flash);
        mem_mapping_disable(&flash->read_mapping);
        flash->command = CMD_READ_ARRAY;
        flash->status = 0;
        
        memset(&rom[0x3a000], 0xFF, 0x2000);

	configure_path();
	fn = (char *) malloc(255);
	strcpy(fn, path);
	strcat(fn, "escd.bin");
        f = romfopen(fn, "rb");
        if (f)
        {
                fread(&rom[0x3a000], 0x2000, 1, f);
                fclose(f);
        }
        
        return flash;
}

void intel_flash_2mbit_close(void *p)
{
        FILE *f;
        flash_t *flash = (flash_t *)p;

	configure_path();
	fn = (char *) malloc(255);
	strcpy(fn, path);
	strcat(fn, "escd.bin");
        f = romfopen(fn, "wb");
        fwrite(&rom[0x3a000], 0x2000, 1, f);
        fclose(f);

        free(flash);
}

device_t intel_flash_2mbit_device =
{
        "Intel 28F001BXT Flash BIOS",
        0,
        intel_flash_2mbit_init,
        intel_flash_2mbit_close,
        NULL,
        NULL,
        NULL,
        NULL,
        NULL
};
#endif