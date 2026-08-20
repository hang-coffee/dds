#include <signal.h>
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include "debugger.h"

#include "cpu.h"
#include "mem.h"

volatile sig_atomic_t sigint_received=0;

void handle_sigint(int sig) {
	sig=sig;
	sigint_received=1;
	return;
}

void exe_err(DOCTOR_CPU *cpu) {
	fprintf(stderr, "\nP=0x%08X\nRegister dump:\n", cpu->P);
	fprintf(stderr, "E =0x%08X\t\tS =0x%08X\t\tT =0x%08X\t\tA =0x%08X\n",
					(*op2reg(cpu, REG_E)), (*op2reg(cpu, REG_S)), (*op2reg(cpu, REG_T)), (*op2reg(cpu, REG_A)));
	fprintf(stderr, "B =0x%08X\t\tF =0x%08X\t\tR =0x%08X\t\tC =0x%08X\n",
					(*op2reg(cpu, REG_B)), (*op2reg(cpu, REG_F)), (*op2reg(cpu, REG_R)), (*op2reg(cpu, REG_C)));
	fprintf(stderr, "D1=0x%08X\t\tD2=0x%08X\t\tX =0x%08X\t\tI =0x%08X\n",
					(*op2reg(cpu, REG_D1)), (*op2reg(cpu, REG_D2)), (*op2reg(cpu, REG_X)), (*op2reg(cpu, REG_I)));
	fprintf(stderr, "DP0=%g DP1=%g DP2=%g DP3=%g\n", cpu->dbl_regs[0], cpu->dbl_regs[1], cpu->dbl_regs[2], cpu->dbl_regs[3]);
fprintf(stderr, "FP0=0x%08X FP1=0x%08X FP2=0x%08X FP3=0x%08X FP4=0x%08X FP5=0x%08X FP6=0x%08X FP7=0x%08X FPCR=0x%08X\n",
				cpu->fp_regs[0], cpu->fp_regs[1], cpu->fp_regs[2], cpu->fp_regs[3],
				cpu->fp_regs[4], cpu->fp_regs[5], cpu->fp_regs[6], cpu->fp_regs[7],
				cpu->fpcr);
	fprintf(stderr, "Sysreg dump:\n");
	fprintf(stderr, "RIN1=0x%08X, RIN2=0x%08X, ICTB=0x%08X, CTRL=0x%08X\n", 
					cpu->intr.rin1, cpu->intr.rin2, cpu->intr.ictb, cpu->intr.ctrl);
	return;
}


