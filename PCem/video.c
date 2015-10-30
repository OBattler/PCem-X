#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include "ibm.h"
#include "device.h"
#include "mem.h"
#include "video.h"
#include "vid_svga.h"
#include "io.h"
#include "cpu.h"
#include "rom.h"
#include "timer.h"

#include "vid_ati18800.h"
#include "vid_ati28800.h"
#include "vid_ati_mach64.h"
#include "vid_cga.h"
#include "vid_cl_ramdac.h"
#include "vid_cl_gd.h"
#include "vid_cpqvdu.h"
#include "vid_ega.h"
#include "vid_et4000.h"
#include "vid_et4000w32.h"
#include "vid_hercules.h"
#include "vid_mda.h"
#include "vid_nv_riva128.h"
#include "vid_olivetti_m24.h"
#include "vid_oti067.h"
#include "vid_paradise.h"
#include "vid_pc1512.h"
#include "vid_pc1640.h"
#include "vid_pc200.h"
#include "vid_pcjr.h"
#include "vid_s3.h"
#include "vid_s3_virge.h"
#include "vid_tandy.h"
#include "vid_tgui9440.h"
#include "vid_tvga.h"
#include "vid_vga.h"

typedef struct
{
        char name[64];
        device_t *device;
        int legacy_id;
} VIDEO_CARD;

static VIDEO_CARD video_cards[] =
{
        {"ATI Graphics Pro Turbo (Mach64 GX)",     &mach64gx_device,            GFX_MACH64GX},
        {"ATI VGA Charger (ATI-28800)",            &ati28800_device,            GFX_VGACHARGER},
        {"ATI VGA Edge-16 (ATI-18800)",            &ati18800_device,            GFX_VGAEDGE16},
        {"CGA (New)",                              &cga_new_device,             GFX_NEW_CGA},
        {"CGA (Old)",                              &cga_device,                 GFX_CGA},
        {"Cirrus Logic CL-GD6235",                 &gd6235_device,              GFX_CL_GD6235},
        {"Cirrus Logic CL-GD5422",                 &gd5422_device,              GFX_CL_GD5422},
        {"Cirrus Logic CL-GD5429",                 &gd5429_device,              GFX_CL_GD5429},
        {"Cirrus Logic CL-GD5430",                 &gd5430_device,              GFX_CL_GD5430},
        {"Cirrus Logic CL-GD5434",                 &gd5434_device,              GFX_CL_GD5434},
        {"Cirrus Logic CL-GD5436",                 &gd5436_device,              GFX_CL_GD5436},
        {"Cirrus Logic CL-GD5440",                 &gd5440_device,              GFX_CL_GD5440},
        {"Cirrus Logic CL-GD5446",                 &gd5446_device,              GFX_CL_GD5446},
        {"Compaq VDU",                             &cpqvdu_device,              GFX_CPQVDU},
        {"Compaq EGA",                             &cpqega_device,              GFX_CPQEGA},
        {"Compaq/Paradise VGA",                    &cpqvga_device,              GFX_CPQVGA},
        {"Diamond Stealth 32 (Tseng ET4000/w32p)", &et4000w32p_device,          GFX_ET4000W32},
        {"Diamond CL-GD5430",                      &dia5430_device,             GFX_DIAMOND5430},
        {"Diamond Stealth 3D 2000 (S3 ViRGE)",     &s3_virge_device,            GFX_VIRGE},
        {"EGA",                                    &ega_device,                 GFX_EGA},
        {"Hercules",                               &hercules_device,            GFX_HERCULES},
        {"JEGA",                                   &jega_device,                GFX_JEGA},
        {"MDA",                                    &mda_device,                 GFX_MDA},
        {"Number Nine 9FX (S3 Trio64)",            &s3_9fx_device,              GFX_N9_9FX},
        {"nVidia RIVA 128",                        &riva128_device,             GFX_RIVA128},
        {"OAK OTI-067",                            &oti067_device,              GFX_OTI067},
        {"Paradise Bahamas 64 (S3 Vision864)",     &s3_bahamas64_device,        GFX_BAHAMAS64},
	{"Paradise WD90C11",			   &paradise_wd90c11_megapc_device,GFX_PARADISE},
        {"Phoenix S3 Trio32",                      &s3_phoenix_trio32_device,   GFX_PHOENIX_TRIO32},
        {"Phoenix S3 Trio64",                      &s3_phoenix_trio64_device,   GFX_PHOENIX_TRIO64},
        {"Phoenix S3 Vision964",                   &s3_phoenix_vision964_device,GFX_PHOENIX_VISION964},
        {"S3 ViRGE/DX",                            &s3_virge_375_device,        GFX_VIRGEDX},
        {"SuperEGA",                               &sega_device,                GFX_SUPEREGA},
        {"Trident TVGA8900D",                      &tvga8900d_device,           GFX_TVGA},
        {"Tseng ET4000AX",                         &et4000_device,              GFX_ET4000},
        {"Trident TGUI9440",                       &tgui9440_device,            GFX_TGUI9440},
        {"Virtual PC S3 Trio64",                   &s3_vpc_trio64_device,       GFX_VPC_TRIO64},
        {"VGA",                                    &vga_device,                 GFX_VGA},
        {"",                                       NULL,                        0}
};

