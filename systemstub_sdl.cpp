/*
 * Bermuda Syndrome engine rewrite
 * Copyright (C) 2007-2011 Gregory Montoir
 */

#include <SDL.h>
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif
#include "mixer.h"
#include "scaler.h"
#include "screenshot.h"
#include "systemstub.h"

enum {
	kSoundSampleRate = 22050,
	kSoundSampleSize = 4096,
	kVideoSurfaceDepth = 32,
	kJoystickCommitValue = 16384,
};

#ifdef __vita__
#define BTN_TRIANGLE 0
#define BTN_CIRCLE 1
#define BTN_CROSS 2
#define BTN_SQUARE 3
#define BTN_LTRIGGER 4
#define BTN_RTRIGGER 5
#define BTN_DOWN 6
#define BTN_LEFT 7
#define BTN_UP 8
#define BTN_RIGHT 9
#define BTN_SELECT 10
#define BTN_START 11
#endif

struct SystemStub_SDL : SystemStub {
	Mixer *_mixer;
#if SDL_VERSION_ATLEAST(2, 0, 0)
	SDL_Window *_window;
	SDL_Renderer *_renderer;
	SDL_Texture *_gameTexture;
	SDL_Texture *_videoTexture;
	SDL_Texture *_backgroundTexture;
	SDL_GameController *_controller;
#else
	SDL_Surface *_screen;
	SDL_Overlay *_yuv;
#endif
	SDL_PixelFormat *_fmt;
	uint32_t *_gameBuffer;
	uint16_t *_videoBuffer;
	uint32_t _pal[256];
	int _screenW, _screenH;
	int _videoW, _videoH;
	int _widescreenW, _widescreenH;
	bool _fullScreenDisplay;
	int _soundSampleRate;
	const uint8_t *_iconData;
	int _iconSize;
	int _screenshot;
	bool _widescreen;
#ifdef __vita__
	SDL_Joystick *_joystick;
#endif

	SystemStub_SDL() :
#if SDL_VERSION_ATLEAST(2, 0, 0)
		_window(0), _renderer(0), _gameTexture(0), _videoTexture(0), _backgroundTexture(0),
#else
		_screen(0), _yuv(0),
#endif
		_fmt(0),
		_gameBuffer(0), _videoBuffer(0),
		_iconData(0), _iconSize(0) {
		_screenshot = 1;
		_mixer = Mixer_SDL_create(this);
	}
	virtual ~SystemStub_SDL() {
		delete _mixer;
	}

	virtual void init(const char *title, int w, int h, bool fullscreen, int screenMode);
	virtual void destroy();
	virtual void setIcon(const uint8_t *data, int size);
	virtual void showCursor(bool show);
	virtual void setPalette(const uint8_t *pal, int n);
	virtual void fillRect(int x, int y, int w, int h, uint8_t color);
	virtual void copyRect(int x, int y, int w, int h, const uint8_t *buf, int pitch, bool transparent);
	virtual void darkenRect(int x, int y, int w, int h);
	virtual void copyRectWidescreen(int w, int h, const uint8_t *buf, int pitch);
	virtual void clearWidescreen();
	virtual void updateScreen();
	virtual void setYUV(bool flag, int w, int h);
	virtual uint8_t *lockYUV(int *pitch);
	virtual void unlockYUV();
	virtual void processEvents();
	virtual void sleep(int duration);
	virtual uint32_t getTimeStamp();
	virtual void lockAudio();
	virtual void unlockAudio();
	virtual void startAudio(AudioCallback callback, void *param);
	virtual void stopAudio();
	virtual int getOutputSampleRate();
	virtual Mixer *getMixer() { return _mixer; }

	void updateMousePosition(int x, int y);
	void handleEvent(const SDL_Event &ev, bool &paused);
#ifdef __vita__
	void renderCopyVita(SDL_Renderer *renderer, SDL_Texture *texture);
#endif
	void setFullscreen(bool fullscreen);
};

SystemStub *SystemStub_SDL_create() {
	return new SystemStub_SDL();
}

#ifdef __EMSCRIPTEN__
static int eventHandler(void *userdata, SDL_Event *ev) {
	bool paused;
	((SystemStub_SDL *)userdata)->handleEvent(*ev, paused);
	return 0;
}
#endif

void SystemStub_SDL::init(const char *title, int w, int h, bool fullscreen, int screenMode) {
#ifdef __vita__
	SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_JOYSTICK);
