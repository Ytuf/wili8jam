#include "audio.h"
#include "p8_sfx.h"
#include "fw2.h"
#include "lauxlib.h"
#include "pico/time.h"
#include <string.h>
#include <math.h>

#define SAMPLE_RATE 16000
#define BUFFER_FRAMES 256
#define SILENCE_BLOCKS 64

typedef struct {
    uint32_t phase, phase_inc;
    int32_t remaining;
    int16_t volume;
    uint8_t waveform;
    uint16_t noise_lfsr;
    bool active;
} synth_channel_t;

static synth_channel_t channels[AUDIO_NUM_CHANNELS];
static int16_t sine_table[256];
static uint32_t audio_buf[BUFFER_FRAMES] __attribute__((aligned(BUFFER_FRAMES * sizeof(uint32_t))));
static bool initialized, paused, output_active;
static unsigned silent_blocks;
static uint64_t next_fill_us;

static int16_t synth_sample(synth_channel_t *ch) {
    if (!ch->active) return 0;
    uint8_t idx = ch->phase >> 24; int16_t sample = 0;
    switch (ch->waveform) {
    case WAVE_SINE: sample = sine_table[idx]; break;
    case WAVE_SQUARE: sample = (ch->phase & 0x80000000u) ? 32767 : -32767; break;
    case WAVE_SAW: sample = (int16_t)((ch->phase >> 16) - 32768); break;
    case WAVE_TRIANGLE:
        sample = ch->phase < 0x80000000u
            ? (int16_t)(((ch->phase >> 15) & 0xffff) - 32768)
            : (int16_t)(32767 - (((ch->phase - 0x80000000u) >> 15) & 0xffff));
        break;
    case WAVE_NOISE: {
        uint16_t bit = ((ch->noise_lfsr >> 0) ^ (ch->noise_lfsr >> 1) ^
                        (ch->noise_lfsr >> 5) ^ (ch->noise_lfsr >> 6)) & 1;
        ch->noise_lfsr = (ch->noise_lfsr >> 1) | (bit << 15);
        sample = (int16_t)(ch->noise_lfsr - 32768);
        break;
    }}
    ch->phase += ch->phase_inc;
    if (ch->remaining > 0 && --ch->remaining == 0) ch->active = false;
    return sample;
}
static bool fill_buffer(void) {
    bool nonzero = false;
    for (unsigned i = 0; i < BUFFER_FRAMES; ++i) {
        int32_t mix = paused ? 0 : p8_sfx_mix_sample();
        for (int c = 0; !paused && c < AUDIO_NUM_CHANNELS; ++c) mix += synth_sample(&channels[c]);
        if (mix > 32767) mix = 32767; if (mix < -32768) mix = -32768;
        int16_t s = (int16_t)mix; if (s) nonzero = true;
        audio_buf[i] = ((uint32_t)(uint16_t)s << 16) | (uint16_t)s;
    }
    return nonzero;
}
static void output_enable(void) {
    if (output_active) return;
    codec_nau88c10_dac_mute(false);
    codec_nau88c10_set_output(CODEC_OUT_SPEAKER);
    audio_i2s_duplex_play_loop(audio_buf, BUFFER_FRAMES);
    output_active = true;
}
static void output_disable(void) {
    if (!output_active) return;
    audio_i2s_duplex_play_stop();
    codec_nau88c10_speaker_low_power();
    output_active = false;
}
bool audio_init(void) {
    const uint32_t audio_zone = picpwr_zone_bit(PICPWR_ZONE_AUDIO);
    uint32_t rails = 0;
    picpwr_keep_awake(audio_zone);
    uint64_t deadline = time_us_64() + 2500000;
    while (time_us_64() < deadline) {
        fw2_app_recovery_task(); picpwr_task();
        if (picpwr_rails(&rails) && (rails & audio_zone)) break;
        fw2_app_recovery_sleep_ms(20);
    }
    if (!picpwr_rails(&rails) || (rails & audio_zone) == 0) return false;
    codec_nau88c10_init();
    audio_i2s_duplex_init(SAMPLE_RATE);
    codec_nau88c10_speaker_low_power();
    for (int i=0;i<256;i++) sine_table[i]=(int16_t)(sinf((float)i*6.283185307f/256.0f)*32767.0f);
    memset(channels,0,sizeof channels);
    for (int i=0;i<AUDIO_NUM_CHANNELS;i++){channels[i].noise_lfsr=0xace1;channels[i].volume=255;}
    initialized = true; next_fill_us = time_us_64();
    return true;
}
void audio_task(void) {
    if (!initialized) return;
    picpwr_task();
    uint64_t now=time_us_64(); if (now < next_fill_us) return;
    next_fill_us = now + 16000;
    bool nonzero=fill_buffer();
    if (nonzero) { silent_blocks=0; output_enable(); }
    else if (output_active && ++silent_blocks >= SILENCE_BLOCKS) output_disable();
}
void audio_pause(void){paused=true;output_disable();}
void audio_resume(void){paused=false;next_fill_us=time_us_64();}
void audio_tone(int channel,float freq,int duration_ms,int waveform){
    if(channel<0||channel>=AUDIO_NUM_CHANNELS||!initialized)return;
    if(waveform<0||waveform>WAVE_NOISE)waveform=WAVE_SQUARE;
    synth_channel_t *ch=&channels[channel];ch->phase=0;
    ch->phase_inc=(uint32_t)(freq*4294967296.0f/SAMPLE_RATE);ch->waveform=(uint8_t)waveform;
    ch->remaining=duration_ms>0?(duration_ms*SAMPLE_RATE)/1000:-1;ch->noise_lfsr=0xace1;ch->active=true;
}
void audio_stop(int channel){
    if(channel<0){for(int i=0;i<AUDIO_NUM_CHANNELS;i++)channels[i].active=false;}
    else if(channel<AUDIO_NUM_CHANNELS)channels[channel].active=false;
}
void audio_volume(int level){
    /* The BSP intentionally exposes a fixed, hardware-safe speaker ceiling.
       Zero powers down immediately; nonzero levels use that capped setting. */
    if(level<=0){audio_stop(-1);output_disable();}
}
static int l_tone(lua_State*L){audio_tone((int)luaL_optnumber(L,4,0),(float)luaL_checknumber(L,1),(int)luaL_optnumber(L,2,0),(int)luaL_optnumber(L,3,WAVE_SQUARE));return 0;}
static int l_stop(lua_State*L){audio_stop((int)luaL_optnumber(L,1,-1));return 0;}
static int l_volume(lua_State*L){audio_volume((int)luaL_checknumber(L,1));return 0;}
static const luaL_Reg lib[]={{"tone",l_tone},{"stop",l_stop},{"volume",l_volume},{NULL,NULL}};
int luaopen_audio(lua_State*L){luaL_newlib(L,lib);return 1;}
