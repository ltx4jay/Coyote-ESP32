/** 
 *  Coyote waveform library
 *
 * WARNING: USE AT YOUR OWN RISK
 *
 * The code is provided as-is, with no warranties of any kind. Not suitable for any purpose.
 * Provided as an example and exercise in BLE development only.
 *
 * By default generates a high-frequency wave on port A, and the 'GrainTouch' wave on port B at a power level of 25.
 *
 * Some guardrails have been implemented to limit the maximum power that the Coyote can output, but these can easily be by-passed.
 * The Coyote e-stim power box can generate dangerous power levels under normal usage.
 * See https://www.reddit.com/r/estim/comments/uadthp/dg_lab_coyote_review_by_an_electronics_engineer/ for more details.
 */


#pragma once


//
// Coyote maximum power and power stepping configuration
//
static struct CFGval {
    uint32_t   step    :  8;
    uint32_t   maxPwr  : 11;
    uint32_t   rsvd    : 13; 
} gCoyoteCfg = {7, 2000, 0};


//
// Power and waveform encodings, transmitted LSB first 3 bytes only
//
struct PowerVal {
    // Maximum power level allowed to be specified
    static const uint32_t MAXPOWER = 100;

    uint32_t  B    : 11;
    uint32_t  A    : 11;
    uint32_t  rsvd : 10;

    // Power values as number of steps (i.e. what is displayed on the app)
    PowerVal(uint8_t a = 0, uint8_t b = 0)
    : B(((b < 100) ? b : MAXPOWER) * gCoyoteCfg.step)
    , A(((a < 100) ? a : MAXPOWER) * gCoyoteCfg.step)
    , rsvd(0)
    {}

    // Values are sent in little-endian order
    operator uint8_t*() const
      {
          return (uint8_t*) this;
      }
};


struct WaveVal {
    uint32_t   x    :  5;
    uint32_t   y    : 10;
    uint32_t   z    :  5;
    uint32_t   rsvd : 12; 

    WaveVal(uint8_t X, uint16_t Y, uint8_t Z)
    : x(X)
    , y(Y)
    , z(Z)
    , rsvd(0)
    {}

    // Construct a wave from observed transmitted values, in transmit order
    WaveVal(std::vector<uint8_t> bytes)
    {
        // Make sure there are 3 bytes
        while (bytes.size() < 3) bytes.push_back(0x00);

        auto b = (uint8_t*) this;
        for (unsigned int i = 0; i < 3; i++) b[i] = bytes[i];
    }

    // Values are sent in little-endian order
    operator uint8_t*() const
      {
          return (uint8_t*) this;
      }
};

typedef std::vector<WaveVal>  Waveform;



//
// Pre-defined DG Labs waveforms
//
namespace DGLABS {

const Waveform GrainTouch = {WaveVal(std::vector<uint8_t>({0xE1, 0x03, 0x00})),
                             WaveVal(std::vector<uint8_t>({0xE1, 0x03, 0x0A})),
                             WaveVal(std::vector<uint8_t>({0xA1, 0x04, 0x0A})),
                             WaveVal(std::vector<uint8_t>({0xC1, 0x05, 0x0A})),
                             WaveVal(std::vector<uint8_t>({0x01, 0x07, 0x00})),
                             WaveVal(std::vector<uint8_t>({0x21, 0x01, 0x0A})),
                             WaveVal(std::vector<uint8_t>({0x61, 0x01, 0x0A})),
                             WaveVal(std::vector<uint8_t>({0xA1, 0x01, 0x0A})),
                             WaveVal(std::vector<uint8_t>({0x01, 0x02, 0x00})),
                             WaveVal(std::vector<uint8_t>({0x01, 0x02, 0x0A})),
                             WaveVal(std::vector<uint8_t>({0x81, 0x02, 0x0A})),
                             WaveVal(std::vector<uint8_t>({0x21, 0x03, 0x0A}))};

//
// High-frequency waform that is modulated when using audio source
//
const Waveform AudioBase = {WaveVal(1, 9, 16)};

}


//
// Some other interesting waveforms. Contributions welcome.
//


namespace LTX4JAY {

const Waveform IntenseVibration = {WaveVal(1, 9, 22)};



const Waveform SlowWave = {WaveVal(1, 26, 8),
                           WaveVal(1, 26, 8),
                           WaveVal(1, 24, 10),
                           WaveVal(1, 22, 12),
                           WaveVal(1, 20, 14),
                           WaveVal(1, 18, 16),
                           WaveVal(1, 16, 18),
                           WaveVal(1, 16, 22),
                           WaveVal(1, 16, 24),
                           WaveVal(1, 12, 24),
                           WaveVal(1, 12, 24),
                           WaveVal(1, 16, 24),
                           WaveVal(1, 16, 22),
                           WaveVal(1, 16, 18),
                           WaveVal(1, 18, 16),
                           WaveVal(1, 20, 14),
                           WaveVal(1, 22, 12),
                           WaveVal(1, 24, 10)};


const Waveform MediumWave = {WaveVal(1, 9, 4),
                             WaveVal(1, 9, 4),
                             WaveVal(1, 9, 6),
                             WaveVal(1, 9, 10),
                             WaveVal(1, 9, 12),
                             WaveVal(1, 9, 17),
                             WaveVal(1, 9, 20),
                             WaveVal(1, 9, 20),
                             WaveVal(1, 9, 20),
                             WaveVal(1, 9, 20),
                             WaveVal(1, 9, 20),
                             WaveVal(1, 9, 17),
                             WaveVal(1, 9, 12),
                             WaveVal(1, 9, 10),
                             WaveVal(1, 9, 6)};

};
