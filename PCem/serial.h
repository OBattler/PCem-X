void serial1_init(uint16_t addr, int irq);
void serial2_init(uint16_t addr, int irq);
void serial3_init(uint16_t addr, int irq);
void serial4_init(uint16_t addr, int irq);
void serial1_set(uint16_t addr, int irq);
void serial2_set(uint16_t addr, int irq);
void serial3_set(uint16_t addr, int irq);
void serial4_set(uint16_t addr, int irq);
void serial1_remove();
void serial2_remove();
void serial3_remove();
void serial4_remove();
void serial_reset();

struct SERIAL;

typedef struct
{
        uint8_t lsr,thr,mctrl,rcr,iir,ier,lcr,msr;
        uint8_t dlab1,dlab2;
        uint8_t dat;
        uint8_t int_status;
        
        int irq;

        void (*rcr_callback)(void *p);
        uint8_t fifo[256];
        int fifo_read, fifo_write;
        
        int recieve_delay;
} SERIAL;

extern SERIAL serial1, serial2, serial3, serial4;
