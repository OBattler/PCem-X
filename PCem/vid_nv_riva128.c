/*nVidia RIVA 128 emulation*/
#include <stdlib.h>
#include "ibm.h"
#include "device.h"
#include "io.h"
#include "mem.h"
#include "pci.h"
#include "rom.h"
#include "thread.h"
#include "timer.h"
#include "video.h"
#include "vid_nv_riva128.h"
#include "vid_svga.h"
#include "vid_svga_render.h"

typedef struct riva128_t
{
  mem_mapping_t   linear_mapping;
  mem_mapping_t     mmio_mapping;

  rom_t bios_rom;

  svga_t svga;

  uint32_t linear_base, linear_size;

  uint16_t rma_addr;

  uint8_t pci_regs[256];

  int memory_size;

  uint8_t ext_regs_locked;

  uint8_t read_bank;
  uint8_t write_bank;

  struct
  {
    int width;
    int height;
    int bpp;
    uint32_t config_0;
  } pfb;

  struct
  {
    uint32_t gen_ctrl;
  } pramdac;

  struct
  {
    uint32_t addr;
    uint32_t data;
    uint8_t access_reg[4];
    uint8_t mode;
  } rma;

} riva128_t;

static uint8_t riva128_pci_read(int func, int addr, void *p);

static uint8_t riva128_pmc_read(uint32_t addr, void *p)
{
  riva128_t *riva128 = (riva128_t *)p;
  svga_t *svga = &riva128->svga;
  uint8_t ret = 0;

  pclog("RIVA 128 PMC read %08X %04X:%08X\n", addr, CS, pc);

  switch(addr)
  {
  case 0x000000: ret = 0x11; break;
  case 0x000001: ret = 0x01; break;
  case 0x000002: ret = 0x03; break;
  case 0x000003: ret = 0x00; break;
  }

  return ret;
}

static uint8_t riva128_pbus_read(uint32_t addr, void *p)
{
  riva128_t *riva128 = (riva128_t *)p;
  svga_t *svga = &riva128->svga;
  uint8_t ret = 0;

  pclog("RIVA 128 PBUS read %08X %04X:%08X\n", addr, CS, pc);

  switch(addr)
  {
  case 0x001800 ... 0x0018ff: ret = riva128_pci_read(0, addr - 0x1800, riva128); break;
  }

  return ret;
}

static uint8_t riva128_pfb_read(uint32_t addr, void *p)
{
  riva128_t *riva128 = (riva128_t *)p;
  svga_t *svga = &riva128->svga;
  uint8_t ret = 0;

  pclog("RIVA 128 PFB read %08X %04X:%08X\n", addr, CS, pc);

  switch(addr)
  {
  case 0x100000: ret = 0x06; break;
  case 0x100200: ret = riva128->pfb.config_0 & 0xff; break;
  case 0x100201: ret = (riva128->pfb.config_0 >> 8) & 0xff; break;
  case 0x100202: ret = (riva128->pfb.config_0 >> 16) & 0xff; break;
  case 0x100203: ret = (riva128->pfb.config_0 >> 24) & 0xff; break;
  }

  return ret;
}

static void riva128_pfb_write(uint32_t addr, uint32_t val, void *p)
{
  riva128_t *riva128 = (riva128_t *)p;
  svga_t *svga = &riva128->svga;
  pclog("RIVA 128 PFB write %08X %08X %04X:%08X\n", addr, val, CS, pc);

  switch(addr)
  {
  case 0x100200:
  riva128->pfb.config_0 = val;
  riva128->pfb.width = (val & 0x3f) << 5;
  switch((val >> 8) & 3)
  {
  case 1: riva128->pfb.bpp = 8; break;
  case 2: riva128->pfb.bpp = 16; break;
  case 3: riva128->pfb.bpp = 32; break;
  }
  break;
  }
}

