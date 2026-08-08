#include <stdio.h>
#include <stdint.h>
#include <signal.h>
#include "debugger.h"
#include "cpu.h"
#include "decode.h"
#include "interrupt.h"
#include "device.h"

DOCTOR_CPU cpu;

int main() {
	signal(SIGINT, handle_sigint);

	cpu_init(&cpu);
	cpu_load_bin(&cpu, "code.bin");
	Decoded_instr instr;
	decode(&cpu, &instr);
	cpu_run(&cpu);
	return 0;
}
