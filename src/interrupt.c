// interrupt.c - RINx寄存器的行为
#include "interrupt.h"

#include "debugger.h"

void irq_set(DOCTOR_CPU *cpu, uint8_t irq_num) {
	set_intr_info(&(cpu->intr.rin2), irq_num, 1);
	return;
}

void irq_clear(DOCTOR_CPU *cpu, uint8_t irq_num) {
	set_intr_info(&(cpu->intr.rin2), irq_num, 0);
	return;
}

int get_next_intr(DOCTOR_CPU *cpu) {			// 获得下一个中断的中断号，而不是IRQ
	if(cpu->intr.rin2==0) return -1;			// 不存在
	return __builtin_ctz(cpu->intr.rin2)+cpu->intr.irq_base;		// 这里返回的是最低的i
}

int handle_intr(DOCTOR_CPU *cpu, uint8_t irq_num) {
	// 检查irq_num
	bool flag_unblockable=false;
	if(irq_num>LAST_EXCEPTION_INT && irq_num!=0xff && irq_num!=0xfe) {	// 说明不是异常/NMI/SVC
		if(!(cpu->intr.intr_enable)) return -1;	// 如果中断关闭，就立即返回
		if(cpu->intr.inl) {						// 嵌套中断
			if(isr_get_ipl(cpu, irq_num)>=isr_get_ipl(cpu, cpu->intr.current_int) && isr_get_inl(cpu, irq_num)) return -2;
		}
	} else {									// 说明是异常/NMI/SVC
		exe_err(cpu);
		flag_unblockable=true;
//		return -3;
	}
	// 首先查询ICT表
	uint32_t ptr=cpu->intr.ictb+8*irq_num;
	uint32_t isr_addr;
	isr_addr=load_dword_from_mem(cpu, ptr, MEM_TYPE_DATA);		// 获得ISR基址
	if(isr_get_dpl(cpu, irq_num) < cpu->intr.cpl && !(flag_unblockable)) {			// 不允许执行，因为越权
		exe_err(cpu);
		cpu->intr.exception=ERR_GP;
		return -4;
	}
	// 压栈
	push(cpu, cpu->intr.ictb, 3);
	push(cpu, cpu->intr.ctrl, 3);
	push(cpu, cpu->P, 3);
	push(cpu, cpu->intr.current_int, 1);		// 保存上一个正在进行的中断
	// ISR_SS
	if(isr_get_ss(cpu, irq_num)) {
		set_dword_mem(cpu, cpu->sys.ksp, *op2reg(cpu, REG_S), MEM_TYPE_DATA);
		(*op2reg(cpu, REG_S))=cpu->sys.ksp;
	}
	// 设置中断状态
	uint32_t mask=0x40000000;					// INL
	if(isr_get_inl(cpu, irq_num)) cpu->intr.ctrl|=mask;
	else cpu->intr.ctrl&=(~mask);
	mask=0x10000000;							// CPL
	cpu->intr.ctrl&=(~mask);
	if(isr_get_nmo(cpu, irq_num)==0) {
		mask=0x20000000;
		cpu->intr.ctrl&=(~mask);				// MPU
	}
	// 跳转
	cpu->P=isr_addr;
	cpu->intr.current_int=irq_num;

	update_intr_context(&(cpu->intr));
	return 0;
}

void iret(DOCTOR_CPU *cpu) {
	uint8_t irq_num=cpu->intr.current_int;
	if(isr_get_ss(cpu, irq_num)) {				// 如果当前中断允许了SS
		(*op2reg(cpu, REG_S))=load_dword_from_mem(cpu, cpu->sys.ksp, MEM_TYPE_DATA);
	}
	cpu->intr.current_int=pop(cpu, 3);
	cpu->P=pop(cpu, 3);
	cpu->intr.ctrl=pop(cpu, 3);
	cpu->intr.ictb=pop(cpu, 3);
	update_intr_context(&(cpu->intr));
	if(irq_num-(cpu->intr.irq_base)<32 && (cpu->intr.irq_base)<=irq_num) {
		set_intr_info(&(cpu->intr.rin2), (irq_num-(cpu->intr.irq_base)), 0);	// 清除中断挂起
	}
	return;
}

void raise_exception(DOCTOR_CPU *cpu, uint8_t ex, uint32_t xar) {
	cpu->sys.xar=xar;
	handle_intr(cpu, ex);
	return;
}

void svc(DOCTOR_CPU *cpu) {
	handle_intr(cpu, 0xfe);
	return;
}

void hlt(DOCTOR_CPU *cpu) {
	cpu->halted=true;
	return;
}

