#include "ibm.h"
#include "cpu.h"
#include "model.h"
#include "io.h"

#include "acer386sx.h"
#include "ali1429.h"
#include "amstrad.h"
#include "colorbook_io.h"
#include "compaq.h"
#include "cpqio.h"
#include "device.h"
#include "dma.h"
#include "fdc.h"
#include "fdc37c665.h"
#include "fdc37c932fr.h"
#include "gameport.h"
#include "headland.h"
#include "i430lx.h"
#include "i430fx.h"
#include "i430hx.h"
#include "i430vx.h"
#include "i430tx.h"
#include "i440fx.h"
#ifdef BROKEN_CHIPSETS
#include "i450gx.h"
#include "i440bx.h"
#endif
#include "ide.h"
#include "intel.h"
#include "intel_flash.h"
#ifdef BROKEN_CHIPSETS
#include "intel_flash_2mbit.h"
#endif
#include "jim.h"
#include "keyboard_amstrad.h"
#include "keyboard_at.h"
#include "keyboard_olim24.h"
#include "keyboard_pcjr.h"
#include "keyboard_xt.h"
#include "lpt.h"
#include "memregs.h"
#include "mouse_ps2.h"
#include "mouse_serial.h"
#include "neat.h"
#include "nmi.h"
#include "nvr.h"
#include "olivetti_m24.h"
#include "pc87306.h"
#ifdef BROKEN_CHIPSETS
#include "pc87309.h"
#endif
#include "pci.h"
#include "pic.h"
#include "piix.h"
#include "pit.h"
#include "ps1.h"
#include "serial.h"
#include "sio.h"
#include "sis496.h"
#include "sis85c471.h"
#include "sound_sn76489.h"
#ifdef BROKEN_CHIPSETS
#include "superio_detect.h"
#endif
// #include "um8669f.h"
#ifdef BROKEN_CHIPSETS
#include "um8881f.h"
#endif
#include "w83877f.h"
#include "w83977f.h"
#include "wd76c10.h"
#include "xtide.h"

void            xt_init();
void          pcjr_init();
void       tandy1k_init();
void           ams_init();
void        europc_init();
void        olim24_init();
void            at_init();
void         at_init_ex();
void    deskpro386_init();
void         px386_init();
void           ps1_init();
void       at_neat_init();
void  at_acer386sx_init();
void    at_wd76c10_init();
void    at_ali1429_init();
void   at_headland_init();
void     at_sis471_init();
void  at_colorbook_init();
void     at_sis496_init();
void     at_batman_init();
void     at_i430lx_init();
void  at_acerv12lc_init();
void      at_plato_init();
void     at_i430nx_init();
void   at_endeavor_init();
void     at_i430fx_init();
void   at_acerv35n_init();
void     at_i430hx_init();
void     at_i430vx_init();
void     at_i430tx_init();
void     at_i440fx_init();
#ifdef BROKEN_CHIPSETS
void    at_um8881f_init();
void     at_apollo_init();
void    at_goliath_init();
void     at_i440bx_init();
void    at_vpc2007_init();
#endif

int model;

int AMSTRAD, AT, PCI, TANDY;

int has_nsc = 0;

int piix_type = 0;