int cga_color_burst = 1;
int cga_brown = 1;

int enable_overscan = 1;
int overscan_color = 0;
int overscan_x = 0;
int overscan_y = 0;

int video_card_available(int card)
{
        if (video_cards[card].device)
                return device_available(video_cards[card].device);

        return 1;
}

char *video_card_getname(int card)
{
        return video_cards[card].name;
}

device_t *video_card_getdevice(int card)
{
        return video_cards[card].device;
}

int video_card_has_config(int card)
{
        return video_cards[card].device->config ? 1 : 0;
}

int video_card_getid(char *s)
{
        int c = 0;

        while (video_cards[c].device)
        {
                if (!strcmp(video_cards[c].name, s))
                        return c;
                c++;
        }
        
        return 0;
}

int video_old_to_new(int card)
{
        int c = 0;
        
        while (video_cards[c].device)
        {
                if (video_cards[c].legacy_id == card)
                        return c;
                c++;
        }
        
        return 0;
}

int video_new_to_old(int card)
{
        return video_cards[card].legacy_id;
}

int video_fullscreen = 0, video_fullscreen_scale, video_fullscreen_first;
uint32_t *video_15to32, *video_16to32;

int egareads=0,egawrites=0;
int changeframecount=2;

uint8_t rotatevga[8][256];

int frames = 0;

int fullchange;

uint8_t edatlookup[4][4];

/*Video timing settings -

8-bit - 1mb/sec
        B = 8 ISA clocks
        W = 16 ISA clocks
        L = 32 ISA clocks

Slow 16-bit - 2mb/sec
        B = 6 ISA clocks
        W = 8 ISA clocks
        L = 16 ISA clocks

Fast 16-bit - 4mb/sec
        B = 3 ISA clocks
        W = 3 ISA clocks
        L = 6 ISA clocks

Slow VLB/PCI - 8mb/sec (ish)
        B = 4 bus clocks
        W = 8 bus clocks
        L = 16 bus clocks

Mid VLB/PCI -
        B = 4 bus clocks
        W = 5 bus clocks
        L = 10 bus clocks

Fast VLB/PCI -
        B = 3 bus clocks
        W = 3 bus clocks
        L = 4 bus clocks
*/

enum
{
        VIDEO_ISA = 0,
        VIDEO_BUS
};

int video_speed = 0;
int video_timing[6][4] =
{
        {VIDEO_ISA, 8, 16, 32},
        {VIDEO_ISA, 6,  8, 16},
        {VIDEO_ISA, 3,  3,  6},
        {VIDEO_BUS, 4,  8, 16},
        {VIDEO_BUS, 4,  5, 10},
        {VIDEO_BUS, 3,  3,  4}
};

void video_updatetiming()
{
        if (video_timing[video_speed][0] == VIDEO_ISA)
        {
                video_timing_b = (int)(isa_timing * video_timing[video_speed][1]);
                video_timing_w = (int)(isa_timing * video_timing[video_speed][2]);
                video_timing_l = (int)(isa_timing * video_timing[video_speed][3]);
        }
        else
        {
                video_timing_b = (int)(bus_timing * video_timing[video_speed][1]);
                video_timing_w = (int)(bus_timing * video_timing[video_speed][2]);
                video_timing_l = (int)(bus_timing * video_timing[video_speed][3]);
        }
        if (cpu_16bitbus)
           video_timing_l = video_timing_w * 2;
}

int video_timing_b, video_timing_w, video_timing_l;

int video_res_x, video_res_y, video_bpp;

void (*video_blit_memtoscreen)(int x, int y, int y1, int y2, int w, int h);
void (*video_blit_memtoscreen_8)(int x, int y, int w, int h);

