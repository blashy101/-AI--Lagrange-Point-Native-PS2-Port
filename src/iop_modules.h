/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef LP_IOP_MODULES_H
#define LP_IOP_MODULES_H
#include <stdbool.h>
struct lp_iop_services { bool pad, card, audio; const char *error; };
/* Detach boot-device drivers before initializing pad, card and audio services.
 * All code/data/IRX are embedded; never reopen the boot device. */
struct lp_iop_services lp_iop_modules_init(void);
#endif
