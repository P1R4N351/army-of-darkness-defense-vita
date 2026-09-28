/* Stand-in for the firmware's hard-float sceGxmSetViewport import: records what it receives. */
typedef struct SceGxmContext SceGxmContext;
volatile float rec[6];
volatile void *rec_ctx;
volatile int rec_calls;
__attribute__((pcs("aapcs-vfp"))) void __vita_softfp_target_sceGxmSetViewport(SceGxmContext *c, float a, float b, float d, float e, float f, float g) {
	rec_ctx = c; rec[0] = a; rec[1] = b; rec[2] = d; rec[3] = e; rec[4] = f; rec[5] = g; rec_calls++;
}
