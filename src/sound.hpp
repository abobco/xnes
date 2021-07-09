#pragma once
#include "xn_openal.hpp"
#include <atomic>
#include <cstdint>
#include <functional>
#include <list>
#include <queue>
#include <thread>
#include <vector>

#ifdef __EMSCRIPTEN__
// emscripten openal seems to only support 8 bit audio format...
#define AUDIO_FORMAT AL_FORMAT_MONO8
typedef uint8_t SoundPacket;
static float fMaxSample = (float)255;
float SoundClamp(float fSample, float fMax) {
  fSample += fMax;
  fSample *= 0.5;
  return fmax(fmin(fSample, fMax), 0.0);
};
#else
// use 16 bit signed integer audio format
#define AUDIO_FORMAT AL_FORMAT_MONO16
typedef int16_t SoundPacket;
static SoundPacket nMaxSample =
    (SoundPacket)pow(2, (sizeof(SoundPacket) * 8) - 1) - 1;
static float fMaxSample = (float)nMaxSample;
float SoundClamp(float fSample, float fMax) {
  fSample += fMax;
  fSample *= 0.5;
  return fmax(fmin(fSample, fMax), 0.0);
}
#endif

struct SoundInfo {
  bool muted = false;
  uint32_t sampleRate;
  uint32_t channelCount;
  uint32_t blockCount;
  uint32_t blockSamples;

  std::thread audioThreadHandle;
  std::atomic<bool> audioThreadActive;
  std::atomic<float> systemTime;
  std::function<float(void)> mixerCallback;
  // std::function<float(int, float, float)> mixerCallback;

  // OpenAL
  std::queue<ALuint> availableBufferQueue;
  std::vector<ALuint> audioBuffers;
  std::vector<SoundPacket> blockMemory;

  int init(bool createThread = false, uint32_t sample_rate = 44100,
           uint32_t channel_count = 1, uint32_t block_count = 8,
           uint32_t block_samples = 512) {
    systemTime = 0;
    audioThreadActive = false;
    sampleRate = sample_rate;
    channelCount = channel_count;
    blockCount = block_count;
    blockSamples = block_samples;

    initOpenAL();
    ALuint &source = g_OpenAL_Instance.source;

    audioBuffers.resize(blockCount);
    al_call(alGenBuffers, blockCount, audioBuffers.data());
    al_call(alGenSources, 1, &source);
    for (auto i = 0; i < blockCount; i++)
      availableBufferQueue.push(audioBuffers[i]);

    blockMemory.resize(blockSamples);

    audioThreadActive = true;
    if (createThread) {
      audioThreadHandle = std::thread(&SoundInfo::audioThread, this);
    }
    return true;
  }

  float getSample() {
    float mix_sample = 0;

    if (mixerCallback != nullptr)
      mix_sample += mixerCallback();

    return mix_sample;
  }

  static void audioThread(SoundInfo *inst) {
    std::vector<ALuint> processed_buffers;
    while (inst->audioThreadActive) {
      inst->step(processed_buffers);
    }
  }

  ALint unqueueBuffers(std::vector<ALuint> &processed_buffers) {
    ALuint &source = g_OpenAL_Instance.source;
    ALint source_state, processed_buffer_count;
    alGetSourcei(source, AL_SOURCE_STATE, &source_state);
    alGetSourcei(source, AL_BUFFERS_PROCESSED, &processed_buffer_count);

    // unqueue processed buffers
    processed_buffers.resize(processed_buffer_count);
    alSourceUnqueueBuffers(source, processed_buffer_count,
                           processed_buffers.data());
    for (ALint nBuf : processed_buffers)
      availableBufferQueue.push(nBuf);

    return source_state;
  }

  void queueBuffers(ALint source_state) {
    // copy samples to buffer
    alBufferData(availableBufferQueue.front(), AUDIO_FORMAT, blockMemory.data(),
                 blockSamples * sizeof(SoundPacket), sampleRate);
    // queue buffer
    alSourceQueueBuffers(g_OpenAL_Instance.source, 1,
                         &availableBufferQueue.front());
    availableBufferQueue.pop();

    if (source_state != AL_PLAYING)
      alSourcePlay(g_OpenAL_Instance.source);
  }

  // generate & upload audio samples with mixer callback
  bool step(std::vector<ALuint> &processed_buffers) {
    ALint source_state = unqueueBuffers(processed_buffers);
    if (availableBufferQueue.empty())
      return false;

    // get new samples
    for (unsigned int n = 0; n < blockSamples; n += channelCount) {
      for (unsigned int c = 0; c < channelCount; c++) {
        blockMemory[n + c] =
            (SoundPacket)(SoundClamp(getSample(), 1.0) * fMaxSample);
      }
    }

    if (!muted)
      queueBuffers(source_state);

    return true;
  }

  // upload audio samples from queue
  bool step(std::vector<ALuint> &processed_buffers,
            std::queue<float> &audio_queue) {
    if (audio_queue.size() < blockSamples)
      return false;

    ALint source_state = unqueueBuffers(processed_buffers);
    if (availableBufferQueue.empty())
      return false;

    // get new samples
    for (unsigned int n = 0; n < blockSamples; n += channelCount) {
      for (unsigned int c = 0; c < channelCount; c++) {
        blockMemory[n + c] =
            (SoundPacket)(SoundClamp(audio_queue.front(), 1.0) * fMaxSample);
        audio_queue.pop();
      }
    }

    if (!muted)
      queueBuffers(source_state);

    return true;
  }

  int destroy() {
    audioThreadActive = false;
    if (audioThreadHandle.joinable())
      audioThreadHandle.join();

    alDeleteBuffers(blockCount, audioBuffers.data());
    audioBuffers.resize(0);
    alDeleteSources(1, &g_OpenAL_Instance.source);

    alcMakeContextCurrent(NULL);
    alcDestroyContext(g_OpenAL_Instance.context);
    alcCloseDevice(g_OpenAL_Instance.device);
    return false;
  }

  void setMixerCallback(std::function<float(void)> func) {
    mixerCallback = func;
  }
} Sound;