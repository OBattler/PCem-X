#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include "ibm.h"
#include "device.h"

#include "ali1429.h"
#include "amstrad.h"
#include "cdrom-ioctl.h"
#include "mem.h"
#include "x86_ops.h"
#include "codegen.h"
#include "cdrom-null.h"
#include "config.h"
#include "cpu.h"
#include "dma.h"
#include "fdc.h"
#include "floppy.h"
#include "sound_gus.h"
#include "ide.h"
#include "keyboard.h"
#include "model.h"
#include "mouse.h"
#include "nvr.h"
#include "pc.h"
#include "pic.h"
#include "pit.h"
#include "plat-joystick.h"
#include "plat-mouse.h"
#include "serial.h"
#include "sound.h"
#include "sound_cms.h"
#include "sound_opl.h"
#include "sound_sb.h"
#include "sound_ssi2001.h"
#include "timer.h"
#include "vid_voodoo.h"
#include "video.h"
#include "win-display.h"
//NETWORK
//Make sure your makefile defines USE_NETWORKING
//if you want this code...
// #ifdef USE_NETWORKING
#include "nethandler.h"
#define NE2000      1
#define RTL8029AS   2
uint8_t ethif;
int inum;
// #endif

int frame = 0;

int cdrom_enabled;
int CPUID;
int vid_resize, vid_api;

int cycles_lost = 0;

int clockrate;
int insc=0;
float mips,flops;
extern int mmuflush;
extern int readlnum,writelnum;
void fullspeed();

int framecount,fps;
int intcount;

int output;
int atfullspeed;

void saveconfig();
int infocus;
int mousecapture;
FILE *pclogf;
void pclog(const char *format, ...)
{
#ifndef RELEASE_BUILD
   char buf[1024];
   //return;
   if (!pclogf)
      pclogf=fopen("pclog.txt","wt");
//return;
   va_list ap;
   va_start(ap, format);
   vsprintf(buf, format, ap);
   va_end(ap);
   fputs(buf,pclogf);
//fflush(pclogf);
#endif
}

void fatal(const char *format, ...)
{
   char buf[256];
//   return;
   if (!pclogf)
      pclogf=fopen("pclog.txt","wt");
//return;
   va_list ap;
   va_start(ap, format);
   vsprintf(buf, format, ap);
   va_end(ap);
   fputs(buf,pclogf);
   fflush(pclogf);
   dumppic();
   dumpregs();
   exit(-1);
}

uint8_t cgastat;

int pollmouse_delay = 2;
void pollmouse()
{
        int x,y;
//        return;
        pollmouse_delay--;
        if (pollmouse_delay) return;
        pollmouse_delay = 2;
        mouse_poll_host();
        mouse_get_mickeys(&x,&y);
        if (mouse_poll)
           mouse_poll(x, y, mouse_buttons);
//        if (mousecapture) position_mouse(64,64);
}

/*PC1512 languages -
  7=English
  6=German
  5=French
  4=Spanish
  3=Danish
  2=Swedish
  1=Italian
        3,2,1 all cause the self test to fail for some reason
  */

int cpuspeed2;