static uint8_t riva128_pramdac_read(uint32_t addr, void *p)
{
  riva128_t *riva128 = (riva128_t *)p;
  svga_t *svga = &riva128->svga;
  uint8_t ret = 0;

  pclog("RIVA 128 PRAMDAC read %08X %04X:%08X\n", addr, CS, pc);

  switch(addr)
  {
  case 0x680600: ret = riva128->pramdac.gen_ctrl & 0xff; break;
  case 0x680601: ret = (riva128->pramdac.gen_ctrl >> 8) & 0xff; break;
  case 0x680602: ret = (riva128->pramdac.gen_ctrl >> 16) & 0xff; break;
  case 0x680603: ret = (riva128->pramdac.gen_ctrl >> 24) & 0xff; break;
  }

  return ret;
}

static void riva128_pramdac_write(uint32_t addr, uint32_t val, void *p)
{
  riva128_t *riva128 = (riva128_t *)p;
  svga_t *svga = &riva128->svga;
  pclog("RIVA 128 PRAMDAC write %08X %08X %04X:%08X\n", addr, val, CS, pc);

  switch(addr)
  {
  case 0x680600:
  riva128->pramdac.gen_ctrl = val;
  break;
  }
}

static uint8_t riva128_mmio_read(uint32_t addr, void *p)
{
  riva128_t *riva128 = (riva128_t *)p;
  svga_t *svga = &riva128->svga;
  uint8_t ret = 0;

  addr &= 0xffffff;

  pclog("RIVA 128 MMIO read %08X %04X:%08X\n", addr, CS, pc);

  switch(addr)
  {
  case 0x000000 ... 0x000fff:
  ret = riva128_pmc_read(addr, riva128);
  break;
  case 0x001000 ... 0x001fff:
  ret = riva128_pbus_read(addr, riva128);
  break;
  case 0x100000 ... 0x100fff:
  ret = riva128_pfb_read(addr, riva128);
  break;
  case 0x680000 ... 0x680fff:
  ret = riva128_pramdac_read(addr, riva128);
  break;
  }
  return ret;
}

static uint16_t riva128_mmio_read_w(uint32_t addr, void *p)
{
  pclog("RIVA 128 MMIO read %08X %04X:%08X\n", addr, CS, pc);
  return 0;
}

static uint32_t riva128_mmio_read_l(uint32_t addr, void *p)
{
  pclog("RIVA 128 MMIO read %08X %04X:%08X\n", addr, CS, pc);
  return 0;
}

static void riva128_mmio_write(uint32_t addr, uint8_t val, void *p)
{
  pclog("RIVA 128 MMIO write %08X %02X %04X:%08X\n", addr, val, CS, pc);
}

static void riva128_mmio_write_w(uint32_t addr, uint16_t val, void *p)
{
  pclog("RIVA 128 MMIO write %08X %04X %04X:%08X\n", addr, val, CS, pc);
}

static void riva128_mmio_write_l(uint32_t addr, uint32_t val, void *p)
{
  riva128_t *riva128 = (riva128_t *)p;
  svga_t *svga = &riva128->svga;

  addr &= 0xffffff;

  pclog("RIVA 128 MMIO write %08X %08X %04X:%08X\n", addr, val, CS, pc);

  switch(addr)
  {
  case 0x100000 ... 0x100fff:
  riva128_pfb_write(addr, val, riva128);
  break;
  case 0x680000 ... 0x680fff:
  riva128_pramdac_write(addr, val, riva128);
  break;
  }
}

static uint8_t riva128_rma_in(uint16_t addr, void *p)
{
  riva128_t *riva128 = (riva128_t *)p;
  svga_t *svga = &riva128->svga;
  uint8_t ret = 0;

  addr &= 0xff;

  pclog("RIVA 128 RMA read %04X %04X:%08X\n", addr, CS, pc);

  switch(addr)
  {
  case 0x00: ret = 0x65; break;
  case 0x01: ret = 0xd0; break;
  case 0x02: ret = 0x16; break;
  case 0x03: ret = 0x2b; break;
  case 0x08: case 0x09: case 0x0a: case 0x0b: ret = riva128_mmio_read(riva128->rma.addr + (addr & 3), riva128); break;
  }

  return ret;
}

