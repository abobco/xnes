#pragma once
#ifndef _USE_MATH_DEFINES
#define _USE_MATH_DEFINES
#endif
#include <array>
#include <cmath>
#include <cstdint>
#include <functional>
#include <vector>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

typedef double AudioFloat;

namespace PulseLength {
static const std::array<uint8_t, 32> table = {
    10, 254, 20, 2,  40, 4,  80, 6,  160, 8,  60, 10, 14, 12, 26, 14,
    12, 16,  24, 18, 48, 20, 96, 22, 192, 24, 72, 26, 16, 28, 32, 30};
}

static const std::vector<uint8_t> triangleSequence = {
    15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5,  4,  3,  2,  1,  0,
    0,  1,  2,  3,  4,  5,  6, 7, 8, 9, 10, 11, 12, 13, 14, 15};

const static AudioFloat PI2 = M_PI * M_PI;
struct APU {
  // https://en.wikipedia.org/wiki/Bhaskara_I%27s_sine_approximation_formula
  static float bhaskara_sin(float t) {
    t = fmod(t, 2 * M_PI);
    bool flip = t > M_PI;
    t = fmod(t, M_PI);
    return 16 * t * (M_PI - t) / (5 * PI2 - 4 * t * (M_PI - t)) *
           (flip ? -1 : 1);
  }

#ifdef USE_EXACT_SIN
  static float fast_sin(float t) { return sin(t); }
#else
  static float fast_sin(float t) {
    float j = t * 0.15915;
    j -= (int)j;
    return 20.785f * j * (j - 0.5f) * (j - 1.0f);
  }
#endif

  struct Pulse {
    AudioFloat frequency = 0;
    AudioFloat dutycycle = 0;
    AudioFloat amplitude = 1;
    static const int harmonics = 3;
    AudioFloat prev_sample = 0;

    AudioFloat sample(AudioFloat t) {
      AudioFloat a = 0;
      AudioFloat b = 0;
      AudioFloat p = 2 * M_PI * dutycycle;

      for (float j = 1; j < harmonics; j++) {
        AudioFloat tc = j * frequency * 2.0 * M_PI * t;
        a += -fast_sin(tc) / j;
        b += -fast_sin(tc - p * j) / j;
      }
      AudioFloat s = (2.0 * amplitude / M_PI) * (a - b);
      AudioFloat out = s + prev_sample;
      prev_sample = s;
      return out;
    }
  };

  struct TriangleWave {
    AudioFloat frequency = 1;
    int harmonics = 10;

    AudioFloat sample(AudioFloat t) {
      AudioFloat s = 0;
      for (float j = 0; j < harmonics; j++) {
        int sign_fac = pow(-1, j);
        float f2 = powf(2.0f, frequency - 1);
        float a = bhaskara_sin(f2 * (2 * j + 1) * t);
        float b = (2 * j + 1) * (2 * j + 1);
        s += (sign_fac * a / b);
      }

      return s * 8 / (PI2);
    }
  };

  struct Sequencer {
    uint32_t sequence = 0;
    uint32_t nextSequence = 0;
    uint16_t timer = 0;
    uint16_t reload = 0;
    uint8_t output = 0;

    uint8_t clock(bool enable, std::function<void(uint32_t &s)> mix_function) {
      if (enable) {
        timer--;
        if (timer == 0xFFFF) {
          timer = reload;
          mix_function(sequence);
          output = sequence & 0x00000001;
        }
      }
      return output;
    }
  };

  struct PulseCounter {
    uint8_t counter = 0;
    uint8_t clock(bool enable, bool halt) {
      if (!enable)
        counter = 0;
      else if (counter > 0 && !halt)
        counter--;
      return counter;
    }
  };

  struct PulseEnvelope {
    static const uint8_t DECAY_BASE = 15;
    bool start = false;
    bool disable = false;
    uint16_t dividerCount = 0;
    uint16_t volume = 0;
    uint16_t output = 0;
    uint16_t decayCount = 0;

    void clock(bool loop) {
      if (!start) {
        if (dividerCount == 0) {
          dividerCount = volume;
          if (decayCount == 0) {
            if (loop)
              decayCount = DECAY_BASE;
          } else
            decayCount--;
        } else
          dividerCount--;
      } else {
        start = false;
        decayCount = DECAY_BASE;
        dividerCount = volume;
      }

      if (disable)
        output = volume;
      else
        output = decayCount;
    }
  };

