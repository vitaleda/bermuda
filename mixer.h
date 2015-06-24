/*
 * Bermuda Syndrome engine rewrite
 * Copyright (C) 2007-2011 Gregory Montoir
 */

#ifndef MIXER_H__
#define MIXER_H__

#include "intern.h"

struct File;
struct MixerImpl;
struct SystemStub;

struct MixerChannel {
	virtual ~MixerChannel() {}
	virtual bool load(File *f, int mixerSampleRate) = 0;
	virtual int read(int16_t *dst, int samples) = 0;
	int id;
};

struct Mixer {
	enum {
		kMaxChannels = 4,
		kDefaultSoundId = -1
	};

	Mixer(SystemStub *stub);
	~Mixer();

	void open();
	void close();

	void startSound(File *f, int *id, MixerChannel *mc);
	void playSound(File *f, int *id);
	void playMusic(File *f, int *id);
	bool isSoundPlaying(int id);
	void stopSound(int id);
	void stopAll();

	void mix(int16_t *buf, int len);
	static void mixCallback(void *param, uint8_t *buf, int len);

	int generateSoundId(int channel);
	int getChannelFromSoundId(int id);
	bool bindChannel(MixerChannel *mc, int *id);
	void unbindChannel(int channel);

	SystemStub *_stub;
	int _channelIdSeed;
	bool _open;
	MixerChannel *_channels[kMaxChannels];
	MixerImpl *_impl;
};

#endif // MIXER_H__
