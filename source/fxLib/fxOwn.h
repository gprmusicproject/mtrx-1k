#pragma once

#include <memory>

namespace fxLib
{
	// The effects of the chain, MTRX-1K's own but for the chorus, ported from Ultramaster
	// KR-106 (GPL v3), and the shimmer, from Shimmer-Reverb (MIT) with the zita-rev1
	// reverberator of the Faust library (fxZitaRev.h); see fxOwn.cpp.
	class OwnEffect
	{
	public:
		virtual ~OwnEffect() = default;

		// buffers for this rate: not on the audio thread
		virtual void prepare(double _rate) = 0;
		// back to silence, on the audio thread
		virtual void clear() = 0;
		// _v are the controls of the type, 0-1; _timeL and _timeR the delay times in seconds
		// where the effect has them. Input and output do not overlap.
		virtual void process(const float* _inL, const float* _inR, float* _outL, float* _outR, int _samples,
		                     const float* _v, double _timeL, double _timeR) = 0;

		bool played = false;   // has sounded since it was last cleared
	};

	// modulation
	std::unique_ptr<OwnEffect> createChorus();
	std::unique_ptr<OwnEffect> createFlanger();
	std::unique_ptr<OwnEffect> createPhaser();
	std::unique_ptr<OwnEffect> createEnsemble();

	// delay: the same tape echo, with one time for both sides, a time for each, or the
	// echoes thrown from side to side
	enum class EchoMode { Mono, Stereo, PingPong };
	std::unique_ptr<OwnEffect> createEcho(EchoMode _mode);
	// the range of its time, in seconds
	constexpr double EchoShortest = 0.02;
	constexpr double EchoLongest = 2.0;

	// reverb
	std::unique_ptr<OwnEffect> createHall();
	std::unique_ptr<OwnEffect> createRoom();
	std::unique_ptr<OwnEffect> createPlate();
	std::unique_ptr<OwnEffect> createSpring();
	std::unique_ptr<OwnEffect> createShimmer();

	// how much of the dry sound and of the effect a Mix control lets through: both at full
	// level at the middle, the effect fading to the left, the dry sound to the right
	inline float dryShare(const float _mix) { return _mix < 0.5f ? 1.0f : 2.0f - 2.0f * _mix; }
	inline float wetShare(const float _mix) { return _mix > 0.5f ? 1.0f : 2.0f * _mix; }
}
