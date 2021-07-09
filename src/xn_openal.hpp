#pragma once
#ifdef __EMSCRIPTEN__
#include <AL/al.h>
#include <AL/alc.h>
#else
#include "al.h"
#include "alc.h"
// #include <AL/alut.h>
#endif
// #include <bit>
#include <fstream>
#include <iostream>
#include <string>

struct OpenAL_Instance {
  ALCdevice *device;
  ALCcontext *context;
  ALuint source;
} g_OpenAL_Instance;

#define al_call(function, ...)                                                 \
  al_call_impl(__FILE__, __LINE__, function, __VA_ARGS__)

int al_check_error(const std::string &filename, const std::uint_fast32_t line) {
  ALenum error = alGetError();
  switch (error) {
  case AL_NO_ERROR:
    return 0;
  case AL_INVALID_NAME:
    std::cerr << "[OPENAL] " << filename << ":" << line
              << "\n\tError: AL_INVALID_NAME\n";
    return -1;
  case AL_INVALID_ENUM:
    std::cerr << "[OPENAL] " << filename << ":" << line
              << "\n\tError: AL_INVALID_ENUM\n";
    return -1;
  case AL_INVALID_VALUE:
    std::cerr << "[OPENAL] " << filename << ":" << line
              << "\n\tError: AL_INVALID_VALUE\n";
    return -1;
  case AL_INVALID_OPERATION:
    std::cerr << "[OPENAL] " << filename << ":" << line
              << "\n\tError: AL_INVALID_OPERATION\n";
    return -1;
  case AL_OUT_OF_MEMORY:
    std::cerr << "[OPENAL] " << filename << ":" << line
              << "\n\tError: AL_OUT_OF_MEMORY\n";
    return -1;
  default:
    std::cerr << "[OPENAL] Error:\n";
    return -1;
  }
}

template <typename alFunction, typename... Params>
auto al_call_impl(const char *filename, const std::uint_fast32_t line,
                  alFunction function, Params... params) ->
    typename std::enable_if_t<
        !std::is_same_v<void, decltype(function(params...))>,
        decltype(function(params...))> {
  auto ret = function(std::forward<Params>(params)...);
  al_check_error(filename, line);
  return ret;
}

template <typename alFunction, typename... Params>
auto al_call_impl(const char *filename, const std::uint_fast32_t line,
                  alFunction function, Params... params) ->
    typename std::enable_if_t<
        std::is_same_v<void, decltype(function(params...))>, bool> {
  function(std::forward<Params>(params)...);
  return al_check_error(filename, line);
}

#define alc_call(function, device, ...)                                        \
  alc_call_impl(__FILE__, __LINE__, function, device, __VA_ARGS__)

int alc_check_error(const std::string &filename, const std::uint_fast32_t line,
                    ALCdevice *device) {
  ALCenum error = alcGetError(device);
  switch (error) {
  case ALC_NO_ERROR:
    return 0;
  case ALC_INVALID_VALUE:
    std::cerr << "[OPENAL]" << filename << ":" << line
              << "\n\tError: ALC_INVALID_VALUE\n";
    return -1;
  case ALC_INVALID_CONTEXT:
    std::cerr << "[OPENAL]" << filename << ":" << line
              << "\n\tError: ALC_INVALID_CONTEXT\n";
    return -1;
  case ALC_INVALID_ENUM:
    std::cerr << "[OPENAL]" << filename << ":" << line
              << "\n\tError: ALC_INVALID_ENUM:\n";
    return -1;
  case ALC_OUT_OF_MEMORY:
    std::cerr << "[OPENAL]" << filename << ":" << line
              << "\n\tError: ALC_OUT_OF_MEMORY\n";
  default:
    std::cerr << "[OPENAL]" << filename << ":" << line
              << "\n\tError: Unknown ALC error\n";
    return -1;
  }
}

template <typename alcFunction, typename... Params>
auto alc_call_impl(const std::string &filename, const std::uint_fast32_t line,
                   alcFunction function, ALCdevice *device, Params... params) ->
    typename std::enable_if_t<
        std::is_same_v<void, decltype(function(params...))>, bool> {
  function(std::forward<Params>(params)...);
  return alc_check_error(filename, line, device);
}

template <typename alcFunction, typename ReturnType, typename... Params>
auto alc_call_impl(const std::string &filename, const std::uint_fast32_t line,
                   alcFunction function, ReturnType &returnValue,
                   ALCdevice *device, Params... params) ->
    typename std::enable_if_t<
        !std::is_same_v<void, decltype(function(params...))>, bool> {
  returnValue = function(std::forward<Params>(params)...);
  return alc_check_error(filename, line, device);
}

int initOpenAL() {
  g_OpenAL_Instance.device = alcOpenDevice(NULL);
  if (!g_OpenAL_Instance.device) {
    std::cerr << "ERROR: Failed to open audio device!\n";
    exit(-1);
  }

  ALCcontext *openALContext;
  if (alc_call(alcCreateContext, openALContext, g_OpenAL_Instance.device,
               g_OpenAL_Instance.device, nullptr) < 0 ||
      !openALContext) {
    std::cerr << "ERROR: Could not create audio context\n";
    exit(-1);
  }

  ALCboolean contextMadeCurrent = false;
  if (alc_call(alcMakeContextCurrent, contextMadeCurrent,
               g_OpenAL_Instance.device, openALContext) < 0 ||
      contextMadeCurrent != ALC_TRUE) {
    std::cerr << "ERROR: Could not make audio context current" << std::endl;
    // exit(-1);
  }
  return 0;
}
