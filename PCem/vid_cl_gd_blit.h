static uint8_t rop_to_index[256];

#define le32_to_cpu(x) (x)
#define le16_to_cpu(x) (x)

void cirrus_bitblt_reset(clgd_t *clgd, svga_t *svga);
void cirrus_bitblt_start(clgd_t *clgd, svga_t *svga);
void cirrus_write_bitblt(clgd_t *clgd, svga_t *svga, unsigned reg_value);
int cirrus_get_bpp(clgd_t *clgd, svga_t *svga);
void cirrus_get_resolution(clgd_t *clgd, svga_t *svga, int *pwidth, int *pheight);
void init_rops();