#else
	SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMECONTROLLER);
#endif
	SDL_ShowCursor(SDL_DISABLE);
	_quit = false;
	memset(&_pi, 0, sizeof(_pi));

	_mixer->open();

	_widescreen = false;
	switch (screenMode) {
	case SCREEN_MODE_DEFAULT: {
			SDL_DisplayMode dm;
			if (SDL_GetDesktopDisplayMode(0, &dm) == 0) {
				_widescreen = ((dm.w / (float)dm.h) >= (16 / 9.f));
			}
		}
		break;
	case SCREEN_MODE_4_3:
		_widescreen = false;
		break;
	case SCREEN_MODE_16_9:
		_widescreen = true;
		break;
	}

#if SDL_VERSION_ATLEAST(2, 0, 0)
	int windowW = w;
	int windowH = h;
	if (_widescreen) {
		windowW = windowH * 16 / 9;
		_widescreenW = windowW;
		_widescreenH = windowH;
	} else {
		_widescreenW = 0;
		_widescreenH = 0;
	}
#ifdef __vita__
	// improve image quality on Vita by enabling linear filtering
	// this is only recently supported by SDL2 for Vita since 2017/12/24
	SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "linear");
#endif
	_window = SDL_CreateWindow(title, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, windowW, windowH, 0);
	if (_widescreen) {
		SDL_GetWindowSize(_window, &_widescreenW, &_widescreenH);
	}
	if (_iconData) {
		SDL_RWops *rw = SDL_RWFromConstMem(_iconData, _iconSize);
		SDL_Surface *icon = SDL_LoadBMP_RW(rw, 1);
		if (icon) {
			SDL_SetWindowIcon(_window, icon);
			SDL_FreeSurface(icon);
		}
	}
	_renderer = SDL_CreateRenderer(_window, -1, SDL_RENDERER_ACCELERATED);
	if (_widescreen) {
		SDL_RenderSetLogicalSize(_renderer, _widescreenW, _widescreenH);
	} else {
		SDL_RenderSetLogicalSize(_renderer, w, h);
	}

	static const uint32_t pfmt = SDL_PIXELFORMAT_RGB888; //SDL_PIXELFORMAT_RGB565;
	_fmt = SDL_AllocFormat(pfmt);
	_gameTexture = SDL_CreateTexture(_renderer, pfmt, SDL_TEXTUREACCESS_STREAMING, w, h);
	if (_widescreen) {
		_backgroundTexture = SDL_CreateTexture(_renderer, pfmt, SDL_TEXTUREACCESS_STREAMING, w, h);
	}

	SDL_GameControllerAddMappingsFromFile("gamecontrollerdb.txt");
	_controller = 0;
	for (int i = 0; i < SDL_NumJoysticks(); ++i) {
		if (SDL_IsGameController(i)) {
			_controller = SDL_GameControllerOpen(i);
			break;
		}
	}
#ifdef __vita__
	_joystick = SDL_JoystickOpen(0);
#endif
#else
	SDL_WM_SetCaption(title, NULL);
#endif

	_screenW = w;
	_screenH = h;

	const int bufferSize = _screenW * _screenH;
	_gameBuffer = (uint32_t *)calloc(bufferSize, sizeof(uint32_t));
	if (!_gameBuffer) {
		error("SystemStub_SDL::init() Unable to allocate offscreen buffer");
	}
	memset(_pal, 0, sizeof(_pal));

	_videoW = _videoH = 0;

	_fullScreenDisplay = false;
	setFullscreen(fullscreen);
	_soundSampleRate = 0;

#ifdef __EMSCRIPTEN__
	emscripten_SDL_SetEventHandler(eventHandler, this);
#endif
}

void SystemStub_SDL::destroy() {
	_mixer->close();

#if SDL_VERSION_ATLEAST(2, 0, 0)
	if (_gameTexture) {
		SDL_DestroyTexture(_gameTexture);
		_gameTexture = 0;
	}
	if (_videoTexture) {
		SDL_DestroyTexture(_videoTexture);
		_videoTexture = 0;
	}
	if (_backgroundTexture) {
		SDL_DestroyTexture(_backgroundTexture);
		_backgroundTexture = 0;
	}
	if (_fmt) {
		SDL_FreeFormat(_fmt);
		_fmt = 0;
	}
	SDL_DestroyRenderer(_renderer);
	SDL_DestroyWindow(_window);

	if (_controller) {
		SDL_GameControllerClose(_controller);
	}
#else
	if (_screen) {
		// free()'d by SDL_Quit()
		_screen = 0;
	}
#endif
	if (_gameBuffer) {
		free(_gameBuffer);
		_gameBuffer = 0;
	}
#ifdef __vita__
	if (_joystick) {
		SDL_JoystickClose(_joystick);
		_joystick = 0;
	}
#endif
	SDL_Quit();
}

