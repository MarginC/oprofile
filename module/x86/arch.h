/**
 * @file arch.h
 * defines registers for x86
 *
 * @remark Copyright 2002 OProfile authors
 * @remark Read the file COPYING
 *
 * @author Will Cohen
 */

#ifndef ARCH_H
#define ARCH_H

/* How to access the processor's instruction pointer */
#define INST_PTR(regs) ((regs)->eip)

/* How to access the processor's status register */
#define STATUS(regs) ((regs)->eflags)

/* Bit in processor's status register for interrupt masking */
#define IRQ_ENABLED(eflags)	(eflags & IF_MASK)

#endif /* ARCH_H */