void video_init()
{
#ifndef RELEASE_BUILD
        pclog("Video_init %i %i\n",romset,gfxcard);
#endif

	overscan_x = overscan_y = 0;

        switch (romset)
        {
		/* Uses own interface, standard font. */
                case ROM_IBMPCJR:
                device_add(&pcjr_video_device);
                return;

		/* Uses own interface, standard font. */
                case ROM_TANDY:
                device_add(&tandy_device);
                return;

		/* Uses own interface. */
                case ROM_PC1512:
                device_add(&pc1512_device);
                return;

		/* CGA, standard font. */
                case ROM_PC1640:
                device_add(&pc1640_device);
                return;

		/* CGA, own font. */
                case ROM_PC200:
                device_add(&pc200_device);
                return;

		/* Uses own interface. */
                case ROM_OLIM24:
                device_add(&m24_device);
                return;

                case ROM_PC2086:
                device_add(&paradise_pvga1a_pc2086_device);
                return;

                case ROM_PC3086:
                device_add(&paradise_pvga1a_pc3086_device);
                return;

                case ROM_MEGAPC:
                device_add(&paradise_wd90c11_megapc_device);
                return;

                case ROM_ACER386:
                device_add(&oti067_device);
                return;

                case ROM_IBMPS1_2011:
                device_add(&ps1vga_device);
                return;
        }
        device_add(video_cards[video_old_to_new(gfxcard)].device);
}


BITMAP *buffer, *buffer32;

uint8_t mda_fontdat[256][8];
uint8_t mda_fontdatm[256][16];

uint8_t cga_fontdat[256][8];
uint8_t cga_fontdatm[256][16];

uint8_t pc1512_fontdat[256][8];
uint8_t pc1512_fontdatm[256][16];

uint8_t pc200_fontdat[256][8];
uint8_t pc200_fontdatm[256][16];

uint8_t herc_fontdat[256][8];
uint8_t herc_fontdatm[256][16];

int xsize=1,ysize=1;

PALETTE cgapal;

void loadfont(char *s, int format, uint8_t fontdat[256][8], uint8_t fontdatm[256][16])
{
        // FILE *f=romfopen(s,"rb");
        FILE *f=fopen(s,"rb");
        int c,d;
        if (!f) return;

	fseek(f, 0, SEEK_SET);
        if (!format)
        {
                for (c=0;c<256;c++)
                {
                        for (d=0;d<8;d++)
                        {
                                fontdatm[c][d]=getc(f);
                        }
                }
                for (c=0;c<256;c++)
                {
                        for (d=0;d<8;d++)
                        {
                                fontdatm[c][d+8]=getc(f);
                        }
                }
                fseek(f,4096+2048,SEEK_SET);
                for (c=0;c<256;c++)
                {
                        for (d=0;d<8;d++)
                        {
                                fontdat[c][d]=getc(f);
                        }
                }
        }
        else if (format == 1)
        {
                for (c=0;c<256;c++)
                {
                        for (d=0;d<8;d++)
                        {
                                fontdatm[c][d]=getc(f);
                        }
                }
                for (c=0;c<256;c++)
                {
                        for (d=0;d<8;d++)
                        {
                                fontdatm[c][d+8]=getc(f);
                        }
                }
                fseek(f, 4096, SEEK_SET);
                for (c=0;c<256;c++)
                {
                        for (d=0;d<8;d++)
                        {
                                fontdat[c][d]=getc(f);
                        }
                        for (d=0;d<8;d++) getc(f);
                }
        }
        else
        {
                for (c=0;c<256;c++)
                {
                        for (d=0;d<8;d++)
                        {
                                fontdat[c][d]=getc(f);
                        }
                }
        }
        fclose(f);
}

void cga_loadfont(char *s, int format)
{
        // FILE *f=romfopen(s,"rb");
        FILE *f=fopen(s,"rb");
        int c,d;
        if (!f) return;

	fseek(f, 0, SEEK_SET);
        if (!format)
        {
                for (c=0;c<256;c++)
                {
                        for (d=0;d<8;d++)
                        {
                                cga_fontdatm[c][d]=getc(f);
                        }
                }
                for (c=0;c<256;c++)
                {
                        for (d=0;d<8;d++)
                        {
                                cga_fontdatm[c][d+8]=getc(f);
                        }
                }
                fseek(f,4096+2048,SEEK_SET);
                for (c=0;c<256;c++)
                {
                        for (d=0;d<8;d++)
                        {
                                cga_fontdat[c][d]=getc(f);
                        }
                }
        }
        else if (format == 1)
        {
                for (c=0;c<256;c++)
                {
                        for (d=0;d<8;d++)
                        {
                                cga_fontdatm[c][d]=getc(f);
                        }
                }
                for (c=0;c<256;c++)
                {
                        for (d=0;d<8;d++)
                        {
                                cga_fontdatm[c][d+8]=getc(f);
                        }
                }
                fseek(f, 4096, SEEK_SET);
                for (c=0;c<256;c++)
                {
                        for (d=0;d<8;d++)
                        {
                                cga_fontdat[c][d]=getc(f);
                        }
                        for (d=0;d<8;d++) getc(f);
                }
        }
        else
        {
                for (c=0;c<256;c++)
                {
                        for (d=0;d<8;d++)
                        {
                                cga_fontdat[c][d]=getc(f);
                        }
                }
        }
        fclose(f);
}