int clocks[3][34][4]=
{
	// 4772728
        {
                {4772727,13920,65625,5965},  /*4.77MHz*/
                {8000000,23333,110000,0}, /*8MHz*/
                {10000000,29166,137500,0}, /*10MHz*/
                {12000000,35000,165000,0}, /*12MHz*/
                {16000000,46666,220000,0}, /*16MHz*/
        },
        {
                {8000000,23333,110000,0}, /*8MHz*/
                {12000000,35000,165000,0}, /*12MHz*/
                {16000000,46666,220000,0}, /*16MHz*/
                {20000000,58333,275000,0}, /*20MHz*/
                {25000000,72916,343751,0}, /*25MHz*/
        },
        {
                {16000000, 46666,220000,0},	/* 0:  16MHz*/
                {20000000, 58333,275000,0},	/* 1:  20MHz*/
                {25000000, 72916,343751,0},	/* 2:  25MHz*/
                {33333333, 96000,454000,0},	/* 3:  33MHz*/
                {40000000,116666,550000,0},	/* 4:  40MHz*/
                {50000000, 72916*2,343751*2,0},	/* 5:  50MHz*/
		{60000000, 87500*2,412500*2,0},	/* 6:  60MHz*/
		{62500000, 91145*2,429689*2,0}, /* 7:  63MHz*/
                {66666666, 96000*2,454000*2,0}, /* 8:  66MHz*/
                {75000000, 72916*3,343751*3,0},	/* 9:  75MHz*/
                {80000000,116666*2,550000*2,0},	/*10:  80MHz*/
		{83333333,120000*2,567500*2,0}, /*11:  83MHz*/
                {90000000, 87500*3,412500*3,0},	/*12:  90MHz*/
                {100000000, 72916*4,343751*4,0},/*13: 100MHz*/
                {120000000,116666*3,550000*3,0},/*14: 120MHz*/
                {125000000, 72916*5,343751*5,0},/*15: 125MHz*/
                {133333333, 96000*4,454000*4,0},/*16: 133MHz*/
                {150000000, 72916*6,343751*6,0},/*17: 150MHz*/
                {160000000,116666*4,550000*4,0},/*18: 160MHz*/
                {166666666, 96000*5,454000*5,0},/*19: 166MHz*/
                {180000000, 87500*6,412500*6,0},/*20: 180MHz*/
                {200000000, 72916*8,343751*8,0},/*21: 200MHz*/
                {200000000, 87500*7,412500*7,0},/*22: 210MHz*/
                {225000000, 72916*9,343751*9,0},/*23: 225MHz*/
                {233333333, 96000*7,454000*7,0},/*24: 233MHz*/
                {240000000,116666*6,550000*6,0},/*25: 240MHz*/
                {266666666, 96000*8,454000*8,0},/*26: 266MHz*/
                {270000000, 87500*9,412500*9,0},/*27: 270MHz*/
                {300000000, 72916*12,343751*12,0},/*28: 300MHz*/
                {333333333, 96000*10,454000*10,0},/*29: 333MHz*/
                {350000000, 72916*14,343751*14,0},/*30: 350MHz*/
                {400000000, 72916*16,343751*16,0},/*31: 400MHz*/
                {450000000, 72916*18,343751*18,0},/*32: 450MHz*/
                {500000000, 72916*20,343751*20,0},/*33: 500MHz*/
        }
};

int updatestatus;
int win_title_update=0;


void onesec()
{
        fps=framecount;
        framecount=0;
        win_title_update=1;
}

void pc_reset()
{
        cpu_set();
        resetx86();
        mem_updatecache();
        //timer_reset();
        dma_reset();
        fdc_reset();
        pic_reset();
        pit_reset();
        serial_reset();

        setpitclock(models[model].cpu[cpu_manufacturer].cpus[cpu].rspeed);
        
//        sb_reset();

        ali1429_reset();
//        video_init();
}

void initpc()
{
        char *p;
//        allegro_init();
        get_executable_name(pcempath,511);
        pclog("executable_name = %s\n", pcempath);
        p=get_filename(pcempath);
        *p=0;
        pclog("path = %s\n", pcempath);        

	fdd_init();
        keyboard_init();
        mouse_init();
        joystick_init();
        midi_init();
        
        loadconfig(NULL);
        pclog("Config loaded\n");

	loadfont("mda.rom", 0, cga_fontdat, cga_fontdatm);
	loadfont("roms/pc1512/40078.ic127", 0, pc1512_fontdat, pc1512_fontdatm);
	loadfont("roms/pc200/40109.bin", 0, pc200_fontdat, pc200_fontdatm);

        codegen_init();
        
        cpuspeed2=(AT)?2:1;
//        cpuspeed2=cpuspeed;
        atfullspeed=0;

        device_init();        
        
        initvideo();
        mem_init();
        loadbios();
        mem_add_bios();
                        
        timer_reset();
        sound_reset();
	fdc_init();
// #ifdef USE_NETWORKING
	vlan_reset();	//NETWORK
	network_card_init(network_card_current);
// #endif

        loaddisc(0,discfns[0]);
        loaddisc(1,discfns[1]);
                
        //loadfont();
        loadnvr();
        sound_init();
        resetide();
#if __unix
	if (cdrom_drive == -1)
	        cdrom_null_open(cdrom_drive);	
	else
#endif
	        ioctl_open(cdrom_drive);
        
        pit_reset();        
/*        if (romset==ROM_AMI386 || romset==ROM_AMI486) */fullspeed();
        ali1429_reset();
//        CPUID=(is486 && (cpuspeed==7 || cpuspeed>=9));
//        pclog("Init - CPUID %i %i\n",CPUID,cpuspeed);
        shadowbios=0;
        
#if __unix
	if (cdrom_drive == -1)
	        cdrom_null_reset();	
	else
#endif
	        ioctl_reset();
}

