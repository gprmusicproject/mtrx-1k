#pragma once

#include <cstdint>

namespace matrixLib
{
	// One CEM3396 voice: two waveform converters, balance, 4-pole low-pass, log and lin VCA.
	// All control inputs are pin voltages.
	class Cem3396
	{
	public:
		struct Controls
		{
			float periodA = 0.0f;     // seconds between discharge pulses, 0 = stopped
			float periodB = 0.0f;
			bool holdA = false;       // no discharge pulses arrive: the timing capacitor charges on
			bool holdB = false;
			float rtCtA = 0.0f;      // conversion resistance times timing capacitor
			float rtCtB = 0.0f;
			float wsA = 0.0f;         // waveshape (V/I converter) inputs
			float wsB = 0.0f;
			float pwA = 0.0f;         // pulse width comparator inputs
			float pwB = 0.0f;
			bool slopedA = true;      // wave select, from the quad level driver
			bool slopedB = false;
			bool noiseB = false;      // converter B capacitor driven by the noise source
			// Sync of converter A to the timer of B: 0 off, 1 soft, 2 medium, 3 hard. Bit 0: the
			// falling edge of timer B discharges the capacitor of A (SYNC2 on the voice board).
			// Bit 1: it restarts the timer of A through its GATE input (SYNC1).
			uint8_t sync = 0;
			float balance = 0.0f;
			float freq = 0.0f;
			float resonance = 0.0f;
			float mod = 0.0f;
			float logGain = 0.0f;
			float linGain = 0.0f;
		};

		void setSamplerate(float _rate);
		void reset();

		// renders one sample
		float process(const Controls& _in, float _noise);

	private:
		struct Converter
		{
			double phase = 0.0;
			double offset = 0.0;      // ramp minus timer, in periods: not zero after a sync edge
			float lastRamp = 0.0f;    // timing capacitor voltage
			float duty = 0.0f;        // share of the cycle the pulse output is high
			bool held = false;
		};

		float filter(float _in, float _cutoff, float _k);

		float m_rate = 48000.0f;
		float m_invRate = 1.0f / 48000.0f;

		Converter m_a, m_b;

		// converter outputs held back two samples for the pre-event half of each BLEP/BLAMP
		// residual: samples n-2, n-1, n
		float m_histA[3] = {};
		float m_histB[3] = {};

		float m_s[4] = {};
		float m_damp = 0.7f;
		float m_dampInv = 1.0f / 0.7f;

		Controls m_smooth;
		bool m_smoothInit = false;
		float m_cvCoeff = 0.02f;
		float m_noiseGain = 1.0f;
	};
}
