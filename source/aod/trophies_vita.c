/* MIT. Optional homebrew-only sceNpTrophy backend; no PSN/network calls.
 * ABI declarations follow Golden Balloon / established vitaGL integrations.
 * See docs/TROPHIES.md and LICENSES/GoldenBalloon-MIT.txt. */
#include "aod/trophies.h"
#include "aod/trophy_storage.h"
#include "utils/logger.h"
#include <psp2/sysmodule.h>
#include <psp2/common_dialog.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/kernel/processmgr.h>
#include <vitaGL.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>

extern int sceNpTrophyInit(void *);
extern int sceNpTrophyCreateContext(int *, const void *, const void *, uint64_t);
extern int sceNpTrophyCreateHandle(int *);
extern int sceNpTrophyDestroyHandle(int);
extern int sceNpTrophyDestroyContext(int);
extern int sceNpTrophyTerm(void);
extern int sceNpTrophySetupDialogInit(void *);
extern SceCommonDialogStatus sceNpTrophySetupDialogGetStatus(void);
extern int sceNpTrophySetupDialogTerm(void);
extern int sceNpTrophyUnlockTrophy(int, int, int, int *);
extern int sceNpTrophyGetTrophyUnlockState(int, int, void *, uint32_t *);
typedef struct {
    int sdkVersion;
    SceCommonDialogParam commonParam;
    int context, options;
    uint8_t reserved[128];
} TrophySetupParam;

#define JOURNAL DATA_PATH "files/homebrew-trophies-v1.dat"
#define PACK "app0:sce_sys/trophy/AODD00001_00/TROPHY.TRP"
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_t worker;
static uint64_t incoming;
static int started, stopping, accepting, available;
static int context = -1, handle = -1, service, module_loaded;

