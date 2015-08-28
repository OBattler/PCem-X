typedef struct
{
        char name[24];
        int id;
        struct
        {
                char name[8];
                CPU *cpus;
        } cpu[4];
        int fixed_gfxcard;
	int pci_only;
        void (*init)();
} MODEL;

extern MODEL models[];

extern int model;

int model_count();
int model_getromset();
int model_getmodel(int romset);
char *model_getname();
void model_init();
