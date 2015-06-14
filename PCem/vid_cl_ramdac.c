/*It is unknown exactly what RAMDAC this is
  It is possibly a Sierra 1502x
  It's addressed by the TLIVESA1 driver for ET4000*/
#include "ibm.h"
#include "device.h"
#include "mem.h"
#include "rom.h"
#include "video.h"
#include "vid_svga.h"
#include "vid_cl_ramdac.h"
#include "vid_cl5446.h"
#include "vid_cl5446_blit.h"

void cl_ramdac_out(uint16_t addr, uint8_t val, cl_ramdac_t *ramdac, void *gd5446, svga_t *svga)
{
	gd5446_t *real_gd5446 = (gd5446_t *) gd5446;
        //pclog("OUT RAMDAC %04X %02X\n",addr,val);
        switch (addr)
        {
                case 0x3C6:
                if (ramdac->state == 4)
                {
                        ramdac->state = 0;
                        ramdac->ctrl = val;
			svga->bpp = cirrus_get_bpp(real_gd5446, svga);
                        return;
                }
                ramdac->state = 0;
                break;
                case 0x3C7: case 0x3C8:
                ramdac->state = 0;
                break;
		case 0x3C9:
		if (svga->seqregs[0x12] & CIRRUS_CURSOR_HIDDENPEL)
		{
			ramdac->state = 0;
			// ramdac->ctrl = val;
                	svga->fullchange = changeframecount;
                	switch (svga->dac_pos)
                	{
                        	case 0: 
                        	real_gd5446->hiddenpal[svga->dac_write & 0xf].r = val & 63;
                        	svga->dac_pos++; 
                        	break;
                        	case 1: 
                        	real_gd5446->hiddenpal[svga->dac_write & 0xf].g = val & 63;
                        	svga->dac_pos++; 
                        	break;
                        	case 2: 
                        	real_gd5446->hiddenpal[svga->dac_write & 0xf].b = val & 63;
                        	svga->dac_pos = 0; 
                        	svga->dac_write = (svga->dac_write + 1) & 255; 
                        	break;
                	}
			return;
		}
                ramdac->state = 0;
		break;
        }
        svga_out(addr, val, svga);
}

uint8_t cl_ramdac_in(uint16_t addr, cl_ramdac_t *ramdac, void *gd5446, svga_t *svga)
{
	gd5446_t *real_gd5446 = (gd5446_t *) gd5446;
        //pclog("IN RAMDAC %04X\n",addr);
        switch (addr)
        {
                case 0x3C6:
                if (ramdac->state == 4)
                {
                        ramdac->state = 0;
                        return ramdac->ctrl;
                }
                ramdac->state++;
                break;
                case 0x3C7: case 0x3C8:
                ramdac->state = 0;
                break;
                case 0x3C9:
		if (svga->seqregs[0x12] & CIRRUS_CURSOR_HIDDENPEL)
		{
                	ramdac->state = 0;
                	switch (svga->dac_pos)
                	{
                        	case 0: 
                        	svga->dac_pos++; 
                        	return real_gd5446->hiddenpal[svga->dac_read & 0xf].r;
                        	case 1: 
                        	svga->dac_pos++; 
                        	return real_gd5446->hiddenpal[svga->dac_read & 0xf].g;
                        	case 2: 
                        	svga->dac_pos=0; 
                        	svga->dac_read = (svga->dac_read + 1) & 255; 
                        	return real_gd5446->hiddenpal[(svga->dac_read - 1) & 15].b;
                	}
		}
                ramdac->state = 0;
                break;
        }
        return svga_in(addr, svga);
}