void SystemStub_SDL::setIcon(const uint8_t *data, int size) {
	_iconData = data;
	_iconSize = size;
}

void SystemStub_SDL::showCursor(bool show) {
	SDL_ShowCursor(show ? SDL_ENABLE : SDL_DISABLE);
}

void SystemStub_SDL::setPalette(const uint8_t *pal, int n) {
	assert(n <= 256);
	_pal[0] = SDL_MapRGB(_fmt, 0, 0, 0); pal += 4;
	for (int i = 1; i < n; ++i) {
		_pal[i] = SDL_MapRGB(_fmt, pal[2], pal[1], pal[0]);
		pal += 4;
	}
}

static bool clipRect(int screenW, int screenH, int &x, int &y, int &w, int &h) {
	if (x < 0) {
		x = 0;
	}
	if (x + w > screenW) {
		w = screenW - x;
	}
	if (y < 0) {
		y = 0;
	}
	if (y + h > screenH) {
		h = screenH - y;
	}
	return (w > 0 && h > 0);
}

void SystemStub_SDL::fillRect(int x, int y, int w, int h, uint8_t color) {
	if (!clipRect(_screenW, _screenH, x, y, w, h)) return;

	const uint32_t fillColor = _pal[color];
	uint32_t *p = _gameBuffer + y * _screenW + x;
	while (h--) {
		for (int i = 0; i < w; ++i) {
			p[i] = fillColor;
		}
		p += _screenW;
	}
}

void SystemStub_SDL::copyRect(int x, int y, int w, int h, const uint8_t *buf, int pitch, bool transparent) {
	if (!clipRect(_screenW,  _screenH, x, y, w, h)) return;

	uint32_t *p = _gameBuffer + y * _screenW + x;
	buf += h * pitch;
	while (h--) {
		buf -= pitch;
		for (int i = 0; i < w; ++i) {
			if (!transparent || buf[i] != 0) {
				p[i] = _pal[buf[i]];
			}
		}
		p += _screenW;
	}
}

void SystemStub_SDL::darkenRect(int x, int y, int w, int h) {
	if (!clipRect(_screenW, _screenH, x, y, w, h)) return;

	const uint32_t redBlueMask = _fmt->Rmask | _fmt->Bmask;
	const uint32_t greenMask = _fmt->Gmask;

	uint32_t *p = _gameBuffer + y * _screenW + x;
	while (h--) {
		for (int i = 0; i < w; ++i) {
			uint32_t color = ((p[i] & redBlueMask) >> 1) & redBlueMask;
			color |= ((p[i] & greenMask) >> 1) & greenMask;
			p[i] = color;
		}
		p += _screenW;
	}
}

static void blur_h(int radius, const uint32_t *src, int srcPitch, int w, int h, const SDL_PixelFormat *fmt, uint32_t *dst, int dstPitch) {

	const int count = 2 * radius + 1;

	for (int y = 0; y < h; ++y) {

		uint32_t r = 0;
		uint32_t g = 0;
		uint32_t b = 0;

		uint32_t color;

		for (int x = -radius; x <= radius; ++x) {
			color = src[MAX(x, 0)];
			r += (color & fmt->Rmask) >> fmt->Rshift;
			g += (color & fmt->Gmask) >> fmt->Gshift;
			b += (color & fmt->Bmask) >> fmt->Bshift;
		}
		dst[0] = ((r / count) << fmt->Rshift) | ((g / count) << fmt->Gshift) | ((b / count) << fmt->Bshift);

		for (int x = 1; x < w; ++x) {
			color = src[MIN(x + radius, w - 1)];
			r += (color & fmt->Rmask) >> fmt->Rshift;
			g += (color & fmt->Gmask) >> fmt->Gshift;
			b += (color & fmt->Bmask) >> fmt->Bshift;

			color = src[MAX(x - radius - 1, 0)];
			r -= (color & fmt->Rmask) >> fmt->Rshift;
			g -= (color & fmt->Gmask) >> fmt->Gshift;
			b -= (color & fmt->Bmask) >> fmt->Bshift;

			dst[x] = ((r / count) << fmt->Rshift) | ((g / count) << fmt->Gshift) | ((b / count) << fmt->Bshift);
		}

		src += srcPitch;
		dst += dstPitch;
	}
}