void resetpc()
{
        pc_reset();
//        cpuspeed2=(AT)?2:1;
//        atfullspeed=0;
///*        if (romset==ROM_AMI386 || romset==ROM_AMI486) */fullspeed();
        shadowbios=0;
}

void pc_keyboard_send(uint8_t val)
{
	if (AT)
	{
		keyboard_at_adddata_keyboard_raw(val);
	}
	else
	{
		keyboard_send(val);
	}
}

void resetpc_cad()
{
	pc_keyboard_send(29);	/* Ctrl key pressed */
	pc_keyboard_send(56);	/* Alt key pressed */
	pc_keyboard_send(83);	/* Delete key pressed */
	pc_keyboard_send(157);	/* Ctrl key released */
	pc_keyboard_send(184);	/* Alt key released */
	pc_keyboard_send(211);	/* Delete key released */
}

void ctrl_alt_esc()
{
	pc_keyboard_send(0x1D);	/* Ctrl key pressed */
	pc_keyboard_send(0x38);	/* Alt key pressed */
	pc_keyboard_send(0x01);	/* Esc key pressed */
	pc_keyboard_send(0x81);	/* Esc key released */
	pc_keyboard_send(0xB8);	/* Alt key released */
	pc_keyboard_send(0x9D);	/* Ctrl key released */
}

void simple_del()
{
	// pc_keyboard_send(83);	/* Delete key pressed */
	// pc_keyboard_send(211);	/* Delete key released */
	// pc_keyboard_send(0x58);	/* Delete key pressed */
	// pc_keyboard_send(0xd8);	/* Delete key released */
	pc_keyboard_send(0x3b);	/* Delete key pressed */
	pc_keyboard_send(0xbb);	/* Delete key released */
}

int pcfirsttime = 1;

void resetpchard()
{
	if (!modelchanged)
	        device_close_all();
	else
		modelchanged = 0;
        device_init();
        
        midi_close();
        midi_init();
        
        timer_reset();
        sound_reset();
        mem_resize();

	if (pcfirsttime)
	{
		fdc_init();
		pcfirsttime = 0;
	}
	else
	        fdc_hard_reset();
        
        model_init();
        video_init();
        speaker_init();

// #ifdef USE_NETWORKING
	vlan_reset();	//NETWORK
	network_card_init(network_card_current);      
// #endif
        
        sound_card_init(sound_card_current);
        if (GUS)
                device_add(&gus_device);
        if (GAMEBLASTER)
                device_add(&cms_device);
        if (SSI2001)
                device_add(&ssi2001_device);
        if (voodoo_enabled)
                device_add(&voodoo_device);        
        pc_reset();
        
        resetide();
        
        loadnvr();

//        cpuspeed2 = (AT)?2:1;
//        atfullspeed = 0;
//        setpitclock(models[model].cpu[cpu_manufacturer].cpus[cpu].rspeed);

        shadowbios = 0;
        ali1429_reset();
        
        keyboard_at_reset();
        
//        output=3;

#if __unix
	if (cdrom_drive == -1)
	        cdrom_null_reset();	
	else
#endif
	        ioctl_reset();
}

