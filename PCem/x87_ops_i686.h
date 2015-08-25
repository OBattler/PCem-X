#define cond_B   ( CF_SET())
#define cond_NB  (!CF_SET())
#define cond_E   ( ZF_SET())
#define cond_NE  (!ZF_SET())
#define cond_BE  ( CF_SET() ||  ZF_SET())
#define cond_NBE (!CF_SET() && !ZF_SET())
#define cond_U   ( PF_SET())
#define cond_NU  (!PF_SET())

#define opFMOV(condition, i)                               \
        static int opFCMOV ## condition ## i(uint32_t fetchdat)  \
        {                                               \
                CLOCK_CYCLES(timing_bnt);               \
	        FP_ENTER();				\
                if (cond_ ## condition)                 \
                {                                       \
			ST(0) = ST(i);			\
			if (abrt)  return 1;		\
                        CLOCK_CYCLES(29);		\
                        return 1;                       \
                }                                       \
                return 0;                               \
        }                                               \

#define opFCMI(i)                               	\
        static int opFCOMI ## i(uint32_t fetchdat)	\
        {                                               \
	        FP_ENTER();				\
	        npxs &= ~(C0|C2|C3);			\
	        npxs |= x87_compare(ST(0), ST(i));	\
		CLOCK_CYCLES(4);			\
		return 0;				\
        }                                               \
							\
        static int opFCOMIP ## i(uint32_t fetchdat)	\
        {                                               \
	        FP_ENTER();				\
	        npxs &= ~(C0|C2|C3);			\
	        npxs |= x87_compare(ST(0), ST(i));	\
		x87_pop();				\
		CLOCK_CYCLES(4);			\
		return 0;				\
        }                                               \
							\
        static int opFUCOMI ## i(uint32_t fetchdat)	\
        {                                               \
	        FP_ENTER();				\
	        npxs &= ~(C0|C2|C3);			\
	        npxs |= x87_ucompare(ST(0), ST(i));	\
		CLOCK_CYCLES(4);			\
		return 0;				\
        }                                               \
							\
        static int opFUCOMIP ## i(uint32_t fetchdat)	\
        {                                               \
	        FP_ENTER();				\
	        npxs &= ~(C0|C2|C3);			\
	        npxs |= x87_ucompare(ST(0), ST(i));	\
		x87_pop();				\
		CLOCK_CYCLES(4);			\
		return 0;				\
        }                                               \

opFMOV(B, 0)
opFMOV(B, 1)
opFMOV(B, 2)
opFMOV(B, 3)
opFMOV(B, 4)
opFMOV(B, 5)
opFMOV(B, 6)
opFMOV(B, 7)
opFMOV(E, 0)
opFMOV(E, 1)
opFMOV(E, 2)
opFMOV(E, 3)
opFMOV(E, 4)
opFMOV(E, 5)
opFMOV(E, 6)
opFMOV(E, 7)
opFMOV(BE, 0)
opFMOV(BE, 1)
opFMOV(BE, 2)
opFMOV(BE, 3)
opFMOV(BE, 4)
opFMOV(BE, 5)
opFMOV(BE, 6)
opFMOV(BE, 7)
opFMOV(U, 0)
opFMOV(U, 1)
opFMOV(U, 2)
opFMOV(U, 3)
opFMOV(U, 4)
opFMOV(U, 5)
opFMOV(U, 6)
opFMOV(U, 7)
opFMOV(NB, 0)
opFMOV(NB, 1)
opFMOV(NB, 2)
opFMOV(NB, 3)
opFMOV(NB, 4)
opFMOV(NB, 5)
opFMOV(NB, 6)
opFMOV(NB, 7)
opFMOV(NE, 0)
opFMOV(NE, 1)
opFMOV(NE, 2)
opFMOV(NE, 3)
opFMOV(NE, 4)
opFMOV(NE, 5)
opFMOV(NE, 6)
opFMOV(NE, 7)
opFMOV(NBE, 0)
opFMOV(NBE, 1)
opFMOV(NBE, 2)
opFMOV(NBE, 3)
opFMOV(NBE, 4)
opFMOV(NBE, 5)
opFMOV(NBE, 6)
opFMOV(NBE, 7)
opFMOV(NU, 0)
opFMOV(NU, 1)
opFMOV(NU, 2)
opFMOV(NU, 3)
opFMOV(NU, 4)
opFMOV(NU, 5)
opFMOV(NU, 6)
opFMOV(NU, 7)

opFCMI(0)
opFCMI(1)
opFCMI(2)
opFCMI(3)
opFCMI(4)
opFCMI(5)
opFCMI(6)
opFCMI(7)
