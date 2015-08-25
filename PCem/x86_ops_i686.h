#define opMOV(condition)								\
	static int opMOV ## condition ## _r_w_a16(uint32_t fetchdat)			\
	{										\
		CLOCK_CYCLES(timing_bnt);						\
	        fetch_ea_16(fetchdat);							\
		if (cond_ ## condition)							\
		{									\
        		if (mod == 3)							\
	        	{								\
        	        	regs[reg].w = regs[rm].w;				\
	                	CLOCK_CYCLES(timing_rr);				\
		        }								\
        		else								\
		        {								\
        		        uint16_t temp;						\
	        	        CHECK_READ(ea_seg, eaaddr, eaaddr+1);			\
		                temp = geteaw();                if (abrt) return 1;	\
		                regs[reg].w = temp;					\
	        	        CLOCK_CYCLES((is486) ? 1 : 4);				\
		        }								\
		}									\
        	return 0;								\
	}										\
											\
	static int opMOV ## condition ## _r_w_a32(uint32_t fetchdat)			\
	{										\
		CLOCK_CYCLES(timing_bnt);						\
	        fetch_ea_32(fetchdat);							\
		if (cond_ ## condition)							\
		{									\
		        if (mod == 3)							\
		        {								\
		                regs[reg].w = regs[rm].w;				\
		                CLOCK_CYCLES(timing_rr);				\
		        }								\
		        else								\
		        {								\
		                uint16_t temp;						\
		                CHECK_READ(ea_seg, eaaddr, eaaddr+1);			\
		                temp = geteaw();                if (abrt) return 1;	\
		                regs[reg].w = temp;					\
		                CLOCK_CYCLES((is486) ? 1 : 4);				\
		        }								\
		}									\
	        return 0;								\
	}										\
											\
	static int opMOV ## condition ## _r_l_a16(uint32_t fetchdat)			\
	{										\
		CLOCK_CYCLES(timing_bnt);						\
	        fetch_ea_16(fetchdat);							\
		if (cond_ ## condition)							\
		{									\
		        if (mod == 3)							\
		        {								\
		                regs[reg].l = regs[rm].l;				\
		                CLOCK_CYCLES(timing_rr);				\
		        }								\
		        else								\
		        {								\
		                uint32_t temp;						\
		                CHECK_READ(ea_seg, eaaddr, eaaddr+3);			\
		                temp = geteal();                if (abrt) return 1;	\
		                regs[reg].l = temp;					\
		                CLOCK_CYCLES(is486 ? 1 : 4);				\
		        }								\
		}									\
	        return 0;								\
	}										\
											\
	static int opMOV ## condition ## _r_l_a32(uint32_t fetchdat)			\
	{										\
		CLOCK_CYCLES(timing_bnt);						\
	        fetch_ea_32(fetchdat);							\
		if (cond_ ## condition)							\
		{									\
		        if (mod == 3)							\
		        {								\
		                regs[reg].l = regs[rm].l;				\
		                CLOCK_CYCLES(timing_rr);				\
		        }								\
		        else								\
		        {								\
		                uint32_t temp;						\
		                CHECK_READ(ea_seg, eaaddr, eaaddr+3);			\
		                temp = geteal();                if (abrt) return 1;	\
		                regs[reg].l = temp;					\
		                CLOCK_CYCLES(is486 ? 1 : 4);				\
		        }								\
		}									\
	        return 0;								\
	}										\

opMOV(O)
opMOV(NO)
opMOV(B)
opMOV(NB)
opMOV(E)
opMOV(NE)
opMOV(BE)
opMOV(NBE)
opMOV(S)
opMOV(NS)
opMOV(P)
opMOV(NP)
opMOV(L)
opMOV(NL)
opMOV(LE)
opMOV(NLE)

static int opRDPMC(uint32_t fetchdat)
{
        if (ECX <= 1)
        {
                EAX = pmc[ECX] & 0xffffffff;
		EDX = pmc[ECX] >> 32;
                CLOCK_CYCLES(9);
                return 0;
        }
        pc = oldpc;
        x86illegal();
        return 1;
}

static int internal_illegal()
{
	pc = oldpc;
	x86gpf(NULL, 0);
	return 1;
}

/*	0 = Limit 0-15
	1 = Base 0-15
	2 = Base 16-23 (bits 0-7), Access rights
		8-11	Type
		12	S
		13, 14	DPL
		15	P
	3 = Limit 16-19 (bits 0-3), Base 24-31 (bits 8-15), granularity, etc.
		4	A
		6	DB
		7	G	*/

static int opSYSENTER(uint32_t fetchdat)
{
	if (!(cr0 & 1))  return internal_illegal();
	if (!cs_msr)  return internal_illegal();

	/* Set EF, IF, RF to 0. */
	eflags &= 0xfffcfdff;

	/* CS */
	temp_seg_data[0] = 0xFFFF;
	temp_seg_data[1] = 0;
	temp_seg_data[2] = 0x9B00;
	temp_seg_data[3] = 0xC;
	do_seg_load(&_cs, temp_seg_data);

	/* SS */
	temp_seg_data[0] = 0xFFFF;
	temp_seg_data[1] = 0;
	temp_seg_data[2] = 0x9300;
	temp_seg_data[3] = 0xC;
	do_seg_load(&_ss, temp_seg_data);

	ESP = esp_msr;
	pc = eip_msr;

	CLOCK_CYCLES(20);
	return 0;
}

static int opSYSEXIT(uint32_t fetchdat)
{
	if (!cs_msr)  return internal_illegal();
	if (!(cr0 & 1))  return internal_illegal();

	/* CS */
	temp_seg_data[0] = 0xFFFF;
	temp_seg_data[1] = 0;
	temp_seg_data[2] = 0xFB00;
	temp_seg_data[3] = 0xC;
	do_seg_load(&_cs, temp_seg_data);
	_cs.seg = cs_msr + 16;

	/* SS */
	temp_seg_data[0] = 0xFFFF;
	temp_seg_data[1] = 0;
	temp_seg_data[2] = 0xF300;
	temp_seg_data[3] = 0xC;
	do_seg_load(&_ss, temp_seg_data);
	_ss.seg = cs_msr + 24;

	ESP = ECX;
	pc = EDX;

	CLOCK_CYCLES(20);
	return 0;
}