MODEL models[] =
{
        {"IBM PC",              ROM_IBMPC,     { "Stock", cpus_8088,	"286 card",  cpus_286,   "",  NULL},         0,      xt_init},
        {"IBM XT",              ROM_IBMXT,     { "Stock", cpus_8088,	"286 card",  cpus_286,   "",  NULL},         0,      xt_init},
        {"IBM PCjr",            ROM_IBMPCJR,   { "",      cpus_pcjr,    "",    NULL,         "",      NULL},         1,    pcjr_init},
        {"Generic XT clone",    ROM_GENXT,     { "Stock", cpus_8088,	"286 card",  cpus_286,   "",  NULL},         0,      xt_init},
	{"AMI XT clone",        ROM_AMIXT,     { "Stock", cpus_8088,	"286 card",  cpus_286,   "",  NULL},         0,      xt_init},
        {"DTK XT clone",        ROM_DTKXT,     { "Stock", cpus_8088,	"286 card",  cpus_286,   "",  NULL},         0,      xt_init},
        {"VTech Laser Turbo XT",ROM_LTXT,      { "Stock", cpus_8088,	"286 card",  cpus_286,   "",  NULL},         0,      xt_init},
        {"VTech Laser XT3",	ROM_LXT3,      { "Stock", cpus_8088,	"286 card",  cpus_286,   "",  NULL},         0,      xt_init},
        {"Phoenix XT clone",    ROM_PXXT,      { "Stock", cpus_8088,	"286 card",  cpus_286,   "",  NULL},         0,      xt_init},
        {"Juko XT clone",       ROM_JUKOPC,    { "Stock", cpus_8088,	"286 card",  cpus_286,   "",  NULL},         0,      xt_init},
        {"Kaypro XT clone",     ROM_KAYPROXT,  { "Stock", cpus_8088,	"286 card",  cpus_286,   "",  NULL},         0,      xt_init},
        {"Tandy 1000",          ROM_TANDY,     { "Stock", cpus_8088,	"286 card",  cpus_286,   "",  NULL},         1, tandy1k_init},
        {"Amstrad PC1512",      ROM_PC1512,    { "",      cpus_pc1512,  "",    NULL,         "",      NULL},         1,     ams_init},
        {"Sinclair PC200",      ROM_PC200,     { "",      cpus_8086,    "",    NULL,         "",      NULL},         1,     ams_init},
        {"Euro PC",             ROM_EUROPC,    { "",      cpus_8086,    "",    NULL,         "",      NULL},         0,  europc_init},
        {"Olivetti M24",        ROM_OLIM24,    { "",      cpus_8086,    "",    NULL,         "",      NULL},         1,  olim24_init},        
        {"Amstrad PC1640",      ROM_PC1640,    { "",      cpus_8086,    "",    NULL,         "",      NULL},         1,     ams_init},
        {"Amstrad PC2086",      ROM_PC2086,    { "",      cpus_8086,    "",    NULL,         "",      NULL},         1,     ams_init},        
        {"Amstrad PC3086",      ROM_PC3086,    { "",      cpus_8086,    "",    NULL,         "",      NULL},         1,     ams_init},
        {"IBM AT",              ROM_IBMAT,     { "",      cpus_ibmat,   "",    NULL,         "",      NULL},         0,      at_init_ex},
        {"Commodore PC 30 III", ROM_CMDPC30,   { "",      cpus_286,     "",    NULL,         "",      NULL},         0,      at_init_ex},        
        {"AMI 286 clone",       ROM_AMI286,    { "",      cpus_286,     "",    NULL,         "",      NULL},         0,      at_neat_init},
        {"Achieve Microsys. 286",ROM_AMSYS,    { "",      cpus_286,     "",    NULL,         "",      NULL},         0,      at_neat_init},
        {"DELL System 200",     ROM_DELL200,   { "",      cpus_286,     "",    NULL,         "",      NULL},         0,           at_init_ex},
        {"IBM PS/1 model 2011", ROM_IBMPS1_2011, { "",      cpus_286,     "",    NULL,         "",      NULL},         1,          ps1_init},
        {"Acer 386SX25/N",      ROM_ACER386,   { "Intel", cpus_acer,    "",    NULL,         "",      NULL},         1, at_acer386sx_init},
        {"Amstrad MegaPC",      ROM_MEGAPC,    { "Intel", cpus_i386,    "AMD", cpus_Am386,   "Cyrix", cpus_486SDLC}, 1,   at_wd76c10_init},
        {"AMI 386 clone",       ROM_AMI386,    { "Intel", cpus_i386,    "AMD", cpus_Am386,   "Cyrix", cpus_486SDLC}, 0,  at_headland_init},
        {"Compaq Deskpro 386",  ROM_DESKPRO_386, { "Intel", cpus_i386,    "AMD", cpus_Am386,   "Cyrix", cpus_486SDLC}, 0,   deskpro386_init},
        {"DTK 386SX clone",     ROM_DTK386,      { "Intel", cpus_i386,    "AMD", cpus_Am386,   "Cyrix", cpus_486SDLC}, 0,      at_neat_init},
        {"Phoenix 386 clone",   ROM_PX386,       { "Intel", cpus_i386,    "AMD", cpus_Am386,   "Cyrix", cpus_486SDLC}, 0,  	    at_init_ex},
        {"AMI 486 clone",       ROM_AMI486,    { "Intel", cpus_i486,    "AMD", cpus_Am486,   "Cyrix", cpus_Cx486},   0,   at_ali1429_init},
        {"AMI WinBIOS 486",     ROM_WIN486,    { "Intel", cpus_i486,    "AMD", cpus_Am486,   "Cyrix", cpus_Cx486},   0,   at_ali1429_init},
        {"Phoenix 486 clone",   ROM_PX486,     { "Intel", cpus_i486,    "AMD", cpus_Am486,   "Cyrix", cpus_Cx486},   0,   at_ali1429_init},
#ifdef BROKEN_CHIPSETS
        {"AMI WinBIOS 486 PCI", ROM_PCI486,    { "Intel", cpus_i486,    "AMD", cpus_Am486, "Cyrix", cpus_Cx486},   0,   at_um8881f_init},
#endif
        {"Award SiS 471",       ROM_SIS471,    { "Intel", cpus_i486,    "AMD", cpus_Am486,   "Cyrix", cpus_Cx486},   0,    at_sis471_init},
        {"Phoenix SiS 471",     ROM_PXSIS471,  { "Intel", cpus_i486,    "AMD", cpus_Am486,   "Cyrix", cpus_Cx486},   0,    at_sis471_init},
        {"Gateway 2000 Colorbook",ROM_COLORBOOK, { "Intel", cpus_i486,    "AMD", cpus_Am486,   "Cyrix", cpus_Cx486},   0, at_colorbook_init},
        {"Award SiS 496/497",   ROM_SIS496,    { "Intel", cpus_i486,    "AMD", cpus_Am486,   "Cyrix", cpus_Cx486},   0,    at_sis496_init},
        {"Intel Premiere/PCI",  ROM_REVENGE,   { "Intel", cpus_Pentium5V, "",  NULL,         "",      NULL},         0,    at_batman_init},
        {"Award 430LX PCI",     ROM_430LX,     { "Intel", cpus_Pentium5V, "",  NULL,         "",      NULL},         0,    at_i430lx_init},
        {"Acer V12LC",          ROM_ACERV12LC, { "Intel", cpus_PentiumS5,"IDT", cpus_WinChip, "",      NULL},         0,    at_acerv12lc_init},
        {"Intel Premiere/PCI II",ROM_PLATO,    { "Intel", cpus_PentiumS5,"IDT", cpus_WinChip, "",      NULL},         0,   at_plato_init},
        {"Award 430NX PCI",     ROM_430NX,     { "Intel", cpus_PentiumS5,"IDT", cpus_WinChip, "",      NULL},         0,   at_i430nx_init},
        {"Intel Advanced/EV",   ROM_ENDEAVOR,  { "Intel", cpus_PentiumS5,"IDT", cpus_WinChip, "",      NULL},         0,  at_endeavor_init},
#ifdef BROKEN_CHIPSETS
        {"AMI WinBIOS Pent. PCI",ROM_APOLLO,   { "Intel", cpus_PentiumS5,"IDT", cpus_WinChip, "",      NULL},         0,   at_apollo_init},
#endif
        {"Award 430FX PCI",     ROM_430FX,     { "Intel", cpus_PentiumS5,"IDT", cpus_WinChip, "",      NULL},         0,   at_i430fx_init},
        {"Award 430HX PCI",     ROM_430HX,     { "Intel", cpus_Pentium, "IDT", cpus_WinChip, "AMD",   cpus_K6},      0,    at_i430hx_init},
        {"Acer V35N",           ROM_ACERV35N,  { "Intel", cpus_Pentium, "IDT", cpus_WinChip, "AMD",   cpus_K6},      0,    at_acerv35n_init},
        {"Award 430VX PCI",     ROM_430VX,     { "Intel", cpus_Pentium, "IDT", cpus_WinChip, "AMD",   cpus_K6},      0,    at_i430vx_init},
        {"Award 430TX PCI",     ROM_430TX,     { "Intel", cpus_Pentium, "IDT", cpus_WinChip, "AMD",   cpus_K6},      0,    at_i430tx_init},
        {"Award 440FX PCI",     ROM_440FX,     { "Intel", cpus_PentiumPro,"Klamath",    cpus_Pentium2,         "Deschut.",      cpus_Pentium2D},         0,    at_i440fx_init},
#ifdef BROKEN_CHIPSETS
        {"AMI Goliath 730 PCI", ROM_GOLIATH,   { "Intel", cpus_PentiumPro,"Klamath",    cpus_Pentium2,         "Deschut.",      cpus_Pentium2D},         0,    at_i440fx_init},
        {"Award 440BX PCI",     ROM_440BX,     { "Intel", cpus_Pentium2,"Deschut.",    cpus_Pentium2D,         "",      NULL},         0,    at_i440bx_init},
        {"Virtual PC 2007",     ROM_VPC2007,   { "Intel", cpus_Pentium2,"Deschut.",    cpus_Pentium2D,         "",      NULL},         0,    at_vpc2007_init},
#endif
        {"", -1, {"", 0, "", 0, "", 0}, 0}
};