int aod_trophies_receive(const char *message) {
    if (!message || strncmp(message, AOD_TROPHY_PREFIX, sizeof(AOD_TROPHY_PREFIX)-1)) return 0;
    int id = aod_trophy_message(message);
    if (id >= 0) {
        pthread_mutex_lock(&lock);
        if (accepting) incoming |= UINT64_C(1) << id;
        pthread_mutex_unlock(&lock);
    }
    return 1; /* consume malformed reserved messages too; never log private input */
}
static int store(void *state, uint64_t earned, uint64_t done) {
    int *initially_absent = state;
    int rc = aod_trophy_save_pending_file(JOURNAL, earned, done, *initially_absent);
    if (!rc) *initially_absent = 0;
    return rc;
}
static int platform_unlock(void *unused, unsigned id) {
    (void)unused;
    int platinum = -1;
    int rc = sceNpTrophyUnlockTrophy(context, handle, (int)id, &platinum);
    if (rc < 0) {
        uint32_t bits[4] = {0}, count = 0;
        /* Avoid undocumented error-number guesses; query already-unlocked state. */
        if (sceNpTrophyGetTrophyUnlockState(context, handle, bits, &count) >= 0 &&
            (bits[id / 32] & (UINT32_C(1) << (id % 32)))) return 0;
        l_warn("homebrew trophy %u unlock error 0x%08x; queued for retry", id, rc);
        return -1;
    }
    l_info("homebrew trophy %u delivered", id);
    return 0;
}
static void *work(void *unused) {
    (void)unused;
    aod_trophy_core core = {0};
    int load = aod_trophy_load_file(JOURNAL, &core.earned, &core.delivered);
    int initially_absent = load == 1;
    if (load < 0) {
        l_warn("homebrew trophies: journal unreadable/corrupt; preserved, disabled this run");
        pthread_mutex_lock(&lock); accepting = 0; pthread_mutex_unlock(&lock);
        return NULL;
    }
    core.saved_earned = core.earned; core.saved_delivered = core.delivered;
    if (available) {
        uint32_t bits[4] = {0}, count = 0;
        if (sceNpTrophyGetTrophyUnlockState(context, handle, bits, &count) >= 0)
            core.delivered = core.earned & ((uint64_t)bits[0] | ((uint64_t)bits[1] << 32));
    }
    aod_trophy_schedule schedule = {0};
    int previous_error = 0;
    for (;;) {
        pthread_mutex_lock(&lock);
        uint64_t batch = incoming; incoming = 0;
        int stop = stopping;
        pthread_mutex_unlock(&lock);
        uint64_t now = sceKernelGetProcessTimeWide();
        int rc = aod_trophy_poll(&core, &schedule, batch, available, stop, now,
                                store, platform_unlock, &initially_absent);
        if (rc < 0) {
            if (rc != previous_error) l_warn("homebrew trophies: worker error %d (pending retained)", rc);
        }
        previous_error = rc;
        if (stop) break; /* step attempted final persistence, no new platform call */
        sceKernelDelayThread(250000);
    }
    return NULL;
}
static void setup(void) {
    static const char comm_id[12] = "AODD00001";
    static const unsigned char signature[160] = {0xb9,0xdd,0xe1,0x3b,1,0};
    FILE *f = fopen(PACK, "rb");
    if (!f) { l_info("homebrew trophies: no pack; earned events journalled only"); return; }
    fclose(f);
    int rc = sceSysmoduleLoadModule(SCE_SYSMODULE_NP_TROPHY);
    if (rc < 0) goto unavailable;
    module_loaded = 1;
    rc = sceNpTrophyInit(NULL);
    if (rc < 0) goto unavailable;
    service = 1;
    rc = sceNpTrophyCreateContext(&context, comm_id, signature, 0);
    if (rc < 0) goto unavailable;
    TrophySetupParam p;
    memset(&p, 0, sizeof(p));
    _sceCommonDialogSetMagicNumber(&p.commonParam);
    p.sdkVersion = PSP2_SDK_VERSION; p.context = context;
    rc = sceNpTrophySetupDialogInit(&p);
    if (rc < 0) goto unavailable;
    /* Setup belongs to startup, BEFORE nativeStart/nativeCreate; never a gameplay frame.
     * Bound missing/broken plugin dialog to 30 seconds; rendering stays on main. */
    uint64_t deadline = sceKernelGetProcessTimeWide() + UINT64_C(30000000);
    SceCommonDialogStatus status;
    do {
        status = sceNpTrophySetupDialogGetStatus();
        if (status != SCE_COMMON_DIALOG_STATUS_RUNNING) break;
        vglSwapBuffers(GL_TRUE);
    } while (sceKernelGetProcessTimeWide() < deadline);
    rc = sceNpTrophySetupDialogTerm();
    if (rc < 0 || status != SCE_COMMON_DIALOG_STATUS_FINISHED) goto unavailable;
    rc = sceNpTrophyCreateHandle(&handle);
    if (rc < 0) goto unavailable;
    available = 1;
    l_info("homebrew trophies: AODD00001 context ready (not gameplay proof)");
    return;
unavailable:
    l_warn("homebrew trophies unavailable: 0x%08x; game continues, pending retained", rc);
}
void aod_trophies_init(void) {
    if (started) return;
    /* Explicit local opt-out; no plugin or device configuration edits. */
    FILE *off = fopen(DATA_PATH "no-trophies", "rb");
    if (off) { fclose(off); return; }
    setup();
    accepting = 1;
    if (pthread_create(&worker, NULL, work, NULL)) {
        accepting = 0;
        l_warn("homebrew trophies: cannot create worker; disabled");
        return;
    }
    started = 1;
}
void aod_trophies_stop(void) {
    if (started) {
        pthread_mutex_lock(&lock); accepting = 0; stopping = 1; pthread_mutex_unlock(&lock);
        pthread_join(worker, NULL); started = 0;
    }
    if (handle >= 0) sceNpTrophyDestroyHandle(handle);
    if (context >= 0) sceNpTrophyDestroyContext(context);
    if (service) sceNpTrophyTerm();
    if (module_loaded) sceSysmoduleUnloadModule(SCE_SYSMODULE_NP_TROPHY);
}
