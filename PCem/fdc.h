void fdc_init();
void fdc_add();
void fdc_add_pcjr();
void fdc_remove();
void fdc_reset();
void fdc_poll();
void fdc_abort();

void configure_from_int(int d, int val);
int int_from_config(int d);
void ejectdisc(int d);

void fdc_change(int val);
void fdc_changel_pcjr(int val);
int fdc_model();
void fdc_setmodel(int val);

extern int discint;
extern int vectorworkaround;