/* Forced-include for tests/asset_fault_test.cpp: routes asset_manager.cpp's stdio backend through
 * fault-injecting wrappers. */
#pragma once
#include <stdio.h>
extern int g_fail_read, g_fail_seek_end, g_fail_tell;
FILE *fault_fopen(const char *p, const char *m);
size_t fault_fread(void *b, size_t s, size_t n, FILE *f);
int fault_fseek(FILE *f, long o, int w);
long fault_ftell(FILE *f);
int fault_ferror(FILE *f);
#define A_FOPEN fault_fopen
#define A_FCLOSE fclose
#define A_FREAD fault_fread
#define A_FSEEK fault_fseek
#define A_FTELL fault_ftell
#define A_FERROR fault_ferror
