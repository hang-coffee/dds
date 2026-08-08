#ifndef EXECUTION_H
#define EXECUTION_H
#include "cpu.h"
#include "decode.h"

int execute(DOCTOR_CPU *cpu, Decoded_instr *instr);
void exe_err(DOCTOR_CPU *cpu);
#endif