static void riva128_rma_out(uint16_t addr, uint8_t val, void *p)
{
  riva128_t *riva128 = (riva128_t *)p;
  svga_t *svga = &riva128->svga;

  addr &= 0xff;

  pclog("RIVA 128 RMA write %04X %02X %04X:%08X\n", addr, val, CS, pc);

  switch(addr)
  {
  case 0x04:
  riva128->rma.addr &= ~0xff;
  riva128->rma.addr |= val;
  break;
  case 0x05:
  riva128->rma.addr &= ~0xff00;
  riva128->rma.addr |= (val << 8);
  break;
  case 0x06:
  riva128->rma.addr &= ~0xff0000;
  riva128->rma.addr |= (val << 16);
  break;
  case 0x07:
  riva128->rma.addr &= ~0xff000000;
  riva128->rma.addr |= (val << 24);
  break;
  case 0x08: case 0x0c: case 0x10: case 0x14:
  riva128->rma.data &= ~0xff;
  riva128->rma.data |= val;
  break;
  case 0x09: case 0x0d: case 0x11: case 0x15:
  riva128->rma.data &= ~0xff00;
  riva128->rma.data |= (val << 8);
  break;
  case 0x0a: case 0x0e: case 0x12: case 0x16:
  riva128->rma.data &= ~0xff0000;
  riva128->rma.data |= (val << 16);
  break;
  case 0x0b: case 0x0f: case 0x13: case 0x17:
  riva128->rma.data &= ~0xff000000;
  riva128->rma.data |= (val << 24);
  if(riva128->rma.addr < 0x1000000) riva128_mmio_write_l(riva128->rma.addr & 0xffffff, riva128->rma.data, riva128);
  else svga_writel_linear((riva128->rma.addr - 0x1000000) & svga->vrammask, riva128->rma.data, svga);
  break;
  }

  if(addr & 0x10) riva128->rma.addr+=4;
}

static uint8_t riva128_in(uint16_t addr, void *p)
{
  riva128_t *riva128 = (riva128_t *)p;
  svga_t *svga = &riva128->svga;
  uint8_t ret = 0;

  if (((addr & 0xfff0) == 0x3d0 || (addr & 0xfff0) == 0x3b0) && !(svga->miscout & 1))
    addr ^= 0x60;

    //        if (addr != 0x3da) pclog("S3 in %04X %04X:%08X  ", addr, CS, pc);
  switch (addr)
  {
  case 0x3D0 ... 0x3D3:
  pclog("RIVA 128 RMA BAR Register read %02X %04X:%08X\n", addr, CS, pc);
  if(!(riva128->rma.mode & 1)) break;
  ret = riva128_rma_in(riva128->rma_addr + ((riva128->rma.mode & 0xe) << 1) + (addr & 3), riva128);
  break;
  case 0x3D4:
  ret = svga->crtcreg;
  break;
  case 0x3D5:
  ret = svga->crtc[svga->crtcreg];
  if(svga->crtcreg > 0x18)
    pclog("RIVA 128 Extended CRTC read %02X %04X:%08X\n", svga->crtcreg, CS, pc);
  break;
  default:
  ret = svga_in(addr, svga);
  break;
  }
  //        if (addr != 0x3da) pclog("%02X\n", ret);
  return ret;
}

