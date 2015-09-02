#include <stdio.h>
#include <inttypes.h>
#include "pit.h"
#include "ibm.h"
#include "io.h"
#include "nvr.h"
#include "pic.h"
#include "timer.h"

int oldromset;
int nvrmask=63;
uint8_t nvrram[256];
int nvraddr;

int nvr_dosave = 0;

static int nvr_onesec_time = 0, nvr_onesec_cnt = 0;

void nvr_recalc()
{
        int c;
        int newrtctime;
        c=1<<((nvrram[0xA]&0xF)-1);
        newrtctime=(int)(RTCCONST * c * (1 << TIMER_SHIFT));
        if (rtctime>newrtctime) rtctime=newrtctime;
}

void nvr_rtc(void *p)
{
        int c;
        if (!(nvrram[0xA]&0xF))
        {
                rtctime=0x7fffffff;
                return;
        }
        c=1<<((nvrram[0xA]&0xF)-1);
        rtctime += (int)(RTCCONST * c * (1 << TIMER_SHIFT));
//        pclog("RTCtime now %f\n",rtctime);
        nvrram[0xC] |= 0x40;
        if (nvrram[0xB]&0x40)
        {
                nvrram[0xC]|=0x80;
                if (AMSTRAD) picint(2);
                else         picint(0x100);
//                pclog("RTC int\n");
        }
}

int is_leap(int year)
{
	year += 1900;
	if (year % 400 == 0)  return 1;
	if (year % 100 == 0)  return 0;
	if (year % 4 == 0)  return 1;
	return 0;
}

void rtc_inc(int isbcd, uint8_t *tgt)
{
	int i = 0;
	if (isbcd)  *tgt = ((*tgt & 0xF0) >> 1) + ((*tgt & 0xF0) >> 3) + (*tgt & 0xf);
	(*tgt)++;
	(*tgt) %= 100;
	if (isbcd)  *tgt = (*tgt % 10) + ((* tgt / 10) * 16);
}

int lastday(int isbcd, int month, int year)
{
	switch(month)
	{
		case 1:
		case 3:
		case 5:
		case 7:
		case 8:
		case 10:
		case 12:
		case 0x10:
		case 0x12:
			if (isbcd)  return 0x31;
			if (!isbcd)  return 31;
			break;
		case 4:
		case 6:
		case 9:
		case 11:
		case 0x11:
			if (isbcd)  return 0x30;
			if (!isbcd)  return 30;
			break;
		case 2:
			if (isbcd)
			{
				if (is_leap(year))
				{
					return 0x29;
				}
				else
				{
					return 0x28;
				}
			}
			else
			{
				if (is_leap(year))
				{
					return 29;
				}
				else
				{
					return 28;
				}
			}
			break;
		default:
			return 0;
			break;
	}
}

void nvr_do()
{
        int c;
	if (nvrram[0xA]&0xF)
	{
		c=1<<((nvrram[0xA]&0xF)-1);
		rtctime += (int)(RTCCONST * c * (1 << TIMER_SHIFT));
	}
	else
		rtctime = 0x7fffffff;
}