char romsets[17][40]={"IBM PC","IBM XT","Generic Turbo XT","Euro PC","Tandy 1000","Amstrad PC1512","Sinclair PC200","Amstrad PC1640","IBM AT","AMI 286 clone","Dell System 200","Misc 286","IBM AT 386","Misc 386","386 clone","486 clone","486 clone 2"};
char clockspeeds[3][12][16]=
{
        {"4.77MHz","8MHz","10MHz","12MHz","16MHz"},
        {"8MHz","12MHz","16MHz","20MHz","25MHz"},
        {"16MHz","20MHz","25MHz","33MHz","40MHz","50MHz","66MHz","75MHz","80MHz","100MHz","120MHz","133MHz"},
};
int framecountx=0;
int sndcount=0;
int oldat70hz;

int sreadlnum,swritelnum,segareads,segawrites, scycles_lost;

int serial_fifo_read, serial_fifo_write;

int emu_fps = 0;

uint8_t is_nonat_286()
{
	if ((romset == ROM_IBMPC) || (romset == ROM_IBMXT) || (romset == ROM_GENXT) || (romset == ROM_AMIXT) || (romset == ROM_DTKXT) || (romset == ROM_LTXT) || (romset == ROM_LXT3))
	{
		if (cpu_manufacturer == 1)
			return 1;
		else
			return 0;
	}
	else
		return 0;
}

void runpc()
{
        char s[200];
        int done=0;

        startblit();
        clockrate = models[model].cpu[cpu_manufacturer].cpus[cpu].rspeed;
        
        if (is386)   
        {
                if (cpu_use_dynarec)
                        exec386_dynarec(models[model].cpu[cpu_manufacturer].cpus[cpu].rspeed / (turbo ? 100 : 200));
                else
                        exec386(models[model].cpu[cpu_manufacturer].cpus[cpu].rspeed / (turbo ? 100 : 200));
        }
        else if (AT || is_nonat_286())
                exec386(models[model].cpu[cpu_manufacturer].cpus[cpu].rspeed / (turbo ? 100 : 200));
        else
                execx86(models[model].cpu[cpu_manufacturer].cpus[cpu].rspeed / (turbo ? 100 : 200));
        
                keyboard_poll_host();
                keyboard_process();
//                checkkeys();
                pollmouse();
                joystick_poll();
        endblit();

                framecountx++;
                framecount++;
                if (framecountx>=100)
                {
                        pclog("onesec\n");
                        framecountx=0;
                        mips=(float)insc/1000000.0f;
                        insc=0;
                        flops=(float)fpucount/1000000.0f;
                        fpucount=0;
                        sreadlnum=readlnum;
                        swritelnum=writelnum;
                        segareads=egareads;
                        segawrites=egawrites;
                        scycles_lost = cycles_lost;

                        cpu_recomp_blocks_latched = cpu_recomp_blocks;
                        cpu_recomp_ins_latched = cpu_recomp_ins;
                        cpu_recomp_full_ins_latched = cpu_recomp_full_ins;
                        cpu_new_blocks_latched = cpu_new_blocks;
                        cpu_recomp_flushes_latched = cpu_recomp_flushes;
                        cpu_recomp_evicted_latched = cpu_recomp_evicted;
                        cpu_recomp_reuse_latched = cpu_recomp_reuse;
                        cpu_recomp_removed_latched = cpu_recomp_removed;
                        cpu_reps_latched = cpu_reps;
                        cpu_notreps_latched = cpu_notreps;
                                                
                        cpu_recomp_blocks = 0;
                        cpu_recomp_ins = 0;
                        cpu_recomp_full_ins = 0;
                        cpu_new_blocks = 0;
                        cpu_recomp_flushes = 0;
                        cpu_recomp_evicted = 0;
                        cpu_recomp_reuse = 0;
                        cpu_recomp_removed = 0;
                        cpu_reps = 0;
                        cpu_notreps = 0;

                        updatestatus=1;
                        readlnum=writelnum=0;
                        egareads=egawrites=0;
                        cycles_lost = 0;
                        mmuflush=0;
                        intcount=0;
                        intcount=pitcount=0;
                        emu_fps = frames;
                        frames = 0;
                }
                if (win_title_update)
                {
                        win_title_update=0;

		        if (is386)
		        {
		                if (cpu_use_dynarec)
		                        sprintf(s, "PCem-X v10 [DYN] - %i%% - %s - %s - %s", fps, model_getname(), models[model].cpu[cpu_manufacturer].cpus[cpu].name, (!mousecapture) ? "Click to capture mouse" : "Press CTRL-END or middle button to release mouse");
		                else
		                        sprintf(s, "PCem-X v10 [INT 286/386] - %i%% - %s - %s - %s", fps, model_getname(), models[model].cpu[cpu_manufacturer].cpus[cpu].name, (!mousecapture) ? "Click to capture mouse" : "Press CTRL-END or middle button to release mouse");
		        }
		        else if (AT || is_nonat_286())
		                sprintf(s, "PCem-X v10 [INT 286/386] - %i%% - %s - %s - %s", fps, model_getname(), models[model].cpu[cpu_manufacturer].cpus[cpu].name, (!mousecapture) ? "Click to capture mouse" : "Press CTRL-END or middle button to release mouse");
		        else
		                sprintf(s, "PCem-X v10 [INT 808x] - %i%% - %s - %s - %s", fps, model_getname(), models[model].cpu[cpu_manufacturer].cpus[cpu].name, (!mousecapture) ? "Click to capture mouse" : "Press CTRL-END or middle button to release mouse");

                        set_window_title(s);
                }
                done++;
                frame++;
}

