#define Bit8u uint8_t
#define Bitu unsigned int
#define bool uint8_t

void configure_comp(double h, uint8_t n, uint8_t bw, uint8_t b1);
void update_cga16_color(cga_t *cga);
void IncreaseHue(bool pressed);
void DecreaseHue(bool pressed);
uint8_t get_color(uint8_t index, uint8_t cyc, uint8_t col);
