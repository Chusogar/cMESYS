// mpegaudiod.h - MPEG-1/2 audio layer I/II decoder (Sega Digital Sound
// Board): C port of dsp-cpp's sound/mpeg_audio, itself MAME's mpeg_audio
// (BSD-3-Clause, Olivier Galibert).
#ifndef MPEGAUDIOD_H
#define MPEGAUDIOD_H
#include <stdint.h>
#include <stdbool.h>
#include <setjmp.h>

enum { MPA_L1 = 1, MPA_L2 = 2, MPA_L2_5 = 4, MPA_L3 = 8, MPA_AMM = 16 };

typedef struct mpeg_audio {
	const uint8_t *m_base;
	int m_accepted, m_position_align;
	int m_sampling_rate, m_last_frame_number;
	int m_param_index, m_cbr_param_index;
	int m_channel_count, m_total_bands, m_joint_bands;
	int m_band_param[2][32];
	int m_scfsi[2][32];
	int m_scf[2][3][32];
	double m_amp_values[2][3][32];
	double m_bdata[2][3][32];
	double m_subbuffer[2][32];
	double m_audio_buffer[2][32*32];
	int m_audio_buffer_pos[2];
	double m_cos_cache[32][32];
	int m_current_pos, m_current_limit;
	int (*do_gb)(const unsigned char *data, int *pos, int count);
	jmp_buf jb;
} mpeg_audio;

void mpa_init(mpeg_audio *s, const void *base, unsigned int accepted, bool lsb_first, int position_align);
// pos / limit in bits; one frame of output (interleaved); false: no complete frame.
bool mpa_decode_buffer(mpeg_audio *s, int *pos, int limit, short *output, int *output_samples, int *sample_rate,
                       int *channels, int atbl);
void mpa_clear(mpeg_audio *s);

#endif