int model_count()
{
        return (sizeof(models) / sizeof(MODEL)) - 1;
}

int model_getromset()
{
        return models[model].id;
}

int model_getmodel(int romset)
{
	int c = 0;
	
	while (models[c].id != -1)
	{
		if (models[c].id == romset)
			return c;
		c++;
	}
	
	return 0;
}

char *model_getname()
{
        return models[model].name;
}

void fdc_polarity_reset()
{
	densel_polarity = -1;
	densel_polarity_mid[0] = -1;
	densel_polarity_mid[1] = -1;
	drt[0] = 0;
	drt[1] = 0;
}

void common_init()
{
	TANDY = 0;
	AMSTRAD = 0;
        dma_init();
        fdc_add();
        lpt_init();
        pic_init();
        pit_init();
        serial1_init(0x3f8, 4);
        serial2_init(0x2f8, 3);
	memregs_init();
        device_add(&gameport_device);
	/* It then gets set for the models that do need it. */
	fdc_clear_dskchg_activelow();
	fdc_polarity_reset();
	piix_type = 0;
}

void xt_init()
{
	PCI = 0;
	maxide = 2;
	AT = 0;
        common_init();
        pit_set_out_func(1, pit_refresh_timer_xt);
        keyboard_xt_init();
        mouse_serial_init();
        xtide_init();
	nmi_init();
}

