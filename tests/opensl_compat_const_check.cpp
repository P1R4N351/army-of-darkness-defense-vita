// The SDK's OpenSLES_AndroidConfiguration.h only works as C++ (unclosed #ifdef __cplusplus);
// check that source/aod/opensl_compat.c's restated constants equal the SDK's.
#include <SLES/OpenSLES.h>
#include <SLES/OpenSLES_AndroidConfiguration.h>
#include <cstring>
#include <cstdio>
int main() {
	int ok = std::strcmp((const char *)SL_ANDROID_KEY_STREAM_TYPE, "androidPlaybackStreamType") == 0 && SL_ANDROID_STREAM_VOICE == 0 &&
	         SL_ANDROID_STREAM_MEDIA == 3 && SL_ANDROID_STREAM_NOTIFICATION == 5;
	std::printf("%s (SDK Android configuration constants)\n", ok ? "1/1 checks passed" : "0/1 FAIL");
	return !ok;
}
