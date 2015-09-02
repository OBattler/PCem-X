#include <string.h>

#include "ibm.h"
#include "io.h"
#include "mem.h"
#include "pci.h"

#include "i450gx.h"

static uint8_t card_i450gx[256];

static void i450gx_map(uint32_t addr, uint32_t size, int state)
{
        switch (state & 3)
        {
                case 0:
                mem_set_mem_state(addr, size, MEM_READ_EXTERNAL | MEM_WRITE_EXTERNAL);
                break;
                case 1:
                mem_set_mem_state(addr, size, MEM_READ_INTERNAL | MEM_WRITE_EXTERNAL);
                break;
                case 2:
                mem_set_mem_state(addr, size, MEM_READ_EXTERNAL | MEM_WRITE_INTERNAL);
                break;
                case 3:
                mem_set_mem_state(addr, size, MEM_READ_INTERNAL | MEM_WRITE_INTERNAL);
                break;
        }
        flushmmucache_nopc();        
}

void i450gx_write(int func, int addr, uint8_t val, void *priv)
{
        if (func)
           return;
           
        switch (addr)
        {
                case 0x00: case 0x01: case 0x02: case 0x03:
                case 0x08: case 0x09: case 0x0a: case 0x0b:
                case 0x0e:
                return;
                
                case 0x59: /*PAM0*/
                if ((card_i450gx[0x59] ^ val) & 0xf0)
                {
                        i450gx_map(0xf0000, 0x10000, val >> 4);
                        shadowbios = (val & 0x10);
                }
                pclog("i450gx_write : PAM0 write %02X\n", val);
                break;
                case 0x5a: /*PAM1*/
                if ((card_i450gx[0x5a] ^ val) & 0x0f)
                        i450gx_map(0xc0000, 0x04000, val & 0xf);
                if ((card_i450gx[0x5a] ^ val) & 0xf0)
                        i450gx_map(0xc4000, 0x04000, val >> 4);
                break;
                case 0x5b: /*PAM2*/
                if ((card_i450gx[0x5b] ^ val) & 0x0f)
                        i450gx_map(0xc8000, 0x04000, val & 0xf);
                if ((card_i450gx[0x5b] ^ val) & 0xf0)
                        i450gx_map(0xcc000, 0x04000, val >> 4);
                break;
                case 0x5c: /*PAM3*/
                if ((card_i450gx[0x5c] ^ val) & 0x0f)
                        i450gx_map(0xd0000, 0x04000, val & 0xf);
                if ((card_i450gx[0x5c] ^ val) & 0xf0)
                        i450gx_map(0xd4000, 0x04000, val >> 4);
                break;
                case 0x5d: /*PAM4*/
                if ((card_i450gx[0x5d] ^ val) & 0x0f)
                        i450gx_map(0xd8000, 0x04000, val & 0xf);
                if ((card_i450gx[0x5d] ^ val) & 0xf0)
                        i450gx_map(0xdc000, 0x04000, val >> 4);
                break;
                case 0x5e: /*PAM5*/
                if ((card_i450gx[0x5e] ^ val) & 0x0f)
                        i450gx_map(0xe0000, 0x04000, val & 0xf);
                if ((card_i450gx[0x5e] ^ val) & 0xf0)
                        i450gx_map(0xe4000, 0x04000, val >> 4);
                pclog("i450gx_write : PAM5 write %02X\n", val);
                break;
                case 0x5f: /*PAM6*/
                if ((card_i450gx[0x5f] ^ val) & 0x0f)
                        i450gx_map(0xe8000, 0x04000, val & 0xf);
                if ((card_i450gx[0x5f] ^ val) & 0xf0)
                        i450gx_map(0xec000, 0x04000, val >> 4);
                pclog("i450gx_write : PAM6 write %02X\n", val);
                break;
        }
                
        card_i450gx[addr] = val;
}

uint8_t i450gx_read(int func, int addr, void *priv)
{
        if (func)
           return 0xff;

        return card_i450gx[addr];
}
 
    
void i450gx_init()
{
        pci_add_specific(0, i450gx_read, i450gx_write, NULL);
        
        memset(card_i450gx, 0, 256);
        card_i450gx[0x00] = 0x86; card_i450gx[0x01] = 0x80; /*Intel*/
        card_i450gx[0x02] = 0x64; card_i450gx[0x03] = 0x84; /*450GX*/
        card_i450gx[0x04] = 0x07; card_i450gx[0x05] = 0x00;
        card_i450gx[0x06] = 0x40; card_i450gx[0x07] = 0x02;
        card_i450gx[0x08] = 0x00; /*A0 stepping*/
        card_i450gx[0x09] = 0x00; card_i450gx[0x0a] = 0x00; card_i450gx[0x0b] = 0x06;
        card_i450gx[0x0C] = 0x08;
        card_i450gx[0x0D] = 0x20;
        card_i450gx[0x40] = card_i450gx[0x41] = card_i450gx[0x42] = 0xFF;
	card_i450gx[0x43] = 0x1F;
        card_i450gx[0x48] = 0x06;
        card_i450gx[0x49] = 0x19;
        card_i450gx[0x4C] = 0x39;
        card_i450gx[0x51] = 0x80;
        card_i450gx[0x52] = 0x02;
	card_i450gx[0x59] = 0x30;
	memset(&card_i450gx[0x5A], 0x33, 6);
	card_i450gx[0x98] = 0x01;
	card_i450gx[0x9A] = 0xF0;
	card_i450gx[0x9B] = 0xFF;
	card_i450gx[0xA0] = 0x01;
	card_i450gx[0xA2] = 0xF0;
	card_i450gx[0xA3] = 0xFF;
	card_i450gx[0xA4] = 0x01;
	card_i450gx[0xA5] = 0xC0;
	card_i450gx[0xA6] = 0xFE;
	card_i450gx[0xB8] = 0x05;
	card_i450gx[0xBC] = 0x01;
	card_i450gx[0xC8] = 0x03;
}