static void blur_v(int radius, const uint32_t *src, int srcPitch, int w, int h, const SDL_PixelFormat *fmt, uint32_t *dst, int dstPitch) {

	const int count = 2 * radius + 1;

	for (int x = 0; x < w; ++x) {

		uint32_t r = 0;
		uint32_t g = 0;
		uint32_t b = 0;

		uint32_t color;

		for (int y = -radius; y <= radius; ++y) {
			color = src[MAX(y, 0) * srcPitch];
			r += (color & fmt->Rmask) >> fmt->Rshift;
			g += (color & fmt->Gmask) >> fmt->Gshift;
			b += (color & fmt->Bmask) >> fmt->Bshift;
		}
		dst[0] = ((r / count) << fmt->Rshift) | ((g / count) << fmt->Gshift) | ((b / count) << fmt->Bshift);

		for (int y = 1; y < h; ++y) {
			color = src[MIN(y + radius, h - 1) * srcPitch];
			r += (color & fmt->Rmask) >> fmt->Rshift;
			g += (color & fmt->Gmask) >> fmt->Gshift;
			b += (color & fmt->Bmask) >> fmt->Bshift;

			color = src[MAX(y - radius - 1, 0) * srcPitch];
			r -= (color & fmt->Rmask) >> fmt->Rshift;
			g -= (color & fmt->Gmask) >> fmt->Gshift;
			b -= (color & fmt->Bmask) >> fmt->Bshift;

			dst[y * dstPitch] = ((r / count) << fmt->Rshift) | ((g / count) << fmt->Gshift) | ((b / count) << fmt->Bshift);
		}

		++src;
		++dst;
	}
}

void SystemStub_SDL::copyRectWidescreen(int w, int h, const uint8_t *buf, int bufPitch) {
	if (_widescreen) {
		void *ptr = 0;
		int dstPitch = 0;
		if (SDL_LockTexture(_backgroundTexture, 0, &ptr, &dstPitch) == 0) {

			uint32_t *src = (uint32_t *)malloc(w * sizeof(uint32_t) * h * sizeof(uint32_t));
			uint32_t *tmp = (uint32_t *)malloc(w * sizeof(uint32_t) * h * sizeof(uint32_t));
			uint32_t *dst = (uint32_t *)ptr;
			if (src && tmp) {
				buf += h * bufPitch;
				for (int y = 0; y < h; ++y) {
					buf -= bufPitch;
					for (int x = 0; x < w; ++x) {
						src[y * w + x] = _pal[buf[x]];
					}
				}
				static const int radius = 16;
				blur_h(radius, src, w, w, h, _fmt, tmp, w);
				blur_v(radius, tmp, w, w, h, _fmt, dst, dstPitch / sizeof(uint32_t));
			}
			free(src);
			free(tmp);

			SDL_UnlockTexture(_backgroundTexture);
		}
	}
}

void SystemStub_SDL::clearWidescreen() {
	if (_widescreen) {
		void *dst = 0;
		int dstPitch = 0;
		if (SDL_LockTexture(_backgroundTexture, 0, &dst, &dstPitch) == 0) {
			assert((dstPitch & 3) == 0);
			const uint32_t color = _pal[0]; // palette #0 is black
			for (int y = 0; y < _screenH; ++y) {
				for (int x = 0; x < _screenW; ++x) {
					((uint32_t *)dst)[x] = color;
				}
				dst = (uint8_t *)dst + dstPitch;
			}
			SDL_UnlockTexture(_backgroundTexture);
		}
	}
}

