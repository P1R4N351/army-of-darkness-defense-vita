#include "reimpl/asset_manager.h"
#include "utils/logger.h"

#include <pthread.h>
#include <malloc.h>
#include <cstring>
#include <cstdio>
#include <cerrno>
#include <wchar.h>
#include <libc_bridge/libc_bridge.h>
#include <string>
#include <fcntl.h>

typedef struct assetManager {
    int dummy = 0; // TODO: mb we will need to store something here in future
    pthread_mutex_t mLock;
} assetManager;

typedef struct aAsset {
    char * filename;
    FILE* f;              // open stream, or nullptr while evicted
    size_t pos;           // logical read position (survives eviction)
    size_t fileSize;
    bool opened = false;  // asset is valid
    struct aAsset * lru_prev = nullptr;
    struct aAsset * lru_next = nullptr;
} asset;

/*
 * aod-vita (hardware boot-03): libgame's whole-file asset reader (0x2ab800) opens AAssets and
 * never calls AAsset_close. Here each AAsset held a stdio stream, so leaked assets accumulate
 * streams; a host run with a small descriptor limit reproduces later opens failing. On the Vita
 * this is the leading hypothesis for boot-03's failed open of an existing script (unconfirmed). At most AOD_MAX_OPEN_ASSET_STREAMS
 * streams are kept open (least-recently-used eviction); an evicted asset reopens at its saved
 * position on next access.
 */
#ifndef AOD_MAX_OPEN_ASSET_STREAMS
#define AOD_MAX_OPEN_ASSET_STREAMS 8
#endif

#ifndef A_FOPEN   /* tests may inject a fault-injecting backend */
#ifdef USE_SCELIBC_IO
#define A_FOPEN sceLibcBridge_fopen
#define A_FCLOSE sceLibcBridge_fclose
#define A_FREAD sceLibcBridge_fread
#define A_FSEEK sceLibcBridge_fseek
#define A_FTELL sceLibcBridge_ftell
#else
#define A_FOPEN fopen
#define A_FCLOSE fclose
#define A_FREAD fread
#define A_FSEEK fseek
#define A_FTELL ftell
#endif
#endif
#ifndef A_FERROR
#ifdef USE_SCELIBC_IO
#define A_FERROR sceLibcBridge_ferror
#else
#define A_FERROR ferror
#endif
#endif

static pthread_mutex_t g_asset_lock = PTHREAD_MUTEX_INITIALIZER;
static aAsset * g_lru_head = nullptr;   // most recently used open stream
static aAsset * g_lru_tail = nullptr;
static int g_open_streams = 0;
static unsigned g_live_assets = 0;

static void lru_unlink(aAsset * a) {
    if (a->lru_prev) a->lru_prev->lru_next = a->lru_next; else if (g_lru_head == a) g_lru_head = a->lru_next;
    if (a->lru_next) a->lru_next->lru_prev = a->lru_prev; else if (g_lru_tail == a) g_lru_tail = a->lru_prev;
    a->lru_prev = a->lru_next = nullptr;
}

static void lru_push_front(aAsset * a) {
    a->lru_prev = nullptr;
    a->lru_next = g_lru_head;
    if (g_lru_head) g_lru_head->lru_prev = a;
    g_lru_head = a;
    if (!g_lru_tail) g_lru_tail = a;
}

static void stream_close(aAsset * a) {
    if (!a->f) return;
    A_FCLOSE(a->f);
    a->f = nullptr;
    lru_unlink(a);
    g_open_streams--;
}

// Caller holds g_asset_lock. Returns false (and logs errno) if the stream cannot be (re)opened.
static bool stream_ensure(aAsset * a) {
    if (a->f) {
        if (g_lru_head != a) { lru_unlink(a); lru_push_front(a); }
        return true;
    }
    while (g_open_streams >= AOD_MAX_OPEN_ASSET_STREAMS && g_lru_tail)
        stream_close(g_lru_tail);
    a->f = A_FOPEN(a->filename, "r");
    if (!a->f) {
#ifdef USE_SCELIBC_IO
        /* SceLibcBridge exports no errno accessor (lib/libc_bridge/nids.yml); newlib's errno is
         * not set by SceLibc's fopen, so it is not reported. */
        l_warn("asset stream (re)open failed: %s errno=unavailable(SceLibc) (open streams %d, live assets %u)",
               a->filename, g_open_streams, g_live_assets);
#else
        l_warn("asset stream (re)open failed: %s errno=%d (open streams %d, live assets %u)",
               a->filename, errno, g_open_streams, g_live_assets);
#endif
        return false;
    }
    if (a->pos && A_FSEEK(a->f, (long) a->pos, SEEK_SET) != 0) {
        A_FCLOSE(a->f);
        a->f = nullptr;
        return false;
    }
    lru_push_front(a);
    g_open_streams++;
    return true;
}

static AAssetManager * g_AAssetManager = nullptr;

AAssetManager * AAssetManager_create() {
    if (g_AAssetManager) return g_AAssetManager;

    assetManager am;

    pthread_mutex_init(&am.mLock, nullptr);

    g_AAssetManager = (AAssetManager *) malloc(sizeof(assetManager));
    memcpy(g_AAssetManager, &am, sizeof(assetManager));

    return g_AAssetManager;
}

