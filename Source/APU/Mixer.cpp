/*
** FamiTracker - NES/Famicom sound tracker
** Copyright (C) 2005-2014  Jonathan Liss
**
** This program is free software; you can redistribute it and/or modify
** it under the terms of the GNU General Public License as published by
** the Free Software Foundation; either version 2 of the License, or
** (at your option) any later version.
**
** This program is distributed in the hope that it will be useful, 
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU 
** Library General Public License for more details.  To obtain a 
** copy of the GNU Library General Public License, write to the Free 
** Software Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
**
** Any permitted reproduction of these routines, in whole or in part,
** must bear this legend.
*/

/*

 This will mix and synthesize the APU audio using blargg's blip-buffer

 Mixing of internal audio relies on Blargg's findings

 Mixing of external channles are based on my own research:

 VRC6 (Madara): 
	Pulse channels has the same amplitude as internal-
    pulse channels on equal volume levels.

 FDS: 
	Square wave @ v = $1F: 2.4V
	  			  v = $0F: 1.25V
	(internal square wave: 1.0V)

 MMC5 (just breed): 
	2A03 square @ v = $0F: 760mV (the cart attenuates internal channels a little)
	MMC5 square @ v = $0F: 900mV

 VRC7:
	2A03 Square  @ v = $0F: 300mV (the cart attenuates internal channels a lot)
	VRC7 Patch 5 @ v = $0F: 900mV
	Did some more tests and found patch 14 @ v=15 to be 13.77dB stronger than a 50% square @ v=15

 ---

 N163 & 5B are still unknown

*/

#include "../stdafx.h"
#include <algorithm>		// // //
#include <memory>
#include <cmath>
#include "Mixer.h"
#include "APU.h"
#include "SN76489_new.h"		// // //

//#define LINEAR_MIXING

static const double AMP_2A03 = 400.0;

static const float LEVEL_FALL_OFF_RATE	= 0.6f;
static const int   LEVEL_FALL_OFF_DELAY = 3;

CMixer::CMixer()
{
	memset(m_iChannelsLeft, 0, sizeof(int32) * CHANNELS);		// // //
	memset(m_iChannelsRight, 0, sizeof(int32) * CHANNELS);		// // //
	memset(m_fChannelLevels, 0, sizeof(float) * CHANNELS);
	memset(m_iChanLevelFallOff, 0, sizeof(uint32) * CHANNELS);

	m_fLevelSN7Left = 1.0f;
	m_fLevelSN7Right = 1.0f;
	m_fLevelSN7SepHi = 1.0f;		// // //
	m_fLevelSN7SepLo = 0.0f;		// // //

	m_iExternalChip = 0;
	m_iSampleRate = 0;
	m_iLowCut = 0;
	m_iHighCut = 0;
	m_iHighDamp = 0;
	m_fOverallVol = 1.0f;

	m_dSumSS = 0.0;
	m_dSumTND = 0.0;
}

CMixer::~CMixer()
{
}

// // //

void CMixer::ExternalSound(int Chip)
{
	m_iExternalChip = Chip;
	UpdateSettings(m_iLowCut, m_iHighCut, m_iHighDamp, m_fOverallVol);
}

void CMixer::SetChipLevel(chip_level_t Chip, float Level)
{
	switch (Chip) {
		case CHIP_LEVEL_SN7L:
			m_fLevelSN7Left = Level;
			break;
		case CHIP_LEVEL_SN7R:
			m_fLevelSN7Right = Level;
			break;
		case CHIP_LEVEL_SN7Sep:		// // //
			m_fLevelSN7SepHi = .5f + Level / 2.f;
			m_fLevelSN7SepLo = .5f - Level / 2.f;
			break;
	}
}

float CMixer::GetAttenuation() const
{
	// // //

	float Attenuation = 1.0f;

	// Increase headroom if some expansion chips are enabled

	// // //

	return Attenuation;
}

void CMixer::UpdateSettings(int LowCut,	int HighCut, int HighDamp, float OverallVol)
{
	float Volume = OverallVol * GetAttenuation();

	// Blip-buffer filtering
	BlipBufferLeft.bass_freq(LowCut);
	BlipBufferRight.bass_freq(LowCut);		// // //

	blip_eq_t eq(-HighDamp, HighCut, m_iSampleRate);

	SynthSN76489Left.treble_eq(eq);
	SynthSN76489Right.treble_eq(eq);
	// // //

	// Volume levels
	SynthSN76489Left.volume(Volume * 0.2f * m_fLevelSN7Left);
	SynthSN76489Right.volume(Volume * 0.2f * m_fLevelSN7Right);		// // //

	m_iLowCut = LowCut;
	m_iHighCut = HighCut;
	m_iHighDamp = HighDamp;
	m_fOverallVol = OverallVol;
}

// // //

void CMixer::MixSamples(blip_sample_t *pBuffer, uint32 Count)
{
	// For VRC7
	BlipBufferLeft.mix_samples(pBuffer, Count);
	//blip_mix_samples(, Count);
}

uint32 CMixer::GetMixSampleCount(int t) const
{
	return BlipBufferLeft.count_samples(t);
}