static void riva128_out(uint16_t addr, uint8_t val, void *p)
{
  riva128_t *riva128 = (riva128_t *)p;
  svga_t *svga = &riva128->svga;

  uint8_t old;

  if (((addr & 0xfff0) == 0x3d0 || (addr & 0xfff0) == 0x3b0) && !(svga->miscout & 1))
    addr ^= 0x60;

  switch(addr)
  {
  case 0x3D0 ... 0x3D3:
  pclog("RIVA 128 RMA BAR Register write %02X %02x %04X:%08X\n", addr, val, CS, pc);
  riva128->rma.access_reg[addr & 3] = val;
  if(!(riva128->rma.mode & 1)) return;
  riva128_rma_out(riva128->rma_addr + ((riva128->rma.mode & 0xe) << 1) + (addr & 3), riva128->rma.access_reg[addr & 3], riva128);
  return;
  case 0x3D4:
  svga->crtcreg = val & 0x7f;
  return;
  case 0x3D5:
  if ((svga->crtcreg < 7) && (svga->crtc[0x11] & 0x80))
    return;
  if ((svga->crtcreg == 7) && (svga->crtc[0x11] & 0x80))
    val = (svga->crtc[7] & ~0x10) | (val & 0x10);
  old = svga->crtc[svga->crtcreg];
  svga->crtc[svga->crtcreg] = val;
  switch(svga->crtcreg)
  {
  case 0x1a:
  if(val & 2) svga->dac8bit = 1;
  else svga->dac8bit = 0;
  break;
  case 0x1e:
  riva128->read_bank = val >> 1;
  if (svga->chain4) svga->read_bank = riva128->read_bank << 16;
  else              svga->read_bank = riva128->read_bank << 14;
  break;
  case 0x1d:
  riva128->write_bank = val >> 1;
  if (svga->chain4) svga->write_bank = riva128->write_bank << 16;
  else              svga->write_bank = riva128->write_bank << 14;
  break;
  case 0x26:
  if (!svga->attrff)
    svga->attraddr = val & 31;
  break;
  case 0x19:
  case 0x25:
  case 0x28:
  case 0x2d:
  svga_recalctimings(svga);
  break;
  case 0x38:
  riva128->rma.mode = val & 0xf;
  break;
  }
  if(svga->crtcreg > 0x18)
    pclog("RIVA 128 Extended CRTC write %02X %02x %04X:%08X\n", svga->crtcreg, val, CS, pc);
  if (old != val)
  {
    if (svga->crtcreg < 0xE || svga->crtcreg > 0x10)
    {
      svga->fullchange = changeframecount;
      svga_recalctimings(svga);
    }
  }
  return;
  case 0x3C5:
  if(svga->seqaddr == 6) riva128->ext_regs_locked = val;
  break;
  }

  svga_out(addr, val, svga);
}

static uint8_t riva128_pci_read(int func, int addr, void *p)
{
  riva128_t *riva128 = (riva128_t *)p;
  svga_t *svga = &riva128->svga;
  uint8_t ret = 0;
  pclog("RIVA 128 PCI read %02X %04X:%08X\n", addr, CS, pc);
  switch (addr)
  {
    case 0x00: ret = 0xd2; break; /*'nVidia'*/
    case 0x01: ret = 0x12; break;

    case 0x02: ret = 0x18; break; /*'RIVA 128'*/
    case 0x03: ret = 0x00; break;

    case 0x04: ret = riva128->pci_regs[0x04] & 0x37; break;
    case 0x05: ret = riva128->pci_regs[0x05] & 0x01; break;

    case 0x06: ret = 0x20; break;
    case 0x07: ret = riva128->pci_regs[0x07] & 0x73; break;

    case 0x08: ret = 0x01; break; /*Revision ID*/
    case 0x09: ret = 0; break; /*Programming interface*/

    case 0x0a: ret = 0x00; break; /*Supports VGA interface*/
    case 0x0b: ret = 0x03; /*output = 3; */break;

    case 0x0e: ret = 0x00; break; /*Header type*/

    case 0x13:
    case 0x17:
    case 0x18:
    case 0x19:
    ret = riva128->pci_regs[addr];
    break;

    case 0x2c: case 0x2d: case 0x2e: case 0x2f:
    ret = riva128->pci_regs[addr];
    //if(CS == 0x0028) output = 3;
    break;

    case 0x34: ret = 0x00; break;

    case 0x3c: ret = riva128->pci_regs[0x3c]; break;

    case 0x3d: ret = 0x01; break; /*INTA*/

    case 0x3e: ret = 0x03; break;
    case 0x3f: ret = 0x01; break;

  }
  //        pclog("%02X\n", ret);
  return ret;
}