void pcjr_init()
{
	PCI = 0;
	maxide = 2;
	AT = 0;
	TANDY = 0;
	AMSTRAD = 0;
        fdc_add_pcjr();
        pic_init();
        pit_init();
        pit_set_out_func(0, pit_irq0_timer_pcjr);
        serial1_init(0x2f8, 3);
        keyboard_pcjr_init();
	// memregs_init();
        device_add(&sn76489_device);
	nmi_mask = 0x80;
	fdc_polarity_reset();
}

void tandy1k_init()
{
	PCI = 0;
	maxide = 2;
	AT = 0;
	TANDY = 1;
        common_init();
	TANDY = 1;
        keyboard_xt_init();
        mouse_serial_init();
        device_add(&sn76489_device);
        xtide_init();
	nmi_init();
	fdc_polarity_reset();
}

void ams_init()
{
	PCI = 0;
	maxide = 2;
	AT = 0;
	AMSTRAD = 1;
        common_init();
	AMSTRAD = 1;
        amstrad_init();
        keyboard_amstrad_init();
        nvr_init();
        xtide_init();
	nmi_init();
	fdc_set_dskchg_activelow();
}

void europc_init()
{
	PCI = 0;
	maxide = 2;
	AT = 0;
        common_init();
        jim_init();
        keyboard_xt_init();
        mouse_serial_init();
        xtide_init();
	nmi_init();
}

void olim24_init()
{
	PCI = 0;
	maxide = 2;
	AT = 0;
        common_init();
        keyboard_olim24_init();
        nvr_init();
        olivetti_m24_init();
        xtide_init();
	nmi_init();
}

void at_init()
{
	AT = 1;
        common_init();
	AT = 1;
        pit_set_out_func(1, pit_refresh_timer_at);
        dma16_init();
        ide_init();
        keyboard_at_init();
        if (models[model].init == at_init)
           mouse_serial_init();
        nvr_init();
        pic2_init();
}

void at_init_ex()
{
	PCI = 0;
	maxide = 2;
	at_init();
}

void deskpro386_init()
{
	PCI = 0;
	maxide = 2;
        at_init();
        mouse_serial_init();
        compaq_init();
	cpqio_init();
}

void px386_init()
{
	PCI = 0;
	maxide = 2;
        at_init();
        mouse_serial_init();
}

void ps1_init()
{
	PCI = 0;
	maxide = 2;
	AT = 1;
        common_init();
        pit_set_out_func(1, pit_refresh_timer_at);
        dma16_init();
        ide_init();
        keyboard_at_init();
        mouse_ps2_init();
        nvr_init();
        pic2_init();
        ps1mb_init();
        fdc_set_dskchg_activelow();
}

