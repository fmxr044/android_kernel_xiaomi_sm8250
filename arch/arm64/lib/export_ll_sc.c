#define EXPORT_LL_SC_C
#include <linux/export.h>
#include <linux/types.h>
#include <linux/atomic.h>

void __ll_sc_atomic_add(int i, atomic_t *v) { atomic_add(i, v); }
EXPORT_SYMBOL(__ll_sc_atomic_add);

int __ll_sc_atomic_add_return(int i, atomic_t *v) { return atomic_add_return(i, v); }
EXPORT_SYMBOL(__ll_sc_atomic_add_return);

int __ll_sc_atomic_sub_return(int i, atomic_t *v) { return atomic_sub_return(i, v); }
EXPORT_SYMBOL(__ll_sc_atomic_sub_return);

void __ll_sc_atomic64_add(s64 i, atomic64_t *v) { atomic64_add(i, v); }
EXPORT_SYMBOL(__ll_sc_atomic64_add);

long __ll_sc_atomic64_add_return(s64 i, atomic64_t *v) { return atomic64_add_return(i, v); }
EXPORT_SYMBOL(__ll_sc_atomic64_add_return);

void __ll_sc_atomic64_andnot(s64 i, atomic64_t *v) { atomic64_andnot(i, v); }
EXPORT_SYMBOL(__ll_sc_atomic64_andnot);

void __ll_sc_atomic64_or(s64 i, atomic64_t *v) { atomic64_or(i, v); }
EXPORT_SYMBOL(__ll_sc_atomic64_or);

u32 __ll_sc___cmpxchg_case_mb_4(volatile void *ptr, unsigned long old, u32 new)
{
	return cmpxchg((u32 *)ptr, (u32)old, new);
}
EXPORT_SYMBOL(__ll_sc___cmpxchg_case_mb_4);

u64 __ll_sc___cmpxchg_case_mb_8(volatile void *ptr, unsigned long old, u64 new)
{
	return cmpxchg((u64 *)ptr, (u64)old, new);
}
EXPORT_SYMBOL(__ll_sc___cmpxchg_case_mb_8);
