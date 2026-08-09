#include <stdio.h>
#include "stdlib.h"
#include <stdint.h>
#include <signal.h>
#include <setjmp.h>
#include "debugger.h"
#include "cpu.h"
#include "decode.h"
#include "device.h"

DOCTOR_CPU cpu;
static sigjmp_buf sigsegv_env;
static DOCTOR_CPU *current_cpu;
static volatile sig_atomic_t segv_occ=0;

static void handle_sigsegv(int sig, siginfo_t *info, void *context) {
	sig=sig;
	signal(SIGSEGV, SIG_DFL);
	segv_occ=1;
	siglongjmp(sigsegv_env, 1);
}

static void init_segv_handler(void) {
	struct sigaction sa;
	sa.sa_sigaction=handle_sigsegv;
	sigemptyset(&sa.sa_mask);
	sa.sa_flags=SA_SIGINFO;
	if(sigaction(SIGSEGV, &sa, NULL)==-1) {
		perror("sigaction");
		exit(EXIT_FAILURE);
	}
	return;
}

int main() {
	signal(SIGINT, handle_sigint);
	init_segv_handler();

	cpu_init(&cpu);
	cpu_load_bin(&cpu, "code.bin");
	Decoded_instr instr;
	decode(&cpu, &instr);
	if(sigsetjmp(sigsegv_env, 1)==0) 
		cpu_run(&cpu);
	else {
		fprintf(stderr, "\nTrapped SIGSEGV. \n");
		exe_err(&cpu);
		exit(EXIT_FAILURE);
	}
	return 0;
}
