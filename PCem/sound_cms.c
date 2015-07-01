// license:BSD-3-Clause (MAME), GPL (DOSBOX), PCem (SVN)
// copyright-holders:Juergen Buchmueller, Manuel Abadia
/***************************************************************************

    Philips SAA1099 Sound driver

    By Juergen Buchmueller and Manuel Abadia

    SAA1099 register layout:
    ========================

    offs | 7654 3210 | description
    -----+-----------+---------------------------
    0x00 | ---- xxxx | Amplitude channel 0 (left)
    0x00 | xxxx ---- | Amplitude channel 0 (right)
    0x01 | ---- xxxx | Amplitude channel 1 (left)
    0x01 | xxxx ---- | Amplitude channel 1 (right)
    0x02 | ---- xxxx | Amplitude channel 2 (left)
    0x02 | xxxx ---- | Amplitude channel 2 (right)
    0x03 | ---- xxxx | Amplitude channel 3 (left)
    0x03 | xxxx ---- | Amplitude channel 3 (right)
    0x04 | ---- xxxx | Amplitude channel 4 (left)
    0x04 | xxxx ---- | Amplitude channel 4 (right)
    0x05 | ---- xxxx | Amplitude channel 5 (left)
    0x05 | xxxx ---- | Amplitude channel 5 (right)
         |           |
    0x08 | xxxx xxxx | Frequency channel 0
    0x09 | xxxx xxxx | Frequency channel 1
    0x0a | xxxx xxxx | Frequency channel 2
    0x0b | xxxx xxxx | Frequency channel 3
    0x0c | xxxx xxxx | Frequency channel 4
    0x0d | xxxx xxxx | Frequency channel 5
         |           |
    0x10 | ---- -xxx | Channel 0 octave select
    0x10 | -xxx ---- | Channel 1 octave select
    0x11 | ---- -xxx | Channel 2 octave select
    0x11 | -xxx ---- | Channel 3 octave select
    0x12 | ---- -xxx | Channel 4 octave select
    0x12 | -xxx ---- | Channel 5 octave select
         |           |
    0x14 | ---- ---x | Channel 0 frequency enable (0 = off, 1 = on)
    0x14 | ---- --x- | Channel 1 frequency enable (0 = off, 1 = on)
    0x14 | ---- -x-- | Channel 2 frequency enable (0 = off, 1 = on)
    0x14 | ---- x--- | Channel 3 frequency enable (0 = off, 1 = on)
    0x14 | ---x ---- | Channel 4 frequency enable (0 = off, 1 = on)
    0x14 | --x- ---- | Channel 5 frequency enable (0 = off, 1 = on)
         |           |
    0x15 | ---- ---x | Channel 0 noise enable (0 = off, 1 = on)
    0x15 | ---- --x- | Channel 1 noise enable (0 = off, 1 = on)
    0x15 | ---- -x-- | Channel 2 noise enable (0 = off, 1 = on)
    0x15 | ---- x--- | Channel 3 noise enable (0 = off, 1 = on)
    0x15 | ---x ---- | Channel 4 noise enable (0 = off, 1 = on)
    0x15 | --x- ---- | Channel 5 noise enable (0 = off, 1 = on)
         |           |
    0x16 | ---- --xx | Noise generator parameters 0
    0x16 | --xx ---- | Noise generator parameters 1
         |           |
    0x18 | --xx xxxx | Envelope generator 0 parameters
    0x18 | x--- ---- | Envelope generator 0 control enable (0 = off, 1 = on)
    0x19 | --xx xxxx | Envelope generator 1 parameters
    0x19 | x--- ---- | Envelope generator 1 control enable (0 = off, 1 = on)
         |           |
    0x1c | ---- ---x | All channels enable (0 = off, 1 = on)
    0x1c | ---- --x- | Synch & Reset generators

    Version History:
    ================
    ??-??-200? - First version of the driver submitted for MESS (GPL/MESS license)
    ??-??-200? - Submitted to DOSBOX for Creative Music System/Game Blaster emulation under GPL
    ??-??-201? - MAME version relicensed to BSD 3 Clause (GPL+ compatible)
    ??-??-201? - optimized DOSBOX version submitted to PCem by Tom Walker under GPL
    07-01-2015 - Applied clock divisor fix from DOSBOX SVN, http://www.vogons.org/viewtopic.php?p=344227#p344227

***************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include "ibm.h"
#include "device.h"
#include "io.h"
#include "sound.h"
#include "sound_cms.h"
#define MASTER_SAA_CLOCK 7159000

typedef struct cms_t
{
        int addrs[2];
        uint8_t regs[2][32];
        uint16_t latch[2][6];
        int freq[2][6];
        float count[2][6];
        int vol[2][6][2];
        int stat[2][6];
        uint16_t noise[2][2];
        uint16_t noisefreq[2][2];
        int noisecount[2][2];
        int noisetype[2][2];

        int16_t buffer[SOUNDBUFLEN * 2];

        int pos;
} cms_t;

void cms_poll(void *p)
{
        cms_t *cms = (cms_t *)p;
        int c, d;
        int16_t out_l = 0, out_r = 0;

        if (cms->pos >= SOUNDBUFLEN)
                return;

        for (c = 0; c < 4; c++)
        {
                switch (cms->noisetype[c >> 1][c & 1])
                {
                        case 0: cms->noisefreq[c >> 1][c & 1] = MASTER_SAA_CLOCK / 256; break;
                        case 1: cms->noisefreq[c >> 1][c & 1] = MASTER_SAA_CLOCK / 512; break;
                        case 2: cms->noisefreq[c >> 1][c & 1] = MASTER_SAA_CLOCK / 1024; break;
                        case 3: cms->noisefreq[c >> 1][c & 1] = cms->freq[c >> 1][(c & 1) * 3]; break;
                }
        }
        for (c = 0; c < 2; c ++)
        {
                if (cms->regs[c][0x1C] & 1)
                {
                        for (d = 0; d < 6; d++)
                        {
                                if (cms->regs[c][0x14] & (1 << d))
                                {
                                        if (cms->stat[c][d]) out_l += (cms->vol[c][d][0] * 90);
                                        if (cms->stat[c][d]) out_r += (cms->vol[c][d][1] * 90);
                                        cms->count[c][d] += cms->freq[c][d];
                                        if (cms->count[c][d] >= 24000)
                                        {
                                                cms->count[c][d] -= 24000;
                                                cms->stat[c][d] ^= 1;
                                        }
                                }
                                else if (cms->regs[c][0x15] & (1 << d))
                                {
                                        if (cms->noise[c][d / 3] & 1) out_l += (cms->vol[c][d][0] * 90);
                                        if (cms->noise[c][d / 3] & 1) out_r += (cms->vol[c][d][0] * 90);
                                }
                        }
                        for (d = 0; d < 2; d++)
                        {
                                cms->noisecount[c][d] += cms->noisefreq[c][d];
                                while (cms->noisecount[c][d] >= 24000)
                                {
                                        cms->noisecount[c][d] -= 24000;
                                        cms->noise[c][d] <<= 1;
                                        if (!(((cms->noise[c][d] & 0x4000) >> 8) ^ (cms->noise[c][d] & 0x40))) 
                                                cms->noise[c][d] |= 1;
                                }
                        }
                }
        }
        cms->buffer[(cms->pos << 1)] = out_l;
        cms->buffer[(cms->pos << 1) + 1] = out_r;

        cms->pos++;
}

void cms_get_buffer(int16_t *buffer, int len, void *p)
{
        cms_t *cms = (cms_t *)p;
        
        int c;

        for (c = 0; c < len * 2; c++)
                buffer[c] += cms->buffer[c];

        cms->pos = 0;
}

void cms_write(uint16_t addr, uint8_t val, void *p)
{
        cms_t *cms = (cms_t *)p;
        int voice;
        int chip = (addr & 2) >> 1;
        
        pclog("cms_write : addr %04X val %02X\n", addr, val);
        
        if (addr & 1)
           cms->addrs[chip] = val & 31;
        else
        {
                cms->regs[chip][cms->addrs[chip] & 31] = val;
                switch (cms->addrs[chip] & 31)
                {
                        case 0x00: case 0x01: case 0x02: /*Volume*/
                        case 0x03: case 0x04: case 0x05:
                        voice = cms->addrs[chip] & 7;
                        cms->vol[chip][voice][0] = val & 0xf;
                        cms->vol[chip][voice][1] = val >> 4;
                        break;
                        case 0x08: case 0x09: case 0x0A: /*Frequency*/
                        case 0x0B: case 0x0C: case 0x0D:
                        voice = cms->addrs[chip] & 7;
                        cms->latch[chip][voice] = (cms->latch[chip][voice] & 0x700) | val;
                        cms->freq[chip][voice] = (MASTER_SAA_CLOCK / 512 << (cms->latch[chip][voice] >> 8)) / (511 - (cms->latch[chip][voice] & 255));
                        break;
                        case 0x10: case 0x11: case 0x12: /*Octave*/
                        voice = (cms->addrs[chip] & 3) << 1;
                        cms->latch[chip][voice] = (cms->latch[chip][voice] & 0xFF) | ((val & 7) << 8);
                        cms->latch[chip][voice + 1] = (cms->latch[chip][voice + 1] & 0xFF) | ((val & 0x70) << 4);
                        cms->freq[chip][voice] = (MASTER_SAA_CLOCK / 512 << (cms->latch[chip][voice] >> 8)) / (511 - (cms->latch[chip][voice] & 255));
                        cms->freq[chip][voice + 1] = (MASTER_SAA_CLOCK / 512 << (cms->latch[chip][voice + 1] >> 8)) / (511 - (cms->latch[chip][voice + 1] & 255));
                        break;
                        case 0x16: /*Noise*/
                        cms->noisetype[chip][0] = val & 3;
                        cms->noisetype[chip][1] = (val >> 4) & 3;
                        break;
                }
        }
}

uint8_t cms_read(uint16_t addr, void *p)
{
        cms_t *cms = (cms_t *)p;
        int chip = (addr & 2) >> 1;
        
        if (addr & 1) 
                return cms->addrs[chip];
                
        return cms->regs[chip][cms->addrs[chip] & 31];
}

void *cms_init()
{
        cms_t *cms = malloc(sizeof(cms_t));
        memset(cms, 0, sizeof(cms_t));

        pclog("cms_init\n");
        io_sethandler(0x0220, 0x0004, cms_read, NULL, NULL, cms_write, NULL, NULL, cms);
        sound_add_handler(cms_poll, cms_get_buffer, cms);
        return cms;
}

void cms_close(void *p)
{
        cms_t *cms = (cms_t *)p;
        
        free(cms);
}

device_t cms_device =
{
        "Creative Music System / Game Blaster",
        0,
        cms_init,
        cms_close,
        NULL,
        NULL,
        NULL,
        NULL
};
