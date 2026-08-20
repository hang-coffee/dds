// interrupt.h - RINx寄存器的行为

#ifndef INTERRUPT_H
#define INTERRUPT_H

#include <stdint.h>
#include "cpu.h"
#include "mem.h"

#define LAST_EXCEPTION_INT 0x03

// 快速获取CTRL寄存器的设置
#define CTRL_GET_GIE(ctrl) ( ( (ctrl) & (0x80000000) ) >> 31 )
#define CTRL_GET_INL(ctrl) ( ( (ctrl) & (0x40000000) ) >> 30 )
#define CTRL_GET_MPU(ctrl) ( ( (ctrl) & (0x20000000) ) >> 29 )
#define CTRL_GET_CPL(ctrl) ( ( (ctrl) & (0x10000000) ) >> 28 )
#define CTRL_GET_IRQB(ctrl) ( ( (ctrl) & (0xff0000) ) >> 16 )

// 快速获取指定中断号的开启或挂起信息
#define GET_INTR_INFO(rin, intr) ( ( (rin) & (1u<<(intr)) ) >> (intr) )

// 快速写上述信息
inline static void set_intr_info(uint32_t *rin, uint8_t intr, uint8_t state) {
	if(intr>31) {
		return;
	}
	uint32_t res=(*rin);
	uint32_t mask=1u<<intr;
	if(state==0) res&=(~mask);
	else res|=mask;
	(*rin)=res;
	return;
}

inline static void update_intr_enable(Intr_ctrl *ctrl) {
	if(ctrl->inside_ppi) {
		ctrl->intr_enable=false;
		return;
	}
	ctrl->intr_enable=CTRL_GET_GIE(ctrl->ctrl);
	return;
}

inline static void update_irq_base(Intr_ctrl *ctrl) {
	uint8_t k=CTRL_GET_IRQB(ctrl->ctrl);
	if(k>(255-32)) {
		ctrl->irq_base=0;
		return;
	}
	else ctrl->irq_base=k;
	return;
}

inline static void update_inl(Intr_ctrl *ctrl) {
	ctrl->inl=CTRL_GET_INL(ctrl->ctrl);
	return;
}

inline static void update_cpl(Intr_ctrl *ctrl) {
	ctrl->cpl=CTRL_GET_CPL(ctrl->ctrl);
	return;
}

inline static void update_intr_context(Intr_ctrl *ctrl) { // 更新中断上下文
	update_intr_enable(ctrl);
	update_irq_base(ctrl);
	update_inl(ctrl);
	update_cpl(ctrl);
	return;
}

inline static uint8_t isr_get_ipl(DOCTOR_CPU *cpu, uint8_t num) {
	return ((load_dword_from_mem(cpu, cpu->intr.ictb+8*num+4, MEM_TYPE_DATA) & 0xf));
}

inline static uint8_t isr_get_inl(DOCTOR_CPU *cpu, uint8_t num) {
	return ((load_dword_from_mem(cpu, cpu->intr.ictb+8*num+4, MEM_TYPE_DATA) & 0x40)>>6);
}

inline static uint8_t isr_get_dpl(DOCTOR_CPU *cpu, uint8_t num) {
	return ((load_dword_from_mem(cpu, cpu->intr.ictb+8*num+4, MEM_TYPE_DATA) & 0x80)>>7);
}

inline static uint8_t isr_get_ss(DOCTOR_CPU *cpu, uint8_t num) {
	return ((load_dword_from_mem(cpu, cpu->intr.ictb+8*num+4, MEM_TYPE_DATA) & 0x20)>>5);
}

inline static uint8_t isr_get_nmo(DOCTOR_CPU *cpu, uint8_t num) {
	return ((load_dword_from_mem(cpu, cpu->intr.ictb+8*num+4, MEM_TYPE_DATA) & 0x10)>>4);
}

void irq_set(DOCTOR_CPU *cpu, uint8_t irq_num);		// 外设请求中断，将RIN2设置为1
void irq_clear(DOCTOR_CPU *cpu, uint8_t irq_num);	// 清除IRQ挂起
int get_next_intr(DOCTOR_CPU *cpu);					// 获取下一个挂起的中断，返回其中断号而非IRQ
int handle_intr(DOCTOR_CPU *cpu, uint8_t irq_num, bool is_software);
	// 处理中断发生时的硬件操作。
	// is_software=true（INT 指令）：不受全局中断开关（GIE=0 / PUSHI-POPI 内部）影响，总是尝试派发
	// 返回: 0=成功, -1=中断关闭, -2=嵌套拒绝, -3=派发栈上溢, ERR_GP=越权

void iret(DOCTOR_CPU *cpu);							// 从中断返回
int raise_exception(DOCTOR_CPU *cpu, uint8_t ex, uint32_t xar);
	// 触发异常（0x00-0x03 / 0xFF）: 写XAR并派发; 返回0=成功, 非0=派发失败
int svc(DOCTOR_CPU *cpu);							// 陷入内核（CPL=0, GIE=0, 中断0xFE）
void hlt(DOCTOR_CPU *cpu);
void pushi(DOCTOR_CPU *cpu);
void popi(DOCTOR_CPU *cpu);

#endif