AAsset* AAssetManager_open(AAssetManager* mgr, const char* filename, int mode) {
    std::string realp = std::string(DATA_PATH) + std::string("assets/") + std::string(filename);

    auto * a = new aAsset;
    a->filename = strdup(realp.c_str());
    a->f = nullptr;
    a->pos = 0;
    a->fileSize = 0;

    pthread_mutex_lock(&g_asset_lock);
    bool ok = stream_ensure(a);
    if (ok) {
        long sz = -1;
        if (A_FSEEK(a->f, 0, SEEK_END) == 0) sz = A_FTELL(a->f);
        if (sz < 0 || A_FSEEK(a->f, 0, SEEK_SET) != 0) {
            /* size unknown: an error, not an empty asset */
            l_warn("AAssetManager_open(%s): size query failed (fseek/ftell)", realp.c_str());
            stream_close(a);
            ok = false;
        } else {
            a->fileSize = (size_t) sz;
            a->opened = true;
            g_live_assets++;
        }
    }
    pthread_mutex_unlock(&g_asset_lock);

    if (!ok) {
        l_warn("AAssetManager_open(%s): open failed", realp.c_str());
        free(a->filename);
        delete a;
        return nullptr;
    }
    l_debug("AAssetManager_open<%p>(%p, %s, %i): %p", __builtin_return_address(0), mgr, realp.c_str(), mode, a);
    return (AAsset *) a;
}

void AAsset_close(AAsset* asset) {
    l_debug("AAsset_close<%p>(%p)", __builtin_return_address(0), asset);
    if (!asset) return;
    auto * a = (aAsset *) asset;
    pthread_mutex_lock(&g_asset_lock);
    stream_close(a);
    if (a->opened) g_live_assets--;
    pthread_mutex_unlock(&g_asset_lock);
    free(a->filename);
    delete a;
}

int AAsset_read(AAsset* asset, void* buf, size_t count) {
    l_debug("AAsset_read<%p>(%p, %p, %i)", __builtin_return_address(0), asset, buf, count);
    if (!asset) return -1;
    auto * a = (aAsset *) asset;
    if (!a->opened) return -1;
    if (a->pos >= a->fileSize || count == 0) return 0;
    pthread_mutex_lock(&g_asset_lock);
    if (!stream_ensure(a)) { pthread_mutex_unlock(&g_asset_lock); return -1; }
    size_t ret = A_FREAD(buf, 1, count, a->f);
    a->pos += ret;
    int err = (ret == 0) && A_FERROR(a->f);
    if (err) stream_close(a);   /* drop the failed stream; a later read reopens at pos */
    pthread_mutex_unlock(&g_asset_lock);
    if (ret > 0) return (int) ret;
    return err ? -1 : 0;        /* -1 on read error, 0 at end of file (Android semantics) */
}

off_t AAsset_seek(AAsset* asset, off_t offset, int whence) {
    l_debug("AAsset_seek(%p, %d, %i)", asset, offset, whence);
    if (!asset) return (off_t) -1;
    auto * a = (aAsset *) asset;
    if (!a->opened) return -1;
    long long base = whence == SEEK_SET ? 0 : whence == SEEK_CUR ? (long long) a->pos
                   : whence == SEEK_END ? (long long) a->fileSize : -1;
    if (base < 0) return -1;
    long long np = base + (long long) offset;
    // Android's AAsset_seek returns the new position, or -1 when it would leave [0, length].
    if (np < 0 || np > (long long) a->fileSize) return -1;
    pthread_mutex_lock(&g_asset_lock);
    a->pos = (size_t) np;
    if (a->f && A_FSEEK(a->f, (long) np, SEEK_SET) != 0) stream_close(a);   // reopens on next read
    pthread_mutex_unlock(&g_asset_lock);
    return (off_t) np;
}

off_t AAsset_getRemainingLength(AAsset* asset) {
    if (!asset) return (off_t) -1;
    auto * a = (aAsset *) asset;
    if (!a->opened) return -1;
    return (off_t)(a->fileSize - a->pos);
}

off_t AAsset_getLength(AAsset* asset) {
    if (!asset) return (off_t) -1;
    auto * a = (aAsset *) asset;
    return (off_t)a->fileSize;
}

AAssetDir* AAssetManager_openDir(AAssetManager* mgr, const char* dirName) {
    l_error("UNIMPLEMENTED: AAssetManager_openDir: %s", dirName);
    return (AAssetDir *)strdup("dummy");
}

const char* AAssetDir_getNextFileName(AAssetDir* assetDir) {
    l_error("UNIMPLEMENTED: AAssetDir_getNextFileName: %p", assetDir);
    return "";
}

void AAssetDir_close(AAssetDir* assetDir) {
    l_error("UNIMPLEMENTED: AAssetDir_close");
    free(assetDir);
}

int AAsset_openFileDescriptor(AAsset* asset, off_t* outStart, off_t* outLength) {
    if (!asset) {
        l_warn("AAsset_openFileDescriptor(%p, %p, %p): asset is null", asset, outStart, outLength);
        return -1;
    }
    auto * a = (aAsset *) asset;
    if (outStart) *outStart = 0;
    if (outLength) *outLength = a->fileSize;
    pthread_mutex_lock(&g_asset_lock);
    stream_close(a);
    pthread_mutex_unlock(&g_asset_lock);
    int ret = open(a->filename, O_RDONLY);
    l_debug("AAsset_openFileDescriptor(%p/\"%s\", %p, %p): ret %i", asset, a->filename, outStart, outLength, ret);
    return ret;
}