void nvr_onesec(void *p)
{
	int is24hour = (nvrram[0xB] & 2) >> 1;
	int isbcd = (nvrram[0xB] & 4) >> 2;
	int lasthour = 23;
	int firsthour = 0;
	int lastmin = 59;
	int lastmonth = 12;
	// Absolutely not, this yields -1 for 0 and -2 for 1
	// isbcd = ~isbcd;
	isbcd = 1 - isbcd;
	if (AMSTRAD)
	{
		isbcd = 1;
		is24hour = 1;
	}
	if (isbcd)
	{
		lastmin = 0x59;
		lastmonth = 0x12;
	}
	if (is24hour & isbcd)
	{
		lasthour = 0x23;
	}
	if (!is24hour)
	{
		if (!isbcd)
		{
			lasthour = 12;
		}
		else
		{
			lasthour = 0x12;
		}
		firsthour = 1;
	}
        nvr_onesec_cnt++;
	if (nvr_onesec_cnt >= 100)
	{
		// 1 second has passed, update NVRAM
		// Mark update in progress
		nvrram[0xA] |= 0x80;
		// nvrram[0xE] |= 0x80;
		// nvr_do();
		rtc_inc(isbcd, &nvrram[0]);
		if (nvrram[0] > lastmin)
		{
			nvrram[0] = 0;
			rtc_inc(isbcd, &nvrram[2]);
			if (nvrram[2] > lastmin)
			{
				nvrram[2] = 0;
				nvrram[0xA] |= 0x80;
				rtc_inc(isbcd, &nvrram[4]);
				if (nvrram[4] > lasthour)
				{
					if (!is24hour)  nvrram[4] ^= 0x80;
					nvrram[4] = firsthour;
					nvrram[6]++;
					if (nvrram[6] >= 8)  nvrram[6] = 1;
					rtc_inc(isbcd, &nvrram[7]);
					if (nvrram[7] > lastday(isbcd, nvrram[8], nvrram[9]))
					{
						nvrram[7] = 1;
						rtc_inc(isbcd, &nvrram[8]);
						if (nvrram[8] > lastmonth)
						{
							nvrram[8] = 1;
							rtc_inc(isbcd, &nvrram[9]);
						}
					}
				}
			}
		}
		/* if (AMSTRAD)
		{
			nvrram[0xE] = nvrram[2];
			nvrram[0xF] = nvrram[4];
			nvrram[0x10] = nvrram[6];
			nvrram[0x11] = nvrram[7];
			nvrram[0x12] = nvrram[8];
			nvrram[0x13] = nvrram[9];
		} */
		/* if ((RTCCONST * 32768.0) < 16000000.0)
		{
			nvrram[0x30] = nvrram[4];
			nvrram[0x31] = nvrram[2];
		} */
		// Mark update no longer in progress
		//if (!AMSTRAD)  nvrram[0xA] &= 0x7F;
		// Uncomment this
		// nvr_do();
		// nvr_dosave = 1;

                nvr_onesec_cnt = 0;
                // nvrram[0xC] |= 0x10;
                /* if (nvrram[0xB] & 0x10)
                {
                        nvrram[0xC] |= 0x90;
                        if (AMSTRAD) picint(2);
                        else         picint(0x100);
                } */
//                pclog("RTC onesec\n");
        }
	else
	{
		if (nvr_onesec_cnt == 1)
		{
			nvrram[0xA] &= 0x7F;
                	if (nvrram[0xB] & 0x10)
                	{
                        	nvrram[0xC] |= 0x90;
                        	if (AMSTRAD) picint(2);
                        	else         picint(0x100);
                	}
		}
		// nvr_do();
		// nvrram[0xA] &= 0x7F;
		// nvrram[0xC] &= 0xEF;
		// nvrram[0xE] &= 0x7F;
		// nvr_dosave = 1;
	}
        nvr_onesec_time += (int)(10000 * TIMER_USEC);
}

void writenvr(uint16_t addr, uint8_t val, void *priv)
{
        int c;
	// if (AMSTRAD)  printf("AM ");
        // printf("Write NVR %03X %02X %02X %04X:%04X %i\n",addr,nvraddr,val,cs>>4,pc,ins);
	// printf("CPU Clock = %f MHz\n", RTCCONST * 32768.0);
        if (addr&1)
        {
//              if (nvraddr == 0x33)  pclog("NVRWRITE33 %02X %04X:%04X %i\n",val,CS,pc,ins);
		/* if (AMSTRAD)
		{
                	if (nvraddr >= 0x13 && nvrram[nvraddr] != val) 
	                   nvr_dosave = 1;
		}
		else
		{ */
//                	if (nvraddr >= 0xe && nvrram[nvraddr] != val) 
                	if (nvrram[nvraddr] != val) 
	                   nvr_dosave = 1;
		// }
                if (nvraddr!=0xC && nvraddr!=0xD && nvraddr!=0xA) nvrram[nvraddr]=val;
                
                if (nvraddr==0xA)
                {
//                       pclog("NVR rate %i\n",val&0xF);
                        if (val&0xF)
                        {
                               	c=1<<((val&0xF)-1);
                               	rtctime += (int)(RTCCONST * c * (1 << TIMER_SHIFT));
				printf("%ld MHz\n", rtctime);
                        }
                        else
                           rtctime = 0x7fffffff;

			val &= 0x7F;
			val |= (nvrram[nvraddr] & 0x80);
			nvrram[nvraddr] = val;
                       pclog("NVR rate %i\n",nvrram[nvraddr]&0xF);
                }
        }
        else        nvraddr=val&nvrmask;
}

uint8_t readnvr(uint16_t addr, void *priv)
{
        uint8_t temp;
	uint32_t where;
	// if (AMSTRAD)  printf("AM ");
        // printf("Read NVR %03X %02X %02X %04X:%04X\n",addr,nvraddr,nvrram[nvraddr],cs>>4,pc);
	// printf("NVR 0F = %02X\n", nvrram[0xC]);
        if (addr&1)
        {
                if (nvraddr==0xD) nvrram[0xD]|=0x80;
		// Amstrad test
                // if (AMSTRAD & (nvraddr==0xA))
                /* if (nvraddr==0xA)
                {
                        temp=nvrram[0xA];
                        nvrram[0xA]^=0x80;
                        return temp;
                } */
                if (nvraddr==0xC)
                {
                        if (AMSTRAD) picintc(2);
                        else         picintc(0x100);
                        temp=nvrram[0xC];
                        nvrram[0xC]=0;
                        return temp;
                }
//                if (AMIBIOS && nvraddr==0x36) return 0;
                /* if (!AMSTRAD)  if (nvraddr==0xA) nvrram[0xA]^=0x80;
		if (AMSTRAD)
		{
			where = cs + pc;
                	if ((where & 0xFFFFFFF0) != 0xFC4C0)
			{
				pclog("Date/time test\n");
				if (nvraddr==0xA) nvrram[0xA]^=0x80;
			}
		} */
                return nvrram[nvraddr];
        }
        return nvraddr;
}