void fullspeed()
{
        cpuspeed2=cpuspeed;
        if (!atfullspeed)
        {
                printf("Set fullspeed - %i %i %i\n",is386,AT,cpuspeed2);
                setpitclock(models[model].cpu[cpu_manufacturer].cpus[cpu].rspeed);
//                if (is386) setpitclock(clocks[2][cpuspeed2][0]);
//                else       setpitclock(clocks[AT?1:0][cpuspeed2][0]);
        }
        atfullspeed=1;
        nvr_recalc();
}

void speedchanged()
{
        if (atfullspeed)
        {
                cpuspeed2=cpuspeed;
                setpitclock(models[model].cpu[cpu_manufacturer].cpus[cpu].rspeed);
//                if (is386) setpitclock(clocks[2][cpuspeed2][0]);
//                else       setpitclock(clocks[AT?1:0][cpuspeed2][0]);
        }
        mem_updatecache();
        nvr_recalc();
}

void closepc()
{
        atapi->exit();
//        ioctl_close();
        dumppic();
//        output=7;
//        setpitclock(clocks[0][0][0]);
//        while (1) runpc();
        savedisc(0);
        savedisc(1);
        dumpregs();
        closevideo();
        device_close_all();
        midi_close();
}

/*int main()
{
        initpc();
        while (!key[KEY_F11])
        {
                runpc();
        }
        closepc();
        return 0;
}

END_OF_MAIN();*/

int cga_comp=0;

