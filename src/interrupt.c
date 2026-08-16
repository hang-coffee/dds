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

// 异常/NMI（0x00-0x03, 0xFF）: 不可屏蔽、强制 ISR_SS=1/ISR_NMO=0/ISR_INL=0
static inline bool is_exception_intr(uint8_t num) {
	return (num<=LAST_EXCEPTION_INT || num==0xff);
}

int handle_intr(DOCTOR_CPU *cpu, uint8_t irq_num, bool is_software) {
	// 检查irq_num
	bool is_exc=is_exception_intr(irq_num);
	bool flag_unblockable=false;
	if(irq_num>LAST_EXCEPTION_INT && irq_num!=0xff && irq_num!=0xfe) {	// 说明不是异常/NMI/SVC
		// 硬件中断受全局中断使能控制；软件中断（INT）不受 GIE=0 / PUSHI-POPI 内部影响
		if(!is_software && !(cpu->intr.intr_enable)) return -1;	// 如果中断关闭，就立即返回
		if(cpu->intr.current_int>=0) {			// 正处于某个ISR中 → 按嵌套规则裁决
			if(!cpu->intr.inl) return -2;		// 当前ISR不允许嵌套（INL=0），拒绝
			if(isr_get_ipl(cpu, irq_num)>=isr_get_ipl(cpu, cpu->intr.current_int)) return -2;	// 新中断优先级不够高，拒绝
		}
	} else {									// 异常/NMI/SVC：不可屏蔽
		flag_unblockable=true;
	}
	// 首先查询ICT表
	uint32_t ptr=cpu->intr.ictb+8*irq_num;
	if(!mem_range_ok(cpu, ptr, 8, MEM_TYPE_DATA)) return -4;	// ICT 表项越界 → 派发失败
	uint32_t isr_addr;
	isr_addr=load_dword_from_mem(cpu, ptr, MEM_TYPE_DATA);		// 获得ISR基址
	if(isr_get_dpl(cpu, irq_num) < cpu->intr.cpl && !(flag_unblockable)) {			// 不允许执行，因为越权
		exe_err(cpu);
		cpu->intr.exception=ERR_GP;
		return ERR_GP;
	}
	// 压栈: (依次) RIN3(ICTB,CTRL), P, 当前中断号
	if(push(cpu, cpu->intr.ictb, 3)) return -3;			// 栈上溢 → 派发失败
	if(push(cpu, cpu->intr.ctrl, 3)) return -3;
	if(push(cpu, cpu->P, 3)) return -3;
	if(push(cpu, (uint32_t)(cpu->intr.current_int & 0xff), 1)) return -3;
	// ISR_SS: 异常强制 SS=1（切换到KSP），普通中断按ICT表项
	bool use_ss=is_exc ? true : (isr_get_ss(cpu, irq_num)!=0);
	if(use_ss) {
		set_dword_mem(cpu, cpu->sys.ksp, *op2reg(cpu, REG_S), MEM_TYPE_DATA);
		(*op2reg(cpu, REG_S))=cpu->sys.ksp;
	}
	// 设置中断状态
	uint32_t mask=0x40000000;					// INL
	if(is_exc) cpu->intr.ctrl&=(~mask);			// 异常强制 INL=0
	else if(isr_get_inl(cpu, irq_num)) cpu->intr.ctrl|=mask;
	else cpu->intr.ctrl&=(~mask);
	mask=0x10000000;							// CPL
	cpu->intr.ctrl&=(~mask);					// ISR 以内核态执行
	if(is_exc) {
		cpu->intr.ctrl&=(~0x20000000);			// 异常强制 MPU=0（ISR_NMO=0）
	} else if(isr_get_nmo(cpu, irq_num)==0) {
		cpu->intr.ctrl&=(~0x20000000);			// MPU
	}
	// 跳转
	cpu->P=isr_addr;
	cpu->intr.current_int=irq_num;

	update_intr_context(&(cpu->intr));
	return 0;
}

void iret(DOCTOR_CPU *cpu) {
	uint8_t irq_num=(uint8_t)(cpu->intr.current_int & 0xff);
	// 异常强制 SS=1（从KSP恢复S），普通中断按ICT表项
	bool use_ss=is_exception_intr(irq_num) || isr_get_ss(cpu, irq_num);
	if(use_ss) {								// 先从KSP恢复S，再回到用户栈弹P和RIN3
		(*op2reg(cpu, REG_S))=load_dword_from_mem(cpu, cpu->sys.ksp, MEM_TYPE_DATA);
	}
	uint32_t saved_int=pop(cpu, 1);
	cpu->intr.current_int=(saved_int==0xff)?(-1):(int)saved_int;	// 0xFF表示无中断(-1)
	cpu->P=pop(cpu, 3);
	cpu->intr.ctrl=pop(cpu, 3);
	cpu->intr.ictb=pop(cpu, 3);
	update_intr_context(&(cpu->intr));
	if(irq_num-(cpu->intr.irq_base)<32 && (cpu->intr.irq_base)<=irq_num) {
		set_intr_info(&(cpu->intr.rin2), (irq_num-(cpu->intr.irq_base)), 0);	// 清除中断挂起
	}
	return;
}

int raise_exception(DOCTOR_CPU *cpu, uint8_t ex, uint32_t xar) {
	cpu->sys.xar=xar;
	return handle_intr(cpu, ex, false);
}

int svc(DOCTOR_CPU *cpu) {
	// 陷入内核：硬件将 RIN3 的 CPL 置为 0，GIE 置为 0，且护栏因进入内核态而暂时失效，
	// 然后调用中断 0xFE。
	// 注意顺序：先派发（压入**原始** RIN3，这样 IRET 才能恢复到用户态原状：
	// CPL=1、GIE 恢复），再把工作状态置为 CPL=0/GIE=0/MPU=0。
	int hr=handle_intr(cpu, 0xfe, false);
	cpu->intr.ctrl &= ~0x80000000;		// GIE=0（工作状态）
	cpu->intr.ctrl &= ~0x20000000;		// MPU=0（护栏暂时失效）
	// CPL 已由 handle_intr 置为 0
	update_intr_context(&(cpu->intr));
	return hr;
}

void hlt(DOCTOR_CPU *cpu) {
	cpu->halted=true;
	return;
}

void pushi(DOCTOR_CPU *cpu) {
	cpu->intr.inside_ppi=true;
	push(cpu, cpu->intr.ictb, 3);
	push(cpu, cpu->intr.ctrl, 3);
	update_intr_context(&(cpu->intr));
	return;
}

void popi(DOCTOR_CPU *cpu) {
	cpu->intr.ctrl=pop(cpu, 3);
	cpu->intr.ictb=pop(cpu, 3);
	cpu->intr.inside_ppi=false;
	update_intr_context(&(cpu->intr));
	return;
}