void loadnvr()
{
        FILE *f;
        int c;
        nvrmask=63;
        oldromset=romset;
        switch (romset)
        {
                case ROM_PC1512:     f = romfopen("pc1512.nvr",     "rb"); break;
                case ROM_PC1640:     f = romfopen("pc1640.nvr",     "rb"); break;
                case ROM_PC200:      f = romfopen("pc200.nvr",      "rb"); break;
                case ROM_PC2086:     f = romfopen("pc2086.nvr",     "rb"); break;
                case ROM_PC3086:     f = romfopen("pc3086.nvr",     "rb"); break;                
                case ROM_IBMAT:      f = romfopen("at.nvr",         "rb"); break;
                case ROM_IBMPS1_2011: f = romfopen("ibmps1_2011.nvr", "rb"); /*nvrmask = 127; */break;
                case ROM_CMDPC30:    f = romfopen("cmdpc30.nvr",    "rb"); nvrmask = 127; break;                
                case ROM_AMI286:     f = romfopen("ami286.nvr",     "rb"); nvrmask = 127; break;
                case ROM_DELL200:    f = romfopen("dell200.nvr",    "rb"); nvrmask = 127; break;
                case ROM_IBMAT386:   f = romfopen("at386.nvr",      "rb"); nvrmask = 127; break;
                case ROM_ACER386:    f = romfopen("acer386.nvr",    "rb"); nvrmask = 127; break;
                case ROM_MEGAPC:     f = romfopen("megapc.nvr",     "rb"); nvrmask = 127; break;
                case ROM_AMI386:     f = romfopen("ami386.nvr",     "rb"); nvrmask = 127; break;
                case ROM_DESKPRO_386: f = romfopen("deskpro386.nvr", "rb"); break;
                case ROM_PX386:      f = romfopen("px386.nvr",      "rb"); break;
                case ROM_DTK386:     f = romfopen("dtk386.nvr",     "rb"); break;
                case ROM_AMI486:     f = romfopen("ami486.nvr",     "rb"); nvrmask = 127; break;
                case ROM_PX486:      f = romfopen("px486.nvr",      "rb"); nvrmask = 127; break;
                case ROM_WIN486:     f = romfopen("win486.nvr",     "rb"); nvrmask = 127; break;
                case ROM_PCI486:     f = romfopen("hot-433.nvr",    "rb"); nvrmask = 127; break;
                case ROM_SIS471:     f = romfopen("sis471.nvr",     "rb"); nvrmask = 127; break;
                case ROM_PXSIS471:   f = romfopen("pxsis471.nvr",   "rb"); nvrmask = 127; break;
                case ROM_COLORBOOK:  f = romfopen("colorbook.nvr",  "rb"); nvrmask = 127; break;
                case ROM_SIS496:     f = romfopen("sis496.nvr",     "rb"); nvrmask = 127; break;
                case ROM_REVENGE:    f = romfopen("revenge.nvr",    "rb"); nvrmask = 127; break;
                case ROM_430LX:      f = romfopen("430lx.nvr",      "rb"); nvrmask = 127; break;
                case ROM_PLATO:      f = romfopen("plato.nvr",      "rb"); nvrmask = 127; break;
                case ROM_430NX:      f = romfopen("430nx.nvr",      "rb"); nvrmask = 127; break;
                case ROM_ENDEAVOR:   f = romfopen("endeavor.nvr",   "rb"); nvrmask = 127; break;
                case ROM_430FX:      f = romfopen("430fx.nvr",      "rb"); nvrmask = 127; break;
                case ROM_430HX:      f = romfopen("430hx.nvr",      "rb"); nvrmask = 127; break;
                case ROM_ACERV35N:   f = romfopen("acerv35n.nvr",   "rb"); nvrmask = 127; break;
                case ROM_430VX:      f = romfopen("430vx.nvr",      "rb"); nvrmask = 127; break;
                case ROM_430TX:      f = romfopen("430tx.nvr",      "rb"); nvrmask = 127; break;
                case ROM_440FX:      f = romfopen("440fx.nvr",      "rb"); nvrmask = 127; break;
                case ROM_440BX:      f = romfopen("440bx.nvr",      "rb"); nvrmask = 127; break;
                case ROM_VPC2007:    f = romfopen("vpc2007.nvr",    "rb"); nvrmask = 127; break;
                default: return;
        }
        if (!f)
        {
                memset(nvrram,0xFF,(nvrmask == 255) ? 256 : 128);
                return;
        }
        fread(nvrram,(nvrmask == 255) ? 256 : 128,1,f);
        fclose(f);
        // nvrram[0xA]=6;
        // nvrram[0xB]=0;
        c=1<<((6&0xF)-1);
        rtctime += (int)(RTCCONST * c * (1 << TIMER_SHIFT));
}