void at_neat_init()
{
        at_init();
	PCI = 0;
	maxide = 2;
        mouse_serial_init();
        neat_init();
}

void at_acer386sx_init()
{
        at_init();
	PCI = 0;
	maxide = 2;
        mouse_ps2_init();
        acer386sx_init();
}

void at_wd76c10_init()
{
        at_init();
	PCI = 0;
	maxide = 2;
        mouse_ps2_init();
        wd76c10_init();
}

void at_headland_init()
{
        at_init();
	PCI = 0;
	maxide = 2;
        headland_init();
        mouse_serial_init();
}

void at_ali1429_init()
{
        at_init();
	PCI = 0;
	maxide = 2;
        ali1429_init();
        mouse_serial_init();
}

void at_sis471_init()
{
        at_init();
	PCI = 0;
	maxide = 2;
        mouse_serial_init();
	sis85c471_init();
}

void at_colorbook_init()
{
        at_init();
	PCI = 0;
	maxide = 2;
        mouse_ps2_init();
	/* Really the FDC37C663 but we share the file as they are essentially identical except for the vendor ID. */
	fdc37c665_init();
	colorbook_io_init();
}

void at_sis496_init()
{
	maxide = 4;
        at_init();
        mouse_serial_init();
        pci_init(PCI_CONFIG_TYPE_1, 0, 31);
        // um8663b_init();
	fdc37c665_init();
        device_add(&sis496_device);
}

void at_batman_init()
{
	maxide = 4;
        at_init();
	maxide = 4;
        // mouse_serial_init();
	mouse_ps2_init();
        pci_init(PCI_CONFIG_TYPE_2, 0xd, 0x10);
        i430lx_init();
	sio_init(1);
	fdc37c665_init();
        intel_batman_init();
        device_add(&intel_flash_device);
}

void at_i430lx_init()
{
	maxide = 2;
        at_init();
	maxide = 2;
        ali1429_init();
        mouse_serial_init();
	// mouse_ps2_init();
        pci_init(PCI_CONFIG_TYPE_2, 0xd, 0x10);
        i430lx_init();
	sio_init(1);
	// fdc37c665_init();
        device_add(&intel_flash_device);
}

void at_acerv12lc_init()
{
	maxide = 2;
        at_init();
        pci_init(PCI_CONFIG_TYPE_1, 0, 31);
	PCI = 0;	/* Horrible hack, I know, but if it serves to make *ALL* graphics cards work on this, why not. */
        ali1429_init();
        mouse_serial_init();
        device_add(&intel_flash_b_device);
}

void at_plato_init()
{
	maxide = 4;
        at_init();
	maxide = 4;
        // mouse_serial_init();
	mouse_ps2_init();
        pci_init(PCI_CONFIG_TYPE_2, 0xd, 0x10);
	/* The LX and NX are essentially the same chip. */
        i430lx_init();
	sio_init(1);
	fdc37c665_init();
        intel_batman_init();
        device_add(&intel_flash_device);
}

void at_i430nx_init()
{
	maxide = 2;
        at_init();
	maxide = 2;
        ali1429_init();
        mouse_serial_init();
	// mouse_ps2_init();
        pci_init(PCI_CONFIG_TYPE_2, 0xd, 0x10);
	/* The LX and NX are essentially the same chip. */
        i430lx_init();
	sio_init(1);
        device_add(&intel_flash_device);
}

void at_endeavor_init()
{
	maxide = 4;
        at_init();
	maxide = 4;
        // mouse_serial_init();
	mouse_ps2_init();
        pci_init(PCI_CONFIG_TYPE_1, 0xd, 0x10);
        i430fx_init();
	piix_type = 1;
        piix_init(7);
	pc87306_init();
        intel_endeavor_init();
        device_add(&intel_flash_device);
}

void at_i430fx_init()
{
	maxide = 4;
        at_init();
	maxide = 4;
        mouse_serial_init();
	// mouse_ps2_init();
        pci_init(PCI_CONFIG_TYPE_1, 0, 31);
        i430fx_init();
	piix_type = 1;
        piix_init(7);
        // um8669f_init();
	fdc37c665_init();
        intel_endeavor_init();
        device_add(&intel_flash_device);
}

