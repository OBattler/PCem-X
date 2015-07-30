void fdc_init();
void fdc_hard_reset();
void fdc_add_ex(uint16_t port, uint8_t superio);
void fdc_add();
void fdc_remove_stab();
void fdc_add_pcjr();
void fdc_remove_ex(uint16_t port);
void fdc_remove();
void fdc_reset();
void fdc_poll();
void fdc_abort();
void fdc_set_dskchg_activelow();
void fdc_clear_dskchg_activelow();

void configure_from_int(int d, int val);
int int_from_config(int d);
void ejectdisc(int d);

void fdc_change(int val);
void fdc_changel_pcjr(int val);
int fdc_model();
void fdc_setmodel(int val);

extern int discint;
extern int densel_polarity;
extern int densel_polarity_mid[2];
extern int densel_force;
extern int drt[2];
extern int fdc_os2;
extern int drive_swap;

void configure_drt();
void fdc_setswap(int val);

uint8_t fdc_read(uint16_t addr, void *priv);