void pc1512_loadfont(char *s, int format)
{
        // FILE *f=romfopen(s,"rb");
        FILE *f=fopen(s,"rb");
        int c,d;
        if (!f) return;

	fseek(f, 0, SEEK_SET);
        if (!format)
        {
                for (c=0;c<256;c++)
                {
                        for (d=0;d<8;d++)
                        {
                                pc1512_fontdatm[c][d]=getc(f);
                        }
                }
                for (c=0;c<256;c++)
                {
                        for (d=0;d<8;d++)
                        {
                                pc1512_fontdatm[c][d+8]=getc(f);
                        }
                }
                fseek(f,4096+2048,SEEK_SET);
                for (c=0;c<256;c++)
                {
                        for (d=0;d<8;d++)
                        {
                                pc1512_fontdat[c][d]=getc(f);
                        }
                }
        }
        else if (format == 1)
        {
                for (c=0;c<256;c++)
                {
                        for (d=0;d<8;d++)
                        {
                                pc1512_fontdatm[c][d]=getc(f);
                        }
                }
                for (c=0;c<256;c++)
                {
                        for (d=0;d<8;d++)
                        {
                                pc1512_fontdatm[c][d+8]=getc(f);
                        }
                }
                fseek(f, 4096, SEEK_SET);
                for (c=0;c<256;c++)
                {
                        for (d=0;d<8;d++)
                        {
                                pc1512_fontdat[c][d]=getc(f);
                        }
                        for (d=0;d<8;d++) getc(f);
                }
        }
        else
        {
                for (c=0;c<256;c++)
                {
                        for (d=0;d<8;d++)
                        {
                                pc1512_fontdat[c][d]=getc(f);
                        }
                }
        }
        fclose(f);
}

void initvideo()
{
        int c, d, e;

	/* Account for overscan. */
        buffer32 = create_bitmap(2064, 2056);

        buffer = create_bitmap(2064, 2056);

        for (c = 0; c < 64; c++)
        {
                cgapal[c + 64].r = (((c & 4) ? 2 : 0) | ((c & 0x10) ? 1 : 0)) * 21;
                cgapal[c + 64].g = (((c & 2) ? 2 : 0) | ((c & 0x10) ? 1 : 0)) * 21;
                cgapal[c + 64].b = (((c & 1) ? 2 : 0) | ((c & 0x10) ? 1 : 0)) * 21;
                if ((c & 0x17) == 6)
                        cgapal[c + 64].g >>= 1;
        }
        for (c = 0; c < 64; c++)
        {
                cgapal[c + 128].r = (((c & 4) ? 2 : 0) | ((c & 0x20) ? 1 : 0)) * 21;
                cgapal[c + 128].g = (((c & 2) ? 2 : 0) | ((c & 0x10) ? 1 : 0)) * 21;
                cgapal[c + 128].b = (((c & 1) ? 2 : 0) | ((c & 0x08) ? 1 : 0)) * 21;
        }

        for (c = 0; c < 256; c++)
        {
                e = c;
                for (d = 0; d < 8; d++)
                {
                        rotatevga[d][c] = e;
                        e = (e >> 1) | ((e & 1) ? 0x80 : 0);
                }
        }
        for (c = 0; c < 4; c++)
        {
                for (d = 0; d < 4; d++)
                {
                        edatlookup[c][d] = 0;
                        if (c & 1) edatlookup[c][d] |= 1;
                        if (d & 1) edatlookup[c][d] |= 2;
                        if (c & 2) edatlookup[c][d] |= 0x10;
                        if (d & 2) edatlookup[c][d] |= 0x20;
//                        printf("Edat %i,%i now %02X\n",c,d,edatlookup[c][d]);
                }
        }

        video_15to32 = malloc(4 * 65536);
        for (c = 0; c < 65536; c++)
                video_15to32[c] = ((c & 31) << 3) | (((c >> 5) & 31) << 11) | (((c >> 10) & 31) << 19);

        video_16to32 = malloc(4 * 65536);
        for (c = 0; c < 65536; c++)
                video_16to32[c] = ((c & 31) << 3) | (((c >> 5) & 63) << 10) | (((c >> 11) & 31) << 19);

}

void closevideo()
{
        free(video_15to32);
        free(video_16to32);
        destroy_bitmap(buffer);
        destroy_bitmap(buffer32);
}