void at_i430hx_init()
{
	maxide = 4;
        at_init();
	maxide = 4;
        // mouse_serial_init();
	mouse_ps2_init();
        pci_init(PCI_CONFIG_TYPE_1, 0, 31);
        i430hx_init();
	piix_type = 3;
        piix_init(7);
	w83877f_init();
        device_add(&intel_flash_device);
}

void at_acerv35n_init()
{
	maxide = 4;
        at_init();
	maxide = 4;
        mouse_serial_init();
	// mouse_ps2_init();
        pci_init(PCI_CONFIG_TYPE_1, 0, 31);
	PCI = 0;	/* Horrible hack, I know, but if it serves to make *ALL* graphics cards work on this, why not. */
        i430hx_init();
	piix_type = 3;
        piix_init(7);
	fdc37c932fr_init();
        device_add(&intel_flash_b_device);
}

void at_i430vx_init()
{
	maxide = 4;
        at_init();
	maxide = 4;
        mouse_serial_init();
	// mouse_ps2_init();
        pci_init(PCI_CONFIG_TYPE_1, 0, 31);
        i430vx_init();
	piix_type = 3;
        piix_init(7);
        // um8669f_init();
	/* Note by OBattler: Switched to a BIOS using that Super I/O chip because it's better than UMC. */
	fdc37c932fr_init();
        device_add(&intel_flash_device);
}

void at_i430tx_init()
{
	maxide = 4;
        at_init();
	maxide = 4;
        // mouse_serial_init();
	mouse_ps2_init();
        pci_init(PCI_CONFIG_TYPE_1, 0, 31);
        i430tx_init();
	piix_type = 3;
        piix_init(7);
	w83977f_init();
        device_add(&intel_flash_device);
}

void at_i440fx_init()
{
	maxide = 4;
        at_init();
	maxide = 4;
        // mouse_serial_init();
	mouse_ps2_init();
        pci_init(PCI_CONFIG_TYPE_1, 0, 31);
        i440fx_init();
	piix_type = 3;
        piix_init(7);
        // um8669f_init();
	/* Note by OBattler: Switched to a BIOS using that Super I/O chip because it's better than UMC. */
	// fdc37c669_init();
	fdc37c665_init();
        device_add(&intel_flash_device);
}

#ifdef BROKEN_CHIPSETS
void at_um8881f_init()
{
        at_init();
	maxide = 2;
        mouse_serial_init();
        pci_init(PCI_CONFIG_TYPE_2, 0xd, 0x10);
        // pci_init(PCI_CONFIG_TYPE_1, 0, 31);
        um8881f_init();
}

void at_apollo_init()
{
        at_init();
	maxide = 4;
        // mouse_serial_init();
	mouse_ps2_init();
        pci_init(PCI_CONFIG_TYPE_2, 0xd, 0x10);
        // pci_init(PCI_CONFIG_TYPE_1, 0xd, 0x10);
        i430fx_init();
	piix_type = 1;
        piix_init(7);
	superio_detect_init();
        intel_endeavor_init();
        device_add(&intel_flash_device);
}

void at_goliath_init()
{
        at_init();
	maxide = 4;
        mouse_serial_init();
	// mouse_ps2_init();
        pci_init(PCI_CONFIG_TYPE_1, 0, 31);
        i450gx_init();
	piix_type = 3;
        piix_init(7);
	fdc37c665_init();
}

void at_i440bx_init()
{
        at_init();
	maxide = 4;
        mouse_serial_init();
	// mouse_ps2_init();
        pci_init(PCI_CONFIG_TYPE_1, 0, 31);
        i440bx_init();
	piix_type = 4;
        piix_init(7);
        // um8669f_init();
	/* Note by OBattler: Switched to a BIOS using that Super I/O chip because it's better than UMC. */
	// fdc37c932fr_init();
	pc87309_init();
        device_add(&intel_flash_2mbit_device);
}

void at_vpc2007_init()
{
        at_init();
	maxide = 4;
        mouse_serial_init();
	// mouse_ps2_init();
        pci_init(PCI_CONFIG_TYPE_1, 0, 31);
        i440bx_init();
	piix_type = 4;
        piix_init(7);
        device_add(&intel_flash_2mbit_device);
}
#endif

void model_init()
{
        // pclog("Initting as %s\n", model_getname());
        io_init();
        
	has_nsc = 0;
        models[model].init();

	// pclog("PCI: %02X, Max IDE: %02X\n", PCI, maxide);
}