  struct Sweeper {
    bool enabled = false;
    bool down = false;
    bool reload = false;
    bool muted = false;
    uint8_t shift = 0;
    uint8_t timer = 0;
    uint8_t period = 0;
    uint16_t change = 0;

    void track(uint16_t &target) {
      if (enabled) {
        change = target >> shift;
        muted = (target < 8) || (target > 0x7FF);
      }
    }

    bool clock(uint16_t &target, bool channel) {
      bool changed = false;
      if (timer == 0 && enabled && shift > 0 && !muted && target >= 8 &&
          change < 0x07FF) {
        if (down)
          target -= (change + channel);
        else
          target += change;
        changed = true;
      }

      if (enabled) {
        if (timer == 0 || reload) {
          timer = period;
          reload = false;
        } else
          timer--;

        muted = (target < 8) || (target > 0x7FF);
      }

      return changed;
    }
  };

  struct PulseChannel {
    bool enable = false;
    bool halt = false;
    float volume = 0.1;
    AudioFloat sample = 0;
    AudioFloat output = 0;
    Sequencer sequencer;
    Pulse pulse;
    PulseEnvelope envelope;
    PulseCounter counter;
    Sweeper sweeper;
    void write(uint16_t rel_addr, uint8_t data) {
      struct NesPulseMapping {
        uint32_t seq;
        AudioFloat duty;
      };
      static const std::array<NesPulseMapping, 4> pulse_mappings = {{
          {0b01000000, 0.125},
          {0b01100000, 0.25},
          {0b01111000, 0.5},
          {0b10011111, 0.75},
      }};
      const uint8_t pulse_mask = (data & 0xC0) >> 6;
      switch (rel_addr) {
      case 0:
        sequencer.nextSequence = pulse_mappings[pulse_mask].seq;
        pulse.dutycycle = pulse_mappings[pulse_mask].duty;
        sequencer.sequence = sequencer.nextSequence;
        halt = (data & 0x20);
        envelope.volume = data & 0x0F;
        envelope.disable = data & 0x10;
        break;
      case 1:
        sweeper.enabled = data & 0x80;
        sweeper.period = (data & 0x70) >> 4;
        sweeper.down = data & 0x08;
        sweeper.shift = data & 0x07;
        sweeper.reload = true;
        break;
      case 2:
        sequencer.reload = (sequencer.reload & 0xFF00) | data;
        break;
      case 3:
        sequencer.reload =
            (uint16_t)(data & 0x07) << 8 | (sequencer.reload & 0x00FF);
        sequencer.timer = sequencer.reload;
        sequencer.sequence = sequencer.nextSequence;
        counter.counter = PulseLength::table[(data & 0xF8) >> 3];
        envelope.start = true;
        break;
      default:
        break;
      }
    }

    void update(AudioFloat systemTime) {
      static const AudioFloat channel_multiplier = 16.0;
      sequencer.clock(enable, [](uint32_t &s) {
        // rotate right 1 bit
        s = ((s & 1) << 7) | ((s & 0x00FE) >> 1);
      });
      pulse.frequency =
          1789773.0 / (channel_multiplier * (AudioFloat)(sequencer.reload + 1));
      pulse.amplitude = (AudioFloat)(envelope.output - 1) / channel_multiplier;
      sample = pulse.sample(systemTime);

      if (counter.counter > 0 && sequencer.timer >= 8 && !sweeper.muted &&
          envelope.output > 2)
        output += (sample - output);
      else
        output = 0;

      if (!enable)
        output = 0;
    }
  };

  struct NoiseChannel {
    bool enable = false;
    bool halt = false;
    float volume = 0.2;
    PulseEnvelope envelope;
    PulseCounter counter;
    Sequencer sequencer;
    AudioFloat sample;
    AudioFloat output;
  } noiseChannel;

  const AudioFloat CLOCK_TIMESTEP = (0.3333333333 / (1789773));
  AudioFloat systemTime = 0;
  uint32_t frameClockCount;
  uint32_t clockCount;
  bool useRaw = false;
  bool enabled = true;

  PulseChannel pulseChannel_1;
  PulseChannel pulseChannel_2;

  APU() { noiseChannel.sequencer.sequence = 0xDBDB; }