void SystemStub_SDL::updateScreen() {
#if SDL_VERSION_ATLEAST(2, 0, 0)
	SDL_RenderClear(_renderer);
	// background graphics (left/right borders)
	if (_widescreen) {
		SDL_RenderCopy(_renderer, _backgroundTexture, 0, 0);
	}
	// game graphics
	SDL_UpdateTexture(_gameTexture, NULL, _gameBuffer, _screenW * sizeof(uint32_t));
	SDL_Rect r;
	r.w = _screenW;
	r.h = _screenH;
	if (_widescreen) {
		r.x = (_widescreenW - r.w) / 2;
		r.y = (_widescreenH - r.h) / 2;
	} else {
		r.x = 0;
		r.y = 0;
	}
#ifdef __vita__
	renderCopyVita(_renderer, _gameTexture);
#else
	SDL_RenderCopy(_renderer, _gameTexture, NULL, &r);
#endif
	// display
	SDL_RenderPresent(_renderer);
#else
	if (SDL_LockSurface(_screen) == 0) {
		for (int y = 0; y < _screenH; ++y) {
			uint8_t *dst = (uint8_t *)_screen->pixels + y * _screen->pitch;
			memcpy(dst, _gameBuffer + y * _screenW, _screenW * sizeof(uint32_t));
		}
		SDL_UnlockSurface(_screen);
		SDL_UpdateRect(_screen, 0, 0, 0, 0);
	}
#endif
}

void SystemStub_SDL::setYUV(bool flag, int w, int h) {
	if (flag) {
#ifndef __EMSCRIPTEN__
#if SDL_VERSION_ATLEAST(2, 0, 0)
		if (!_videoTexture) {
			_videoTexture = SDL_CreateTexture(_renderer, SDL_PIXELFORMAT_UYVY, SDL_TEXTUREACCESS_STREAMING, w, h);
		}
		if (!_videoBuffer) {
			_videoBuffer = (uint16_t *)malloc(w * h * sizeof(uint16_t));
		}
#else
		if (!_yuv) {
			_yuv = SDL_CreateYUVOverlay(w, h, SDL_UYVY_OVERLAY, _screen);
		}
#endif
#endif
		_videoW = w;
		_videoH = w;
	} else {
#ifndef __EMSCRIPTEN__
#if SDL_VERSION_ATLEAST(2, 0, 0)
		if (_videoTexture) {
			SDL_DestroyTexture(_videoTexture);
			_videoTexture = 0;
		}
		if (_videoBuffer) {
			free(_videoBuffer);
			_videoBuffer = 0;
		}
#else
		if (_yuv) {
			SDL_FreeYUVOverlay(_yuv);
			_yuv = 0;
		}
#endif
#endif
	}
}

uint8_t *SystemStub_SDL::lockYUV(int *pitch) {
#ifndef __EMSCRIPTEN__
#if SDL_VERSION_ATLEAST(2, 0, 0)
	*pitch = _videoW * sizeof(uint16_t);
	return (uint8_t *)_videoBuffer;
#else
	if (_yuv && SDL_LockYUVOverlay(_yuv) == 0) {
		*pitch = _yuv->pitches[0];
		return _yuv->pixels[0];
	}
	return 0;
#endif
#else
	return 0;
#endif
}

void SystemStub_SDL::unlockYUV() {
#ifndef __EMSCRIPTEN__
#if SDL_VERSION_ATLEAST(2, 0, 0)
	SDL_RenderClear(_renderer);
	if (_videoBuffer) {
		SDL_UpdateTexture(_videoTexture, NULL, _videoBuffer, _videoW * sizeof(uint16_t));
	}
#ifdef __vita__
	renderCopyVita(_renderer, _videoTexture);
#else
	SDL_RenderCopy(_renderer, _videoTexture, NULL, NULL);
#endif
	SDL_RenderPresent(_renderer);
#else
	if (_yuv) {
		SDL_UnlockYUVOverlay(_yuv);
		SDL_Rect r;
		if (_yuv->w * 2 <= _screenW && _yuv->h * 2 <= _screenH) {
			r.w = _yuv->w * 2;
			r.h = _yuv->h * 2;
		} else {
			r.w = _yuv->w;
			r.h = _yuv->h;
		}
		r.x = (_screenW - r.w) / 2;
		r.y = (_screenH - r.h) / 2;
		SDL_DisplayYUVOverlay(_yuv, &r);
	}
#endif
#endif
}

void SystemStub_SDL::processEvents() {
	bool paused = false;
	while (!_quit) {
		SDL_Event ev;
		while (SDL_PollEvent(&ev)) {
			handleEvent(ev, paused);
		}
		if (paused) {
			SDL_Delay(100);
		} else {
			break;
		}
	}
}

