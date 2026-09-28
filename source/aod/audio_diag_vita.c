/*
 * aod-vita audio evidence (boot-10), Vita binding: -Wl,--wrap shims for the sceAudioOut calls the
 * SDK's OpenSL ES library makes (arguments and return codes pass through unchanged), and newlib
 * stderr redirected into the port log so libOpenSLES's SL_LOGE/SL_LOGI lines become visible.
 *
 * This file is part of aod-vita and is distributed under the MIT license.
 */
#include "aod/audio_diag.h"

#include <psp2/audioout.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/kernel/processmgr.h>
#include <stdio.h>
#include <reent.h>

int __real_sceAudioOutOpenPort(SceAudioOutPortType type, int len, int freq, SceAudioOutMode mode);
int __wrap_sceAudioOutOpenPort(SceAudioOutPortType type, int len, int freq, SceAudioOutMode mode) {
	int rc = __real_sceAudioOutOpenPort(type, len, freq, mode);
	aod_audiodiag_open((int)type, len, freq, (int)mode, rc);
	return rc;
}

int __real_sceAudioOutOutput(int port, const void *buf);
int __wrap_sceAudioOutOutput(int port, const void *buf) {
	aod_audiodiag_sample(port, buf);          /* the caller's prepared buffer, before it is queued */
	SceUInt64 t0 = sceKernelGetProcessTimeWide();
	int rc = __real_sceAudioOutOutput(port, buf);
	aod_audiodiag_output_time((unsigned)(sceKernelGetProcessTimeWide() - t0));
	aod_audiodiag_output(port, rc);
	return rc;
}

int __real_sceAudioOutSetVolume(int port, SceAudioOutChannelFlag ch, int *vol);
int __wrap_sceAudioOutSetVolume(int port, SceAudioOutChannelFlag ch, int *vol) {
	int rc = __real_sceAudioOutSetVolume(port, ch, vol);
	aod_audiodiag_volume(port, (int)ch, vol, rc);
	return rc;
}

/* libOpenSLES internal (IOutputMixExt.o), called from Vita.o's audioThread: mix one output buffer. */
void __real_IOutputMixExt_FillBuffer(void *self, void *buf, unsigned size);
void __wrap_IOutputMixExt_FillBuffer(void *self, void *buf, unsigned size) {
	SceUInt64 t0 = sceKernelGetProcessTimeWide();
	__real_IOutputMixExt_FillBuffer(self, buf, size);
	aod_audiodiag_fill_time((unsigned)(sceKernelGetProcessTimeWide() - t0));
}

static SceKernelLwMutexWork stderr_lock;

static int stderr_writefn(void *cookie, const char *data, int n) {
	(void)cookie;
	sceKernelLockLwMutex(&stderr_lock, 1, NULL);
	aod_audiodiag_stderr_write(data, n > 0 ? (size_t)n : 0);
	sceKernelUnlockLwMutex(&stderr_lock, 1);
	return n;
}

/* Call once, early, from the main thread. libOpenSLES writes via _impure_ptr->_stderr. */
void aod_audiodiag_install_stderr(void) {
	if (sceKernelCreateLwMutex(&stderr_lock, "aod_stderr", 0, 0, NULL) < 0)
		return;
	FILE *f = funopen(NULL, NULL, stderr_writefn, NULL, NULL);
	if (!f)
		return;
	setvbuf(f, NULL, _IOLBF, 256);
	_impure_ptr->_stderr = f;
	_GLOBAL_REENT->_stderr = f;
}