  void cpuWrite(uint16_t addr, uint8_t data) {

    if (addr >= 0x4000 && addr <= 0x4003) {
      pulseChannel_1.write(addr - 0x4000, data);
    } else if (addr >= 0x4004 && addr <= 0x4007) {
      pulseChannel_2.write(addr - 0x4004, data);
    } else if (addr == 0x400C) {
      noiseChannel.envelope.volume = (data & 0x0F);
      noiseChannel.envelope.disable = (data & 0x10);
      noiseChannel.halt = (data & 0x20);
    } else if (addr == 0x400E) {
      std::array<uint16_t, 16> noise_seq_vals = {0,   4,    8,    16,  32,  64,
                                                 96,  128,  160,  202, 254, 380,
                                                 508, 1016, 2034, 4068};
      noiseChannel.sequencer.reload = noise_seq_vals[data & 0x0F];
    } else if (addr == 0x4015) { // STATUS
      pulseChannel_1.enable = data & 0x01;
      pulseChannel_2.enable = data & 0x02;
      noiseChannel.enable = data & 0x04;
    } else if (addr == 0x400F) {
      pulseChannel_1.envelope.start = true;
      pulseChannel_2.envelope.start = true;
      noiseChannel.envelope.start = true;
      noiseChannel.counter.counter = PulseLength::table[(data & 0xF8) >> 3];
    }
  }

  uint8_t cpuRead(uint16_t addr) {
    uint8_t data = 0;
    if (addr == 0x4015) {
      data |= pulseChannel_1.counter.counter > 0 ? 1 : 0;
      data |= pulseChannel_2.counter.counter > 0 ? 2 : 0;
      data |= noiseChannel.counter.counter > 0 ? 4 : 0;
    }
    return data;
  }

  void clock() {
    if (enabled) {
      // use frame count to determine if a sequence needs updating
      bool quarter_frame_clock = false;
      bool half_frame_clock = false;
      systemTime += CLOCK_TIMESTEP;

      // if (clockCount % 3 == 0) {
      //   // triangleChannel.linearCounter.clock();
      //   triangleChannel.update(systemTime);
      // }

      if (clockCount % 6 == 0) {
        frameClockCount++;

        // 4 step sequence mode
        if (frameClockCount == 3729 || frameClockCount == 11186)
          quarter_frame_clock = true;
        if (frameClockCount == 7457) {
          quarter_frame_clock = true;
          half_frame_clock = true;
        }
        if (frameClockCount == 14916) {
          quarter_frame_clock = true;
          half_frame_clock = true;
          frameClockCount = 0;
        }

        // update audio devices

        // adjust volume envelope
        if (quarter_frame_clock) {
          pulseChannel_1.envelope.clock(pulseChannel_1.halt);
          pulseChannel_2.envelope.clock(pulseChannel_2.halt);
          noiseChannel.envelope.clock(noiseChannel.halt);
        }

        // adjust note length and frequency sweepers
        if (half_frame_clock) {
          pulseChannel_1.counter.clock(pulseChannel_1.enable,
                                       pulseChannel_1.halt);
          pulseChannel_2.counter.clock(pulseChannel_2.enable,
                                       pulseChannel_2.halt);
          noiseChannel.counter.clock(noiseChannel.enable, noiseChannel.halt);

          pulseChannel_1.sweeper.clock(pulseChannel_1.sequencer.reload, 0);
          pulseChannel_2.sweeper.clock(pulseChannel_2.sequencer.reload, 1);
        }

        // update pulse channel 1
        pulseChannel_1.update(systemTime);
        pulseChannel_2.update(systemTime);
        noiseChannel.sequencer.clock(noiseChannel.enable, [](uint32_t &s) {
          s = (((s & 0x0001) ^ ((s & 0x0002) >> 1)) << 14) |
              ((s & 0x7FFF) >> 1);
        });

        if (noiseChannel.counter.counter > 0 &&
            noiseChannel.sequencer.timer >= 8) {
          noiseChannel.output =
              (AudioFloat)noiseChannel.sequencer.output *
              ((AudioFloat)(noiseChannel.envelope.output - 1) / 16.0);
        }

        if (!noiseChannel.enable)
          noiseChannel.output = 0;
      }

      pulseChannel_1.sweeper.track(pulseChannel_1.sequencer.reload);
      pulseChannel_2.sweeper.track(pulseChannel_2.sequencer.reload);

      clockCount++;
    }
  }

  AudioFloat getSample() {
    // if (useRaw) {
    // return (pulseChannel_1.sample - 0.5);
    // }
    // return ((pulseChannel_1.sample) + (pulseChannel_2.sample)) * 0.5;
    // return (pulseChannel_2.sample);

    return (pulseChannel_1.output) * pulseChannel_1.volume +
           (pulseChannel_2.output) * pulseChannel_2.volume +
           (noiseChannel.output) * noiseChannel.volume;
  }
};