#ifdef __vita__
void SystemStub_SDL::renderCopyVita(SDL_Renderer *renderer, SDL_Texture *texture) {

	SDL_Rect src;
	src.x = 0;
	src.y = 0;
	src.w = _screenW;
	src.h = _screenH;

	SDL_Rect dst;
	dst.h = 544;
	if (_fullScreenDisplay) {
		dst.w = 960; // stretch to screen
	} else {
		dst.w = (float)src.w * ((float)dst.h / (float)src.h); // fit to screen
	}
	dst.y = 0;
	dst.x = (960 - dst.w) / 2;

	SDL_RenderCopy(renderer, texture, &src, &dst);
}
#endif

void SystemStub_SDL::updateMousePosition(int x, int y) {
	if (_widescreen) {
		x -= (_widescreenW - _screenW) / 2;
		y -= (_widescreenH - _screenH) / 2;
	}
	_pi.mouseX = x;
	_pi.mouseY = y;
}


void SystemStub_SDL::handleEvent(const SDL_Event &ev, bool &paused) {
	switch (ev.type) {
#ifdef __vita__
		case SDL_JOYBUTTONDOWN:
		case SDL_JOYBUTTONUP:
			if (_joystick) {
				const bool pressed = (ev.jbutton.state == SDL_PRESSED);
				switch (ev.jbutton.button) {
					case BTN_SQUARE:
						_pi.space = pressed; // use weapon
						break;
					case BTN_CIRCLE:
						_pi.shift = pressed; // run / store weapon
						break;
					case BTN_CROSS:
						_pi.enter = pressed; // use current selected item / skip video or dialogue
						break;
					case BTN_TRIANGLE:
						_pi.tab = pressed; // display inventory
						break;
					case BTN_UP:
						if (pressed) {
							_pi.dirMask |= PlayerInput::DIR_UP;
						} else if (ev.jbutton.state == SDL_RELEASED){
							_pi.dirMask &= ~PlayerInput::DIR_UP;
						}
						break;
					case BTN_RIGHT:
						if (pressed) {
							_pi.dirMask |= PlayerInput::DIR_RIGHT;
						} else if (ev.jbutton.state == SDL_RELEASED){
							_pi.dirMask &= ~PlayerInput::DIR_RIGHT;
						}
						break;
					case BTN_DOWN:
						if (pressed) {
							_pi.dirMask |= PlayerInput::DIR_DOWN;
						} else if (ev.jbutton.state == SDL_RELEASED) {
							_pi.dirMask &= ~PlayerInput::DIR_DOWN;
						}
						break;
					case BTN_LEFT:
						if (pressed) {
							_pi.dirMask |= PlayerInput::DIR_LEFT;
						} else if (ev.jbutton.state == SDL_RELEASED){
							_pi.dirMask &= ~PlayerInput::DIR_LEFT;
						}
						break;
					case BTN_RTRIGGER:
						_pi.save = pressed; // save state
						break;
					case BTN_LTRIGGER:
						_pi.load = pressed; // load last saved state
						break;
					case BTN_SELECT:
						if (pressed) {
							_fullScreenDisplay = !_fullScreenDisplay;
							setFullscreen(_fullScreenDisplay);
						}
						break;
					case BTN_START:
						_pi.escape = pressed;
						break;
				}
			}
			break;
#else
	case SDL_QUIT:
		_quit = true;
		break;
#if SDL_VERSION_ATLEAST(2, 0, 0)
	case SDL_WINDOWEVENT:
		switch (ev.window.event) {
		case SDL_WINDOWEVENT_FOCUS_GAINED:
		case SDL_WINDOWEVENT_FOCUS_LOST:
			paused = (ev.window.event == SDL_WINDOWEVENT_FOCUS_LOST);
			SDL_PauseAudio(paused);
			break;
		}
		break;
	case SDL_CONTROLLERAXISMOTION:
		if (_controller) {
			switch (ev.caxis.axis) {
			case SDL_CONTROLLER_AXIS_LEFTX:
			case SDL_CONTROLLER_AXIS_RIGHTX:
				if (ev.caxis.value < -kJoystickCommitValue) {
					_pi.dirMask |= PlayerInput::DIR_LEFT;
				} else {
					_pi.dirMask &= ~PlayerInput::DIR_LEFT;
				}
				if (ev.caxis.value > kJoystickCommitValue) {
					_pi.dirMask |= PlayerInput::DIR_RIGHT;
				} else {
					_pi.dirMask &= ~PlayerInput::DIR_RIGHT;
				}
				break;
			case SDL_CONTROLLER_AXIS_LEFTY:
			case SDL_CONTROLLER_AXIS_RIGHTY:
				if (ev.caxis.value < -kJoystickCommitValue) {
					_pi.dirMask |= PlayerInput::DIR_UP;
				} else {
					_pi.dirMask &= ~PlayerInput::DIR_UP;
				}
				if (ev.caxis.value > kJoystickCommitValue) {
					_pi.dirMask |= PlayerInput::DIR_DOWN;
				} else {
					_pi.dirMask &= ~PlayerInput::DIR_DOWN;
				}
				break;
			}
		}
		break;
	case SDL_CONTROLLERBUTTONDOWN:
	case SDL_CONTROLLERBUTTONUP:
		if (_controller) {
			const bool pressed = ev.cbutton.state == SDL_PRESSED;
			switch (ev.cbutton.button) {
			case SDL_CONTROLLER_BUTTON_A:
				_pi.enter = pressed;
				break;
			case SDL_CONTROLLER_BUTTON_B:
				_pi.space = pressed;
				break;
			case SDL_CONTROLLER_BUTTON_X:
				_pi.shift = pressed;
				break;
			case SDL_CONTROLLER_BUTTON_Y:
				_pi.ctrl = pressed;
				break;
			case SDL_CONTROLLER_BUTTON_BACK:
				_pi.escape = pressed;
				break;
			case SDL_CONTROLLER_BUTTON_GUIDE:
			case SDL_CONTROLLER_BUTTON_START:
				_pi.tab = pressed;
				break;
			case SDL_CONTROLLER_BUTTON_DPAD_UP:
				if (pressed) {
					_pi.dirMask |= PlayerInput::DIR_UP;
				} else {
					_pi.dirMask &= ~PlayerInput::DIR_UP;
				}
				break;
			case SDL_CONTROLLER_BUTTON_DPAD_DOWN:
				if (pressed) {
					_pi.dirMask |= PlayerInput::DIR_DOWN;
				} else {
					_pi.dirMask &= ~PlayerInput::DIR_DOWN;
				}
				break;
			case SDL_CONTROLLER_BUTTON_DPAD_LEFT:
				if (pressed) {
					_pi.dirMask |= PlayerInput::DIR_LEFT;
				} else {
					_pi.dirMask &= ~PlayerInput::DIR_LEFT;
				}
				break;
			case SDL_CONTROLLER_BUTTON_DPAD_RIGHT:
				if (pressed) {
					_pi.dirMask |= PlayerInput::DIR_RIGHT;
				} else {
					_pi.dirMask &= ~PlayerInput::DIR_RIGHT;
				}
				break;
			}
		}
		break;
#else
	case SDL_ACTIVEEVENT:
		if (ev.active.state & SDL_APPINPUTFOCUS) {
			paused = ev.active.gain == 0;
			SDL_PauseAudio(paused ? 1 : 0);
		}
		break;
#endif
	case SDL_KEYUP:
		switch (ev.key.keysym.sym) {
		case SDLK_LEFT:
			_pi.dirMask &= ~PlayerInput::DIR_LEFT;
			break;
		case SDLK_RIGHT:
			_pi.dirMask &= ~PlayerInput::DIR_RIGHT;
			break;
		case SDLK_UP:
			_pi.dirMask &= ~PlayerInput::DIR_UP;
			break;
		case SDLK_DOWN:
			_pi.dirMask &= ~PlayerInput::DIR_DOWN;
			break;
		case SDLK_RETURN:
			_pi.enter = false;
			break;
		case SDLK_SPACE:
			_pi.space = false;
			break;
		case SDLK_RSHIFT:
		case SDLK_LSHIFT:
			_pi.shift = false;
			break;
		case SDLK_RCTRL:
		case SDLK_LCTRL:
			_pi.ctrl = false;
			break;
		case SDLK_TAB:
			_pi.tab = false;
			break;
		case SDLK_ESCAPE:
			_pi.escape = false;
			break;
		case SDLK_c: {
				// capture game screen buffer (no support for video YUYV buffer)
				char name[32];
				snprintf(name, sizeof(name), "screenshot-%03d.tga", _screenshot);
				saveTGA(name, (const uint8_t *)_gameBuffer, _screenW, _screenH);
				++_screenshot;
				debug(DBG_INFO, "Written '%s'", name);
			}
			break;
		case SDLK_f:
			_pi.fastMode = !_pi.fastMode;
			break;
		case SDLK_s:
			_pi.save = true;
			break;
		case SDLK_l:
			_pi.load = true;
			break;
		case SDLK_w:
			setFullscreen(!_fullScreenDisplay);
			break;
		case SDLK_KP_PLUS:
		case SDLK_PAGEUP:
			_pi.stateSlot = 1;
			break;
		case SDLK_KP_MINUS:
		case SDLK_PAGEDOWN:
			_pi.stateSlot = -1;
			break;
		default:
			break;
		}
		break;
	case SDL_KEYDOWN:
	switch (ev.key.keysym.sym) {
		case SDLK_LEFT:
			_pi.dirMask |= PlayerInput::DIR_LEFT;
			break;
		case SDLK_RIGHT:
			_pi.dirMask |= PlayerInput::DIR_RIGHT;
			break;
		case SDLK_UP:
			_pi.dirMask |= PlayerInput::DIR_UP;
			break;
		case SDLK_DOWN:
			_pi.dirMask |= PlayerInput::DIR_DOWN;
			break;
		case SDLK_RETURN:
			_pi.enter = true;
			break;
		case SDLK_SPACE:
			_pi.space = true;
			break;
		case SDLK_RSHIFT:
		case SDLK_LSHIFT:
			_pi.shift = true;
			break;
		case SDLK_RCTRL:
		case SDLK_LCTRL:
			_pi.ctrl = true;
			break;
		case SDLK_TAB:
			_pi.tab = true;
			break;
		case SDLK_ESCAPE:
			_pi.escape = true;
			break;
		default:
			break;
		}
		break;
	case SDL_MOUSEBUTTONDOWN:
		if (ev.button.button == SDL_BUTTON_LEFT) {
			_pi.leftMouseButton = true;
		} else if (ev.button.button == SDL_BUTTON_RIGHT) {
			_pi.rightMouseButton = true;
		}
		updateMousePosition(ev.button.x, ev.button.y);
		break;
	case SDL_MOUSEBUTTONUP:
		if (ev.button.button == SDL_BUTTON_LEFT) {
			_pi.leftMouseButton = false;
		} else if (ev.button.button == SDL_BUTTON_RIGHT) {
			_pi.rightMouseButton = false;
		}
		updateMousePosition(ev.button.x, ev.button.y);
		break;
	case SDL_MOUSEMOTION:
		updateMousePosition(ev.motion.x, ev.motion.y);
		break;
#endif
	default:
		break;
	}
}