void loadconfig(char *fn)
{
        char s[512];
        char *p;
        
        if (!fn)
        {
                append_filename(config_file_default, pcempath, "pcem.cfg", 511);
        
                config_load(config_file_default);
        }
        else
                config_load(fn);
        
        GAMEBLASTER = config_get_int(NULL, "gameblaster", 0);
        GUS = config_get_int(NULL, "gus", 0);
        SSI2001 = config_get_int(NULL, "ssi2001", 0);
        voodoo_enabled = config_get_int(NULL, "voodoo", 0);

// #ifdef USE_NETWORKING
	//network
	ethif = config_get_int(NULL, "netinterface", 1);
        if (ethif >= inum)
            inum = ethif + 1;
        network_card_current = config_get_int(NULL, "netcard", NE2000);
// #endif
        
        model = config_get_int(NULL, "model", 14);

        if (model >= model_count())
                model = model_count() - 1;

        romset = model_getromset();
        cpu_manufacturer = config_get_int(NULL, "cpu_manufacturer", 0);
        cpu = config_get_int(NULL, "cpu", 0);
        cpu_use_dynarec = config_get_int(NULL, "cpu_use_dynarec", 0);
        
        gfxcard = config_get_int(NULL, "gfxcard", 0);
        video_speed = config_get_int(NULL, "video_speed", 3);
        sound_card_current = config_get_int(NULL, "sndcard", SB2);

        p = (char *)config_get_string(NULL, "disc_a", "");
        if (p) strcpy(discfns[0], p);
        else   strcpy(discfns[0], "");

        p = (char *)config_get_string(NULL, "disc_b", "");
        if (p) strcpy(discfns[1], p);
        else   strcpy(discfns[1], "");

        mem_size = config_get_int(NULL, "mem_size", 4);
        cdrom_drive = config_get_int(NULL, "cdrom_drive", 0);
        cdrom_enabled = config_get_int(NULL, "cdrom_enabled", 0);
        
        slowega = config_get_int(NULL, "slow_video", 1);
        cache = config_get_int(NULL, "cache", 3);
        cga_comp = config_get_int(NULL, "cga_composite", 0);
        
        vid_resize = config_get_int(NULL, "vid_resize", 0);
        vid_api = config_get_int(NULL, "vid_api", 0);
        video_fullscreen_scale = config_get_int(NULL, "video_fullscreen_scale", 0);
        video_fullscreen_first = config_get_int(NULL, "video_fullscreen_first", 1);

        hdc[0].spt = config_get_int(NULL, "hdc_sectors", 0);
        hdc[0].hpc = config_get_int(NULL, "hdc_heads", 0);
        hdc[0].tracks = config_get_int(NULL, "hdc_cylinders", 0);
        p = (char *)config_get_string(NULL, "hdc_fn", "");
        if (p) strcpy(ide_fn[0], p);
        else   strcpy(ide_fn[0], "");
        hdc[1].spt = config_get_int(NULL, "hdd_sectors", 0);
        hdc[1].hpc = config_get_int(NULL, "hdd_heads", 0);
        hdc[1].tracks = config_get_int(NULL, "hdd_cylinders", 0);
        p = (char *)config_get_string(NULL, "hdd_fn", "");
        if (p) strcpy(ide_fn[1], p);
        else   strcpy(ide_fn[1], "");
        hdc[2].spt = config_get_int(NULL, "hde_sectors", 0);
        hdc[2].hpc = config_get_int(NULL, "hde_heads", 0);
        hdc[2].tracks = config_get_int(NULL, "hde_cylinders", 0);
        p = (char *)config_get_string(NULL, "hde_fn", "");
        if (p) strcpy(ide_fn[2], p);
        else   strcpy(ide_fn[2], "");
        hdc[3].spt = config_get_int(NULL, "hdf_sectors", 0);
        hdc[3].hpc = config_get_int(NULL, "hdf_heads", 0);
        hdc[3].tracks = config_get_int(NULL, "hdf_cylinders", 0);
        p = (char *)config_get_string(NULL, "hdf_fn", "");
        if (p) strcpy(ide_fn[3], p);
        else   strcpy(ide_fn[3], "");

        p = (char *)config_get_string(NULL, "disc_a", "");
        if (p) strcpy(discfns[0], p);
        else   strcpy(discfns[0], "");
	configure_from_int(0, config_get_int(NULL, "drivetype_a", 8));

        p = (char *)config_get_string(NULL, "disc_b", "");
        if (p) strcpy(discfns[1], p);
        else   strcpy(discfns[1], "");
	configure_from_int(1, config_get_int(NULL, "drivetype_b", 8));

	force_43 = config_get_int(NULL, "force_43", 0);
	enable_overscan = config_get_int(NULL, "enable_overscan", 1);
	ps1xtide = config_get_int(NULL, "ps1xtide", 0);

        disable_xchg_dynarec = config_get_int(NULL, "disable_xchg_dynarec", 0);
        turbo = config_get_int(NULL, "turbo", 1);

        cga_color_burst = config_get_int(NULL, "cga_color_burst", 1);

        cga_brown = config_get_int(NULL, "cga_brown", 1);
}

