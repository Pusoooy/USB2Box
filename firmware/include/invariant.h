#ifndef USB2BOX_INVARIANT_H
#define USB2BOX_INVARIANT_H

#include <stdint.h>

#ifdef USB2BOX_DEBUG
_Noreturn void usb2_assert_failed(const char *file, uint32_t line);
#define USB2_ASSERT(expr) ((expr) ? (void)0 : usb2_assert_failed(__FILE__, __LINE__))
#else
#define USB2_ASSERT(expr) ((void)0)
#endif

#endif
