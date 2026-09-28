/* Error-path regression for source/reimpl/asset_manager.cpp: a read error is -1 (not EOF 0),
 * EOF is 0, and a failed size query fails the open instead of yielding an empty asset. */
#include "reimpl/asset_manager.h"
#include <cstdio>
#include <string>
#include <sys/stat.h>
extern "C" void _log_print(int t, const char *fmt, ...) {}
int g_fail_read, g_fail_seek_end, g_fail_tell;
static int injected_error;
FILE *fault_fopen(const char *p, const char *m) { return fopen(p, m); }
size_t fault_fread(void *b, size_t s, size_t n, FILE *f) {
	if (g_fail_read) { injected_error = 1; return 0; }
	injected_error = 0; return fread(b, s, n, f);
}
int fault_fseek(FILE *f, long o, int w) { if (g_fail_seek_end && w == SEEK_END) return -1; return fseek(f, o, w); }
long fault_ftell(FILE *f) { return g_fail_tell ? -1 : ftell(f); }
int fault_ferror(FILE *f) { return injected_error || ferror(f); }

static int fails, checks;
#define CHECK(c, ...) do { checks++; if (!(c)) { fails++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

int main() {
	std::string d = std::string(DATA_PATH) + "assets/";
	mkdir(DATA_PATH, 0755); mkdir(d.c_str(), 0755);
	FILE *f = fopen((d + "x.bin").c_str(), "wb"); fwrite("0123456789", 1, 10, f); fclose(f);
	char buf[32];

	AAsset *a = AAssetManager_open(nullptr, "x.bin", 2);
	CHECK(a && AAsset_getLength(a) == 10, "open ok");
	g_fail_read = 1;
	CHECK(AAsset_read(a, buf, 4) == -1, "read error reported as -1 (not EOF 0)");
	g_fail_read = 0;
	CHECK(AAsset_read(a, buf, 4) == 4 && buf[0] == '0', "read recovers after error (reopen at pos)");
	CHECK(AAsset_read(a, buf, 32) == 6 && buf[5] == '9', "partial read to end");
	CHECK(AAsset_read(a, buf, 4) == 0, "EOF is 0");
	CHECK(AAsset_seek(a, 11, SEEK_SET) == -1, "seek past end rejected");
	CHECK(AAsset_seek(a, -1, SEEK_SET) == -1, "negative seek rejected");
	CHECK(AAsset_seek(a, 10, SEEK_SET) == 10 && AAsset_read(a, buf, 1) == 0, "seek to end then EOF");
	AAsset_close(a);

	g_fail_seek_end = 1;
	CHECK(AAssetManager_open(nullptr, "x.bin", 2) == nullptr, "fseek(SEEK_END) failure -> open fails (not empty asset)");
	g_fail_seek_end = 0; g_fail_tell = 1;
	CHECK(AAssetManager_open(nullptr, "x.bin", 2) == nullptr, "ftell failure -> open fails");
	g_fail_tell = 0;
	a = AAssetManager_open(nullptr, "x.bin", 2);
	CHECK(a && AAsset_getLength(a) == 10, "open ok again after faults");
	if (a) AAsset_close(a);
	printf("%d/%d checks passed (fault injection)\n", checks - fails, checks);
	return fails != 0;
}
