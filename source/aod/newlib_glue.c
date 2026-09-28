/*
 * aod-vita: the nightly softfp newlib references _getentropy_r from getentropy() but the
 * syscall layer does not provide it. Back it with the kernel RNG.
 * This file is part of aod-vita and is distributed under the MIT license.
 */
#include <errno.h>
#include <reent.h>
#include <stddef.h>
#include <psp2/kernel/rng.h>

#undef _getentropy_r
int _getentropy_r(struct _reent *r, void *buf, size_t n) {
	if (n > 256) { r->_errno = EIO; return -1; }
	if (sceKernelGetRandomNumber(buf, n) < 0) { r->_errno = EIO; return -1; }
	return 0;
}
