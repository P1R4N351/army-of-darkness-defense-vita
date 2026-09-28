/*
 * Host regression test for source/reimpl/asset_manager.cpp (hardware boot-03).
 * libgame's whole-file asset reader (0x2ab800) opens assets and never calls AAsset_close.
 * With one stdio stream held per AAsset, a bounded stream table runs out and later opens of
 * existing files fail ("not found"). Here the process fd limit stands in for that table.
 */
#include "reimpl/asset_manager.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <sys/resource.h>
#include <sys/stat.h>

extern "C" void _log_print(int t, const char *fmt, ...) {}

static int fails, checks;
#define CHECK(c, ...) do { checks++; if (!(c)) { fails++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

int main() {
	const int N = 300;
	std::string dir = std::string(DATA_PATH) + "assets/scripts.archondb/";
	mkdir(DATA_PATH, 0755); mkdir((std::string(DATA_PATH) + "assets").c_str(), 0755); mkdir(dir.c_str(), 0755);
	for (int i = 0; i < N; i++) {
		FILE *f = fopen((dir + "s" + std::to_string(i) + ".lua").c_str(), "wb");
		for (int k = 0; k < 1000 + i; k++) fputc('a' + (i + k) % 26, f);
		fclose(f);
	}
	struct rlimit rl = { 32, 32 };
	setrlimit(RLIMIT_NOFILE, &rl);

	std::vector<AAsset *> leaked;
	for (int i = 0; i < N; i++) {
		std::string name = "scripts.archondb/s" + std::to_string(i) + ".lua";
		AAsset *a = AAssetManager_open(nullptr, name.c_str(), 2);
		CHECK(a != nullptr, "open #%d failed (stream exhaustion)", i);
		if (!a) break;
		/* exactly what libgame 0x2ab800 does: length, whole read, no close */
		int len = (int)AAsset_getLength(a);
		CHECK(len == 1000 + i, "len #%d = %d", i, len);
		std::vector<char> buf(len);
		int r = AAsset_read(a, buf.data(), len);
		CHECK(r == len && buf[0] == 'a' + i % 26 && buf[len - 1] == 'a' + (i + len - 1) % 26, "read #%d = %d", i, r);
		leaked.push_back(a);
	}
	/* an early (evicted) asset must still work: seek + read + remaining */
	if (leaked.size() > 10) {
		AAsset *a = leaked[3];
		CHECK(AAsset_seek(a, 10, SEEK_SET) == 10, "seek on evicted asset");
		char c = 0;
		CHECK(AAsset_read(a, &c, 1) == 1 && c == 'a' + (3 + 10) % 26, "read after reopen got %d", c);
		CHECK(AAsset_getRemainingLength(a) == 1003 - 11, "remaining = %ld", (long)AAsset_getRemainingLength(a));
		CHECK(AAsset_seek(a, -1, SEEK_END) == 1002, "seek end");
		CHECK(AAsset_read(a, &c, 1) == 1 && c == 'a' + (3 + 1002) % 26, "last byte");
		CHECK(AAsset_read(a, &c, 1) == 0, "read at EOF returns 0");
	}
	CHECK(AAssetManager_open(nullptr, "scripts.archondb/missing.lua", 2) == nullptr, "missing file -> NULL");
	for (AAsset *a : leaked) AAsset_close(a);
	printf("%d/%d checks passed (%zu assets leaked-open)\n", checks - fails, checks, leaked.size());
	return fails != 0;
}