void savenvr()
{
        FILE *f;
        switch (oldromset)
        {
                case ROM_PC1512:     f = romfopen("pc1512.nvr",     "wb"); break;
                case ROM_PC1640:     f = romfopen("pc1640.nvr",     "wb"); break;
                case ROM_PC200:      f = romfopen("pc200.nvr",      "wb"); break;
                case ROM_PC2086:     f = romfopen("pc2086.nvr",     "wb"); break;
                case ROM_PC3086:     f = romfopen("pc3086.nvr",     "wb"); break;
                case ROM_IBMAT:      f = romfopen("at.nvr",         "wb"); break;
		case ROM_IBMPS1_2011: f = romfopen("ibmps1_2011.nvr", "wb"); break;
                case ROM_CMDPC30:    f = romfopen("cmdpc30.nvr",    "wb"); break;                
                case ROM_AMI286:     f = romfopen("ami286.nvr",     "wb"); break;
                case ROM_DELL200:    f = romfopen("dell200.nvr",    "wb"); break;
                case ROM_IBMAT386:   f = romfopen("at386.nvr",      "wb"); break;
                case ROM_ACER386:    f = romfopen("acer386.nvr",    "wb"); break;
                case ROM_MEGAPC:     f = romfopen("megapc.nvr",     "wb"); break;
                case ROM_AMI386:     f = romfopen("ami386.nvr",     "wb"); break;
                case ROM_DESKPRO_386: f = romfopen("deskpro386.nvr", "wb"); break;
                case ROM_PX386:      f = romfopen("px386.nvr",      "wb"); break;
                case ROM_DTK386:     f = romfopen("dtk386.nvr",     "wb"); break;
                case ROM_AMI486:     f = romfopen("ami486.nvr",     "wb"); break;
                case ROM_PX486:      f = romfopen("px486.nvr",      "wb"); break;
                case ROM_WIN486:     f = romfopen("win486.nvr",     "wb"); break;
                case ROM_PCI486:     f = romfopen("hot-433.nvr",    "wb"); break;
                case ROM_SIS471:     f = romfopen("sis471.nvr",     "wb"); break;
                case ROM_PXSIS471:   f = romfopen("pxsis471.nvr",   "wb"); break;
                case ROM_COLORBOOK:  f = romfopen("colorbook.nvr",  "wb"); break;
                case ROM_SIS496:     f = romfopen("sis496.nvr",     "wb"); break;
                case ROM_REVENGE:    f = romfopen("revenge.nvr",    "wb"); break;
                case ROM_430LX:      f = romfopen("430lx.nvr",      "wb"); break;
                case ROM_PLATO:      f = romfopen("plato.nvr",      "wb"); break;
                case ROM_430NX:      f = romfopen("430nx.nvr",      "wb"); break;
                case ROM_ENDEAVOR:   f = romfopen("endeavor.nvr",   "wb"); break;
                case ROM_430FX:      f = romfopen("430fx.nvr",      "wb"); break;
                case ROM_430HX:      f = romfopen("430hx.nvr",      "wb"); break;
                case ROM_ACERV35N:   f = romfopen("acerv35n.nvr",   "wb"); break;
                case ROM_430VX:      f = romfopen("430vx.nvr",      "wb"); break;
                case ROM_430TX:      f = romfopen("430tx.nvr",      "wb"); break;
                case ROM_440FX:      f = romfopen("440fx.nvr",      "wb"); break;
                case ROM_440BX:      f = romfopen("440bx.nvr",      "wb"); break;
                case ROM_VPC2007:    f = romfopen("vpc2007.nvr",    "wb"); break;
                default: return;
        }
        fwrite(nvrram,(nvrmask == 255) ? 256 : 128,1,f);
	if(AMSTRAD)  nvrram[0x20] = 0x19;
        fclose(f);
}

void nvr_init()
{
        io_sethandler(0x0070, 0x0002, readnvr, NULL, NULL, writenvr, NULL, NULL,  NULL);
        timer_add(nvr_rtc, &rtctime, TIMER_ALWAYS_ENABLED, NULL);
        timer_add(nvr_onesec, &nvr_onesec_time, TIMER_ALWAYS_ENABLED, NULL);
}
