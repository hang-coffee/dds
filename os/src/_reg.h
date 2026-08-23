#ifdef __IDE__
extern unsigned int __reg_A;
extern unsigned int __reg_B;
extern unsigned int __reg_C;
extern unsigned int __reg_D1;
extern unsigned int __reg_D2;
extern unsigned int __reg_R;
extern unsigned int __reg_X;
extern unsigned int __reg_I;
extern unsigned int __reg_S;
extern unsigned int __reg_F;
extern unsigned int __reg_E;
extern unsigned int __reg_T;
#define __interrupt__ __attribute__((__unused__))
#endif