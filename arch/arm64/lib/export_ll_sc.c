#include <linux/export.h>

/* 32-bit 原子加 */
void __ll_sc_atomic_add(int i, void *v)
{
	unsigned int tmp;
	int result;

	asm volatile("// __ll_sc_atomic_add\n"
	"1:	ldxr	%w0, %2\n"
	"	add	%w0, %w0, %w3\n"
	"	stxr	%w1, %w0, %2\n"
	"	cbnz	%w1, 1b\n"
	: "=&r" (result), "=&r" (tmp), "+Q" (*(int *)v)
	: "Ir" (i));
}
EXPORT_SYMBOL(__ll_sc_atomic_add);

/* 32-bit 原子加并返回 */
int __ll_sc_atomic_add_return(int i, void *v)
{
	unsigned int tmp;
	int result;

	asm volatile("// __ll_sc_atomic_add_return\n"
	"1:	ldxr	%w0, %2\n"
	"	add	%w0, %w0, %w3\n"
	"	stlxr	%w1, %w0, %2\n"
	"	cbnz	%w1, 1b\n"
	"	dmb	ish\n"
	: "=&r" (result), "=&r" (tmp), "+Q" (*(int *)v)
	: "Ir" (i)
	: "memory");

	return result;
}
EXPORT_SYMBOL(__ll_sc_atomic_add_return);

/* 32-bit 原子减并返回 */
int __ll_sc_atomic_sub_return(int i, void *v)
{
	unsigned int tmp;
	int result;

	asm volatile("// __ll_sc_atomic_sub_return\n"
	"1:	ldxr	%w0, %2\n"
	"	sub	%w0, %w0, %w3\n"
	"	stlxr	%w1, %w0, %2\n"
	"	cbnz	%w1, 1b\n"
	"	dmb	ish\n"
	: "=&r" (result), "=&r" (tmp), "+Q" (*(int *)v)
	: "Jr" (i)
	: "memory");

	return result;
}
EXPORT_SYMBOL(__ll_sc_atomic_sub_return);

/* 64-bit 原子加 */
void __ll_sc_atomic64_add(long long i, void *v)
{
	unsigned int tmp;
	long result;

	asm volatile("// __ll_sc_atomic64_add\n"
	"1:	ldxr	%0, %2\n"
	"	add	%0, %0, %3\n"
	"	stxr	%w1, %0, %2\n"
	"	cbnz	%w1, 1b\n"
	: "=&r" (result), "=&r" (tmp), "+Q" (*(long long *)v)
	: "Ir" (i));
}
EXPORT_SYMBOL(__ll_sc_atomic64_add);

/* 64-bit 原子加并返回 */
long __ll_sc_atomic64_add_return(long long i, void *v)
{
	unsigned int tmp;
	long result;

	asm volatile("// __ll_sc_atomic64_add_return\n"
	"1:	ldxr	%0, %2\n"
	"	add	%0, %0, %3\n"
	"	stlxr	%w1, %0, %2\n"
	"	cbnz	%w1, 1b\n"
	"	dmb	ish\n"
	: "=&r" (result), "=&r" (tmp), "+Q" (*(long long *)v)
	: "Ir" (i)
	: "memory");

	return result;
}
EXPORT_SYMBOL(__ll_sc_atomic64_add_return);

/* 64-bit 原子位清除 (andnot) */
void __ll_sc_atomic64_andnot(long long i, void *v)
{
	unsigned int tmp;
	long result;

	asm volatile("// __ll_sc_atomic64_andnot\n"
	"1:	ldxr	%0, %2\n"
	"	bic	%0, %0, %3\n"
	"	stxr	%w1, %0, %2\n"
	"	cbnz	%w1, 1b\n"
	: "=&r" (result), "=&r" (tmp), "+Q" (*(long long *)v)
	: "r" (i));
}
EXPORT_SYMBOL(__ll_sc_atomic64_andnot);

/* 64-bit 原子按位或 (or) */
void __ll_sc_atomic64_or(long long i, void *v)
{
	unsigned int tmp;
	long result;

	asm volatile("// __ll_sc_atomic64_or\n"
	"1:	ldxr	%0, %2\n"
	"	orr	%0, %0, %3\n"
	"	stxr	%w1, %0, %2\n"
	"	cbnz	%w1, 1b\n"
	: "=&r" (result), "=&r" (tmp), "+Q" (*(long long *)v)
	: "Lr" (i));
}
EXPORT_SYMBOL(__ll_sc_atomic64_or);

/* 32-bit 原子比较并交换 (cmpxchg_mb_4) */
unsigned int __ll_sc___cmpxchg_case_mb_4(volatile void *ptr, unsigned long old, unsigned int new)
{
	unsigned int tmp, oldval;

	asm volatile("// __ll_sc___cmpxchg_case_mb_4\n"
	"1:	ldxr	%w1, %2\n"
	"	eor	%w0, %w1, %w3\n"
	"	cbnz	%w0, 2f\n"
	"	stlxr	%w0, %w4, %2\n"
	"	cbnz	%w0, 1b\n"
	"	dmb	ish\n"
	"2:\n"
	: "=&r" (tmp), "=&r" (oldval), "+Q" (*(unsigned int *)ptr)
	: "Ir" ((unsigned int)old), "r" (new)
	: "memory");

	return oldval;
}
EXPORT_SYMBOL(__ll_sc___cmpxchg_case_mb_4);

/* 64-bit 原子比较并交换 (cmpxchg_mb_8) */
unsigned long long __ll_sc___cmpxchg_case_mb_8(volatile void *ptr, unsigned long old, unsigned long long new)
{
	unsigned int tmp;
	unsigned long long oldval;

	asm volatile("// __ll_sc___cmpxchg_case_mb_8\n"
	"1:	ldxr	%1, %2\n"
	"	eor	%0, %1, %3\n"
	"	cbnz	%0, 2f\n"
	"	stlxr	%w0, %4, %2\n"
	"	cbnz	%w0, 1b\n"
	"	dmb	ish\n"
	"2:\n"
	: "=&r" (tmp), "=&r" (oldval), "+Q" (*(unsigned long long *)ptr)
	: "Ir" (old), "r" (new)
	: "memory");

	return oldval;
}
EXPORT_SYMBOL(__ll_sc___cmpxchg_case_mb_8);