static void riva128_pci_write(int func, int addr, uint8_t val, void *p)
{
  pclog("RIVA 128 PCI write %02X %02X %04X:%08X\n", addr, val, CS, pc);
  riva128_t *riva128 = (riva128_t *)p;
  svga_t *svga = &riva128->svga;
  switch (addr)
  {
    case 0x00: case 0x01: case 0x02: case 0x03:
    case 0x08: case 0x09: case 0x0a: case 0x0b:
    case 0x3d: case 0x3e: case 0x3f:
    return;

    case PCI_REG_COMMAND:
    if (val & PCI_COMMAND_IO)
    {
      io_removehandler(0x03c0, 0x0020, riva128_in, NULL, NULL, riva128_out, NULL, NULL, riva128);
      io_sethandler(0x03c0, 0x0020, riva128_in, NULL, NULL, riva128_out, NULL, NULL, riva128);
    }
    else
      io_removehandler(0x03c0, 0x0020, riva128_in, NULL, NULL, riva128_out, NULL, NULL, riva128);
    riva128->pci_regs[PCI_REG_COMMAND] = val & 0x37;
    return;

    case 0x05:
    riva128->pci_regs[0x05] = val & 0x01;
    return;

    case 0x07:
    riva128->pci_regs[0x07] = (riva128->pci_regs[0x07] & 0x8f) | (val & 0x70);
    return;

    case 0x13:
    {
      riva128->pci_regs[addr] = val;
      uint32_t mmio_addr = val << 24;
      mem_mapping_set_addr(&riva128->mmio_mapping, mmio_addr, 0x1000000);
      return;
    }

    case 0x17:
    {
      riva128->pci_regs[addr] = val;
      uint32_t linear_addr = (val << 24);
      mem_mapping_set_addr(&riva128->linear_mapping, linear_addr, 0x400000);
      return;
    }

    case 0x18:
    riva128->pci_regs[addr] = val;
    return;

    case 0x19:
    riva128->pci_regs[addr] = val;
    io_removehandler(riva128->rma_addr, 0x0100, riva128_rma_in, NULL, NULL, riva128_rma_out, NULL, NULL, riva128);
    {
      riva128->rma_addr = (riva128->pci_regs[0x18] & 0xFC) | (riva128->pci_regs[0x19] << 8);
      io_sethandler(riva128->rma_addr, 0x0020, riva128_rma_in, NULL, NULL, riva128_rma_out, NULL, NULL, riva128);
    }
    return;

    case 0x30: case 0x32: case 0x33:
    riva128->pci_regs[addr] = val;
    if (riva128->pci_regs[0x30] & 0x01)
    {
      uint32_t addr = (riva128->pci_regs[0x32] << 16) | (riva128->pci_regs[0x33] << 24);
      //                        pclog("RIVA 128 bios_rom enabled at %08x\n", addr);
      mem_mapping_set_addr(&riva128->bios_rom.mapping, addr, 0x8000);
      mem_mapping_enable(&riva128->bios_rom.mapping);
    }
    else
    {
      //                        pclog("RIVA 128 bios_rom disabled\n");
      mem_mapping_disable(&riva128->bios_rom.mapping);
    }
    return;

    case 0x40: case 0x41: case 0x42: case 0x43:
    riva128->pci_regs[addr - 0x14] = val; //0x40-0x43 are ways to write to 0x2c-0x2f
    return;
  }
}

static void riva128_recalctimings(svga_t *svga)
{
  svga->ma |= (svga->crtc[0x19] & 0x1f) << 16;
  svga->rowoffset |= (svga->crtc[0x19] & 0xe0) << 3;
  if (svga->crtc[0x25] & 0x01) svga->vtotal      += 0x400;
  if (svga->crtc[0x25] & 0x02) svga->dispend     += 0x400;
  if (svga->crtc[0x25] & 0x04) svga->vblankstart += 0x400;
  if (svga->crtc[0x25] & 0x08) svga->vsyncstart  += 0x400;
  if (svga->crtc[0x25] & 0x10) svga->htotal      += 0x100;
  if (svga->crtc[0x25] & 0x20) svga->rowoffset   += 0x800;
  if (svga->crtc[0x2d] & 0x02) svga->htotal      += 0x100;
  if (svga->crtc[0x2d] & 0x02) svga->hdisp       += 0x100;
  switch(svga->crtc[0x28] & 3)
  {
    case 1:
    svga->bpp = 8;
    svga->render = svga_render_8bpp_highres;
    break;
    case 2:
    svga->bpp = 16;
    svga->render = svga_render_16bpp_highres;
    break;
    case 3:
    svga->bpp = 32;
    svga->render = svga_render_32bpp_highres;
    break;
  }
}

