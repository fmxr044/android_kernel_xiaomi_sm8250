#include <linux/export.h>
#include <linux/types.h>

void __cfi_slowpath(u64 type, void *ptr)
{
 /* 空实现：非 CFI 内核，CFI 检查降级为 no-op */
}
EXPORT_SYMBOL(__cfi_slowpath);