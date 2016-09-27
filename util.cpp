/*
 * Bermuda Syndrome engine rewrite
 * Copyright (C) 2007-2011 Gregory Montoir
 */

#include <cstdarg>
#include "util.h"

uint16_t g_debugMask;

void debug(uint16_t cm, const char *msg, ...) {
	char buf[1024];
	if (cm & g_debugMask) {
		va_list va;
		va_start(va, msg);
		vsprintf(buf, msg, va);
		va_end(va);
#ifdef BERMUDA_VITA
		debugNetPrintf(DEBUG, "%s\n", buf);
#else
		printf("%s\n", buf);
#endif
		fflush(stdout);
	}
}

void error(const char *msg, ...) {
	char buf[1024];
	va_list va;
	va_start(va, msg);
	vsprintf(buf, msg, va);
	va_end(va);
#ifdef BERMUDA_VITA
	debugNetPrintf(ERROR, "%s\n", buf);
#else
	fprintf(stderr, "ERROR: %s!\n", buf);
#endif
	fflush(stderr);
	exit(-1);
}

void warning(const char *msg, ...) {
	char buf[1024];
	va_list va;
	va_start(va, msg);
	vsprintf(buf, msg, va);
	va_end(va);
#ifdef BERMUDA_VITA
	debugNetPrintf(INFO, "%s\n", buf);
#else
	fprintf(stderr, "WARNING: %s!\n", buf);
#endif
	fflush(stderr);

}
