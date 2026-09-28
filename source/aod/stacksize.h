/*
 * aod-vita: bionic pthread stack-size semantics on pthread-embedded (boot-13 BGM fix).
 *
 * Android (32-bit bionic) accepts any stack size >= PTHREAD_STACK_MIN = 8192. The VitaSDK
 * pthread-embedded pthread_attr_setstacksize returns EINVAL below 32768 (linked code:
 * `cmp r1, #0x8000; bcc -> mov r0, #22`). FMOD Ex 4.44.31 creates its "FMOD file thread" with an
 * 8192-byte stack and treats any failure in FMOD_OS_Thread_Create as FMOD_ERR_INTERNAL (33); that
 * is how createStream("loop.ogg") failed and the menu had no music.
 *
 * This file is part of aod-vita and is distributed under the MIT license.
 */
#ifndef AOD_STACKSIZE_H
#define AOD_STACKSIZE_H

#include <errno.h>
#include <stddef.h>

#define AOD_BIONIC_STACK_MIN 8192u   /* bionic PTHREAD_STACK_MIN on 32-bit (NDK pthread.h) */
#define AOD_PTE_STACK_MIN 32768u     /* pthread-embedded's setstacksize floor */

/* Returns 0 and the size to give pthread-embedded, or EINVAL where bionic would refuse. */
static inline int aod_bionic_stacksize(size_t requested, size_t *pte_size) {
	if (requested < AOD_BIONIC_STACK_MIN) return EINVAL;
	*pte_size = requested < AOD_PTE_STACK_MIN ? AOD_PTE_STACK_MIN : requested;
	return 0;
}

#endif