void saveconfig()
{
        config_set_int(NULL, "gameblaster", GAMEBLASTER);
        config_set_int(NULL, "gus", GUS);
        config_set_int(NULL, "ssi2001", SSI2001);
        config_set_int(NULL, "voodoo", voodoo_enabled);

//NETWORK
// #ifdef USE_NETWORKING
	config_set_int(NULL, "netinterface", ethif);
	config_set_int(NULL, "netcard", network_card_current);
// #endif
        
        config_set_int(NULL, "model", model);
        config_set_int(NULL, "cpu_manufacturer", cpu_manufacturer);
        config_set_int(NULL, "cpu", cpu);
        config_set_int(NULL, "cpu_use_dynarec", cpu_use_dynarec);
        
        config_set_int(NULL, "gfxcard", gfxcard);
        config_set_int(NULL, "video_speed", video_speed);
        config_set_int(NULL, "sndcard", sound_card_current);
        config_set_int(NULL, "cpu_speed", cpuspeed);
        config_set_int(NULL, "has_fpu", hasfpu);
        config_set_int(NULL, "slow_video", slowega);
        config_set_int(NULL, "cache", cache);
        config_set_int(NULL, "cga_composite", cga_comp);
        config_set_string(NULL, "disc_a", discfns[0]);
        config_set_string(NULL, "disc_b", discfns[1]);
        config_set_int(NULL, "mem_size", mem_size);
        config_set_int(NULL, "cdrom_drive", cdrom_drive);
        config_set_int(NULL, "cdrom_enabled", cdrom_enabled);
        config_set_int(NULL, "vid_resize", vid_resize);
        config_set_int(NULL, "vid_api", vid_api);
        config_set_int(NULL, "video_fullscreen_scale", video_fullscreen_scale);
        config_set_int(NULL, "video_fullscreen_first", video_fullscreen_first);
        
        config_set_int(NULL, "hdc_sectors", hdc[0].spt);
        config_set_int(NULL, "hdc_heads", hdc[0].hpc);
        config_set_int(NULL, "hdc_cylinders", hdc[0].tracks);
        config_set_string(NULL, "hdc_fn", ide_fn[0]);
        config_set_int(NULL, "hdd_sectors", hdc[1].spt);
        config_set_int(NULL, "hdd_heads", hdc[1].hpc);
        config_set_int(NULL, "hdd_cylinders", hdc[1].tracks);
        config_set_string(NULL, "hdd_fn", ide_fn[1]);
        config_set_int(NULL, "hde_sectors", hdc[2].spt);
        config_set_int(NULL, "hde_heads", hdc[2].hpc);
        config_set_int(NULL, "hde_cylinders", hdc[2].tracks);
        config_set_string(NULL, "hde_fn", ide_fn[2]);
        config_set_int(NULL, "hdf_sectors", hdc[3].spt);
        config_set_int(NULL, "hdf_heads", hdc[3].hpc);
        config_set_int(NULL, "hdf_cylinders", hdc[3].tracks);
        config_set_string(NULL, "hdf_fn", ide_fn[3]);

        config_set_int(NULL, "drivetype_a", int_from_config(0));
        config_set_int(NULL, "drivetype_b", int_from_config(1));

        config_set_int(NULL, "force_43", force_43);
        config_set_int(NULL, "enable_overscan", enable_overscan);
        config_set_int(NULL, "ps1xtide", ps1xtide);

        config_set_int(NULL, "disable_xchg_dynarec", disable_xchg_dynarec);
        config_set_int(NULL, "turbo", turbo);

        config_set_int(NULL, "cga_color_burst", cga_color_burst);
        config_set_int(NULL, "cga_brown", cga_brown);

        config_save(config_file_default);
}
