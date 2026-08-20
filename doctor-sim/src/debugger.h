#ifndef DEBUGGER_H
#define DEBUGGER_H

#include "cpu.h"

void handle_sigint(int sig);
void exe_err(DOCTOR_CPU *cpu);
void dump_stack(DOCTOR_CPU *cpu);
#endif

