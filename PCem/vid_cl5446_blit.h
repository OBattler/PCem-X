static uint8_t rop_to_index[256];

#define le32_to_cpu(x) (x)
#define le16_to_cpu(x) (x)

void cirrus_bitblt_reset(gd5446_t *gd5446, svga_t *svga);
void cirrus_bitblt_start(gd5446_t *gd5446, svga_t *svga);
void cirrus_write_bitblt(gd5446_t *gd5446, svga_t *svga, unsigned reg_value);
int cirrus_get_bpp(gd5446_t *gd5446, svga_t *svga);
void cirrus_get_resolution(gd5446_t *gd5446, svga_t *svga, int *pwidth, int *pheight);
void init_rops();