static void *riva128_init()
{
  riva128_t *riva128 = malloc(sizeof(riva128_t));
  memset(riva128, 0, sizeof(riva128_t));

  riva128->memory_size = device_get_config_int("memory");

  svga_init(&riva128->svga, riva128, riva128->memory_size << 20,
  riva128_recalctimings,
  riva128_in, riva128_out,
  NULL, NULL);

  rom_init(&riva128->bios_rom, "roms/riva128.bin", 0xc0000, 0x8000, 0x7fff, 0, MEM_MAPPING_EXTERNAL);
  if (PCI)
    mem_mapping_disable(&riva128->bios_rom.mapping);

  mem_mapping_add(&riva128->mmio_mapping,     0, 0,
    riva128_mmio_read,
    riva128_mmio_read_w,
    riva128_mmio_read_l,
    riva128_mmio_write,
    riva128_mmio_write_w,
    riva128_mmio_write_l,
    NULL,
    0,
    riva128);
  mem_mapping_add(&riva128->linear_mapping,   0, 0,
    svga_read_linear,
    svga_readw_linear,
    svga_readl_linear,
    svga_write_linear,
    svga_writew_linear,
    svga_writel_linear,
    NULL,
    0,
    &riva128->svga);

  io_sethandler(0x03c0, 0x0020, riva128_in, NULL, NULL, riva128_out, NULL, NULL, riva128);

  riva128->pci_regs[4] = 3;
  riva128->pci_regs[5] = 0;
  riva128->pci_regs[6] = 0;
  riva128->pci_regs[7] = 2;

  //win9x drivers expect this.
  riva128->pci_regs[0x2c] = 0xd2;
  riva128->pci_regs[0x2d] = 0x12;
  riva128->pci_regs[0x2e] = 0x00;
  riva128->pci_regs[0x2f] = 0x03;

  gfxpciid = pci_add(riva128_pci_read, riva128_pci_write, riva128);

  return riva128;
}

static void riva128_close(void *p)
{
  riva128_t *riva128 = (riva128_t *)p;
  FILE *f = fopen("vram.dmp", "wb");
  fwrite(riva128->svga.vram, 4 << 20, 1, f);
  fclose(f);

  svga_close(&riva128->svga);

  free(riva128);
}

static int riva128_available()
{
  return rom_present("roms/riva128.bin");
}

static void riva128_speed_changed(void *p)
{
  riva128_t *riva128 = (riva128_t *)p;

  svga_recalctimings(&riva128->svga);
}

static void riva128_force_redraw(void *p)
{
  riva128_t *riva128 = (riva128_t *)p;

  riva128->svga.fullchange = changeframecount;
}

static void riva128_add_status_info(char *s, int max_len, void *p)
{
  riva128_t *riva128 = (riva128_t *)p;

  svga_add_status_info(s, max_len, &riva128->svga);
}

static device_config_t riva128_config[] =
{
  {
    .name = "memory",
    .description = "Memory size",
    .type = CONFIG_SELECTION,
    .selection =
    {
      {
        .description = "4 MB",
        .value = 4
      },
      {
        .description = ""
      }
    },
    .default_int = 4
  },
  {
    .type = -1
  }
};

device_t riva128_device =
{
        "nVidia RIVA 128",
        0,
        riva128_init,
        riva128_close,
        riva128_available,
        riva128_speed_changed,
        riva128_force_redraw,
        riva128_add_status_info,
        riva128_config
};