bool CMixer::AllocateBuffer(unsigned int BufferLength, uint32 SampleRate, uint8 NrChannels)
{
	m_iSampleRate = SampleRate;
	return BlipBufferLeft.set_sample_rate(SampleRate, (BufferLength * 1000 * 2) / SampleRate) == nullptr		// // //
		&& BlipBufferRight.set_sample_rate(SampleRate, (BufferLength * 1000 * 2) / SampleRate) == nullptr;
}

void CMixer::SetClockRate(uint32 Rate)
{
	// Change the clockrate
	BlipBufferLeft.clock_rate(Rate);
	BlipBufferRight.clock_rate(Rate);		// // //
}

void CMixer::ClearBuffer()
{
	BlipBufferLeft.clear();
	BlipBufferRight.clear();		// // //

	m_dSumSS = 0;
	m_dSumTND = 0;
}

int CMixer::SamplesAvail() const
{	
	return std::min<int>(BlipBufferLeft.samples_avail(), BlipBufferRight.samples_avail());		// // //
}

int CMixer::FinishBuffer(int t)
{
	BlipBufferLeft.end_frame(t);
	BlipBufferRight.end_frame(t);		// // //

	for (int i = 0; i < CHANNELS; ++i) {
		if (m_iChanLevelFallOff[i] > 0)
			m_iChanLevelFallOff[i]--;
		else {
			if (m_fChannelLevels[i] > 0) {
				m_fChannelLevels[i] -= LEVEL_FALL_OFF_RATE;
				if (m_fChannelLevels[i] < 0)
					m_fChannelLevels[i] = 0;
			}
		}
	}

	// Return number of samples available
	return SamplesAvail();		// // //
}

//
// Mixing
//

// // //

void CMixer::AddValue(int ChanID, int Chip, int Left, int Right, int FrameCycles)		// // //
{
	// Add sound to mixer
	//
	
	StoreChannelLevel(ChanID, (int)sqrt((Left * Left + Right * Right) / 2));		// // //

	if (int Delta = Left - m_iChannelsLeft[ChanID]) {		// // //
		m_iChannelsLeft[ChanID] = Left;
		switch (Chip) {
		case SNDCHIP_NONE:
			switch (ChanID) {
			case CHANID_SQUARE1:
			case CHANID_SQUARE2:
			case CHANID_SQUARE3:
			case CHANID_NOISE:
				SynthSN76489Left.offset(FrameCycles, (int)(Delta * m_fLevelSN7SepHi), &BlipBufferLeft);
				SynthSN76489Right.offset(FrameCycles, (int)(Delta * m_fLevelSN7SepLo), &BlipBufferRight);
			}
			break;
		}
	}

	if (int Delta = Right - m_iChannelsRight[ChanID]) {		// // //
		m_iChannelsRight[ChanID] = Right;
		switch (Chip) {
		case SNDCHIP_NONE:
			switch (ChanID) {
			case CHANID_SQUARE1:
			case CHANID_SQUARE2:
			case CHANID_SQUARE3:
			case CHANID_NOISE:
				SynthSN76489Left.offset(FrameCycles, (int)(Delta * m_fLevelSN7SepLo), &BlipBufferLeft);
				SynthSN76489Right.offset(FrameCycles, (int)(Delta * m_fLevelSN7SepHi), &BlipBufferRight);
			}
			break;
		}
	}
}

int CMixer::ReadBuffer(int Size, void *Buffer, bool Stereo)
{
	if (Stereo) {		// // //
		long Samples = BlipBufferLeft.read_samples((blip_sample_t*)Buffer, Size, true);
		Samples += BlipBufferRight.read_samples((blip_sample_t*)Buffer + 1, Size, true);
		return Samples;
	}
	return BlipBufferLeft.read_samples((blip_sample_t*)Buffer, Size);
}

int32 CMixer::GetChanOutput(uint8 Chan) const
{
	return (int32)m_fChannelLevels[Chan];
}

void CMixer::StoreChannelLevel(int Channel, int Value)
{
	int AbsVol = abs(Value);

	// Adjust channel levels for some channels
	switch (Channel) {		// // // 
	case CHANID_SQUARE1:
	case CHANID_SQUARE2:
	case CHANID_SQUARE3:
	case CHANID_NOISE:
		int Lv = AbsVol;
		AbsVol = 0;
		while (AbsVol < 15 && Lv >= CSN76489::VOLUME_TABLE[14 - AbsVol])
			++AbsVol;
	}

	if (float(AbsVol) >= m_fChannelLevels[Channel]) {
		m_fChannelLevels[Channel] = float(AbsVol);
		m_iChanLevelFallOff[Channel] = LEVEL_FALL_OFF_DELAY;
	}
}

void CMixer::ClearChannelLevels()
{
	memset(m_fChannelLevels, 0, sizeof(float) * CHANNELS);
	memset(m_iChanLevelFallOff, 0, sizeof(uint32) * CHANNELS);
}

uint32 CMixer::ResampleDuration(uint32 Time) const
{
	return (uint32)BlipBufferLeft.resampled_duration((blip_time_t)Time);
}
