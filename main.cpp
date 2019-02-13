/*
 * Bermuda Syndrome engine rewrite
 * Copyright (C) 2007-2011 Gregory Montoir
 */

#include <getopt.h>
#include <sys/stat.h>
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif
#include "game.h"
#include "systemstub.h"
#ifdef __vita__
#include <psp2/power.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>
int _newlib_heap_size_user = 192 * 1024 * 1024;
#endif

static const char *USAGE =
	"Bermuda Syndrome\n"
	"Usage: bs [OPTIONS]...\n"
	"  --datapath=PATH    Path to data files (default 'DATA')\n"
	"  --savepath=PATH    Path to save files (default '.')\n"
	"  --musicpath=PATH   Path to music files (default 'MUSIC')\n"
	"  --fullscreen       Fullscreen display\n"
	"  --widescreen=MODE  Widescreen mode ('default', '4:3' or '16:9')\n";

static Game *g_game;
static SystemStub *g_stub;

static void init(const char *dataPath, const char *savePath, const char *musicPath, bool fullscreen, int screenMode) {
	g_stub = SystemStub_SDL_create();
	g_game = new Game(g_stub, dataPath ? dataPath : "DATA", savePath ? savePath : ".", musicPath ? musicPath : "MUSIC");
	g_game->init(fullscreen, screenMode);
}

static void fini() {
	g_game->fini();
	delete g_game;
	delete g_stub;
	g_stub = 0;
}

#ifdef __EMSCRIPTEN__
static void mainLoop() {
	if (!g_stub->_quit) {
		g_game->mainLoop();
	}
}
#endif

#undef main
int main(int argc, char *argv[]) {
#ifdef __vita__
	sceKernelPowerTick(SCE_KERNEL_POWER_TICK_DISABLE_AUTO_SUSPEND);
	scePowerSetArmClockFrequency(444);
	// Initialze File System Factory
	sceIoMkdir("ux0:data", 0777);
	sceIoMkdir("ux0:data/bermuda", 0777);
	sceIoMkdir("ux0:data/bermuda/DATA", 0777);
	sceIoMkdir("ux0:data/bermuda/SAVE", 0777);

	const char *dataPath = "ux0:data/bermuda/DATA";
	const char *savePath = "ux0:data/bermuda/SAVE";
	const char *musicPath = "app0:MUSIC";
	bool fullscreen = false;
	int graphicsScale = 1;
	int screenMode = SCREEN_MODE_4_3;
#else
	char *dataPath = 0;
	char *savePath = 0;
	char *musicPath = 0;
	bool fullscreen = false;
	int screenMode = SCREEN_MODE_DEFAULT;
#endif
	if (argc == 2) {
		// data path as the only command line argument
		struct stat st;
		if (stat(argv[1], &st) == 0 && (S_ISDIR(st.st_mode) || S_ISREG(st.st_mode))) {
			dataPath = strdup(argv[1]);
		}
	}
	while (1) {
		static struct option options[] = {
			{ "datapath",   required_argument, 0, 1 },
			{ "savepath",   required_argument, 0, 2 },
			{ "musicpath",  required_argument, 0, 3 },
			{ "fullscreen", no_argument,       0, 4 },
			{ "widescreen", required_argument, 0, 5 },
			{ "help",       no_argument,       0, 0 },
			{ 0, 0, 0, 0 }
		};
		int index;
		const int c = getopt_long(argc, argv, "", options, &index);
		if (c == -1) {
			break;
		}
		switch (c) {
		case 1:
			dataPath = strdup(optarg);
			break;
		case 2:
			savePath = strdup(optarg);
			break;
		case 3:
			musicPath = strdup(optarg);
			break;
		case 4:
			fullscreen = true;
			break;
		case 5: {
				static const struct {
					const char *name;
					int mode;
				} modes[] = {
					{ "default", SCREEN_MODE_DEFAULT },
					{ "4:3",  SCREEN_MODE_4_3 },
					{ "16:9", SCREEN_MODE_16_9 },
					{ 0, -1 }
				};
				for (int i = 0; modes[i].name; ++i) {
					if (strcmp(modes[i].name, optarg) == 0) {
						screenMode = modes[i].mode;
						break;
					}
				}
			}
			break;
		default:
			fprintf(stdout, "%s", USAGE);
			return 0;
		}
	}
	g_debugMask = DBG_INFO; // | DBG_GAME | DBG_OPCODES | DBG_DIALOGUE;
	init(dataPath, savePath, musicPath, fullscreen, screenMode);
#ifdef __EMSCRIPTEN__
	emscripten_set_main_loop(mainLoop, kCycleDelay, 0);
#elif __vita__
	while (!g_stub->_quit) {
		g_game->mainLoop();
		g_stub->processEvents();
	}
	fini();
#else
	uint32_t lastFrameTimeStamp = g_stub->getTimeStamp();
	while (!g_stub->_quit) {
		g_game->mainLoop();
		const uint32_t end = lastFrameTimeStamp + kCycleDelay;
		do {
			g_stub->sleep(10);
			g_stub->processEvents();
		} while (!g_stub->_pi.fastMode && g_stub->getTimeStamp() < end);
		lastFrameTimeStamp = g_stub->getTimeStamp();
	}
	fini();
#endif
#ifdef __vita__
	sceKernelExitProcess(0);
#else
	free(dataPath);
	free(savePath);
	free(musicPath);
#endif
	return 0;
}