void SystemStub_SDL::sleep(int duration) {
	SDL_Delay(duration);
}

uint32_t SystemStub_SDL::getTimeStamp() {
	return SDL_GetTicks();
}

void SystemStub_SDL::lockAudio() {
	SDL_LockAudio();
}

void SystemStub_SDL::unlockAudio() {
	SDL_UnlockAudio();
}

void SystemStub_SDL::startAudio(AudioCallback callback, void *param) {
	SDL_AudioSpec desired, obtained;
	memset(&desired, 0, sizeof(desired));
	desired.freq = kSoundSampleRate;
	desired.format = AUDIO_S16SYS;
	desired.channels = 2;
	desired.samples = kSoundSampleSize;
	desired.callback = callback;
	desired.userdata = param;
	if (SDL_OpenAudio(&desired, &obtained) == 0) {
		_soundSampleRate = obtained.freq;
		SDL_PauseAudio(0);
	} else {
		error("SystemStub_SDL::startAudio() Unable to open sound device");
	}
}

void SystemStub_SDL::stopAudio() {
	SDL_CloseAudio();
}

int SystemStub_SDL::getOutputSampleRate() {
	return _soundSampleRate;
}

void SystemStub_SDL::setFullscreen(bool fullscreen) {
#if SDL_VERSION_ATLEAST(2, 0, 0)
	if (_fullScreenDisplay != fullscreen) {
		SDL_SetWindowFullscreen(_window, fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
		_fullScreenDisplay = fullscreen;
	}
#else
	_screen = SDL_SetVideoMode(_screenW, _screenH, kVideoSurfaceDepth, fullscreen ? SDL_FULLSCREEN : 0);
	if (_screen) {
		_fmt = _screen->format;
	}
#endif
}
