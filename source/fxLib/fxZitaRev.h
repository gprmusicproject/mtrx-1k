#pragma once

// The reverberator of Fons Adriaensen's zita-rev1 as the Faust library has it
// (demos.lib zita_rev1, an 8x8 feedback delay network with Schroeder allpass combs),
// generated with Faust 2.81.2, taken from Shimmer-Reverb by Danial Kooshki
// (github.com/Danial-k-f/Shimmer-Reverb, MIT licence). Only the user section of the generated
// file is here: the Faust architecture around it, its metadata and its user interface are
// left out, and the class stands alone.
//
// author: Julius O. Smith III, Christopher Arndt
// Copyright (C) 2003-2019 by Julius O. Smith III <jos@ccrma.stanford.edu>
// licence: STK-4.3 (MIT-style); zita_rev1 in demos.lib: MIT
//
// Shimmer-Reverb: MIT License, Copyright (c) 2025 Danial Kooshki
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of this
// software and associated documentation files (the "Software"), to deal in the Software
// without restriction, including without limitation the rights to use, copy, modify, merge,
// publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons
// to whom the Software is furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all copies or
// substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
// INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR
// PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE
// FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
// OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
// DEALINGS IN THE SOFTWARE.

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace fxLib::zita
{
using FAUSTFLOAT = float;

#if defined(_WIN32)
#define RESTRICT __restrict
#else
#define RESTRICT __restrict__
#endif

static float ZitaRev_faustpower2_f(float value) {
	return value * value;
}

class ZitaRev {
	


	

	// ==== Other Faust-generated methods ====
	/*void init(int sample_rate) { ... }
	void compute(int count, FAUSTFLOAT** inputs, FAUSTFLOAT** outputs) { ... }*/

 private:
	
	FAUSTFLOAT fVslider0;
	FAUSTFLOAT fVslider1;
	int fSampleRate;
	float fConst0;
	float fConst1;
	FAUSTFLOAT fVslider2;
	FAUSTFLOAT fVslider3;
	FAUSTFLOAT fVslider4;
	float fConst2;
	float fConst3;
	FAUSTFLOAT fVslider5;
	FAUSTFLOAT fVslider6;
	float fConst4;
	float fRec13[2];
	FAUSTFLOAT fVslider7;
	float fRec12[2];
	int IOTA0;
	float fVec0[16384];
	float fConst5;
	int iConst6;
	float fVec1[16384];
	FAUSTFLOAT fVslider8;
	float fConst7;
	float fVec2[4096];
	int iConst8;
	float fRec10[2];
	float fConst9;
	float fConst10;
	float fRec17[2];
	float fRec16[2];
	float fVec3[16384];
	float fConst11;
	int iConst12;
	float fVec4[2048];
	int iConst13;
	float fRec14[2];
	float fConst14;
	float fConst15;
	float fRec21[2];
	float fRec20[2];
	float fVec5[16384];
	float fConst16;
	int iConst17;
	float fVec6[4096];
	int iConst18;
	float fRec18[2];
	float fConst19;
	float fConst20;
	float fRec25[2];
	float fRec24[2];
	float fVec7[16384];
	float fConst21;
	int iConst22;
	float fVec8[2048];
	int iConst23;
	float fRec22[2];
	float fConst24;
	float fConst25;
	float fRec29[2];
	float fRec28[2];
	float fVec9[32768];
	float fConst26;
	int iConst27;
	float fVec10[16384];
	float fVec11[4096];
	int iConst28;
	float fRec26[2];
	float fConst29;
	float fConst30;
	float fRec33[2];
	float fRec32[2];
	float fVec12[16384];
	float fConst31;
	int iConst32;
	float fVec13[4096];
	int iConst33;
	float fRec30[2];
	float fConst34;
	float fConst35;
	float fRec37[2];
	float fRec36[2];
	float fVec14[32768];
	float fConst36;
	int iConst37;
	float fVec15[4096];
	int iConst38;
	float fRec34[2];
	float fConst39;
	float fConst40;
	float fRec41[2];
	float fRec40[2];
	float fVec16[32768];
	float fConst41;
	int iConst42;
	float fVec17[2048];
	int iConst43;
	float fRec38[2];
	float fRec2[3];
	float fRec3[3];
	float fRec4[3];
	float fRec5[3];
	float fRec6[3];
	float fRec7[3];
	float fRec8[3];
	float fRec9[3];
	float fRec1[3];
	float fRec0[3];
	float fConst44;
	float fConst45;
	FAUSTFLOAT fVslider9;
	float fRec42[2];
	FAUSTFLOAT fVslider10;
	float fRec43[2];
	float fRec45[3];
	float fRec44[3];
	
 public:
	ZitaRev() {
	}
	
	// ==== Setters for JUCE parameters ====
	void setDecay(float value) { fVslider4 = value; }
	void setLowRT60(float value) { fVslider7 = value; }
	void setHFDamping(float value) { fVslider5 = value; }
	void setLFx(float value) { fVslider6 = value; }
	void setPreDelay(float value) { fVslider8 = value; }
	void setEQ1(float freq, float gain) { fVslider3 = freq; fVslider2 = gain; }
	void setEQ2(float freq, float gain) { fVslider1 = freq; fVslider0 = gain; }
	void setWetDry(float value) { fVslider9 = value; }
	void setOutputLevel(float value) { fVslider10 = value; }



	int getNumInputs() {
		return 2;
	}
	int getNumOutputs() {
		return 2;
	}
	
	static void classInit(int sample_rate) {
	}
	
	void instanceConstants(int sample_rate) {
		fSampleRate = sample_rate;
		fConst0 = std::min<float>(1.92e+05f, std::max<float>(1.0f, float(fSampleRate)));
		fConst1 = 6.2831855f / fConst0;
		fConst2 = std::floor(0.174713f * fConst0 + 0.5f);
		fConst3 = 6.9077554f * (fConst2 / fConst0);
		fConst4 = 3.1415927f / fConst0;
		fConst5 = std::floor(0.022904f * fConst0 + 0.5f);
		iConst6 = int(std::min<float>(8192.0f, std::max<float>(0.0f, fConst2 - fConst5)));
		fConst7 = 0.001f * fConst0;
		iConst8 = int(std::min<float>(2048.0f, std::max<float>(0.0f, fConst5 + -1.0f)));
		fConst9 = std::floor(0.153129f * fConst0 + 0.5f);
		fConst10 = 6.9077554f * (fConst9 / fConst0);
		fConst11 = std::floor(0.020346f * fConst0 + 0.5f);
		iConst12 = int(std::min<float>(8192.0f, std::max<float>(0.0f, fConst9 - fConst11)));
		iConst13 = int(std::min<float>(1024.0f, std::max<float>(0.0f, fConst11 + -1.0f)));
		fConst14 = std::floor(0.127837f * fConst0 + 0.5f);
		fConst15 = 6.9077554f * (fConst14 / fConst0);
		fConst16 = std::floor(0.031604f * fConst0 + 0.5f);
		iConst17 = int(std::min<float>(8192.0f, std::max<float>(0.0f, fConst14 - fConst16)));
		iConst18 = int(std::min<float>(2048.0f, std::max<float>(0.0f, fConst16 + -1.0f)));
		fConst19 = std::floor(0.125f * fConst0 + 0.5f);
		fConst20 = 6.9077554f * (fConst19 / fConst0);
		fConst21 = std::floor(0.013458f * fConst0 + 0.5f);
		iConst22 = int(std::min<float>(8192.0f, std::max<float>(0.0f, fConst19 - fConst21)));
		iConst23 = int(std::min<float>(1024.0f, std::max<float>(0.0f, fConst21 + -1.0f)));
		fConst24 = std::floor(0.210389f * fConst0 + 0.5f);
		fConst25 = 6.9077554f * (fConst24 / fConst0);
		fConst26 = std::floor(0.024421f * fConst0 + 0.5f);
		iConst27 = int(std::min<float>(16384.0f, std::max<float>(0.0f, fConst24 - fConst26)));
		iConst28 = int(std::min<float>(2048.0f, std::max<float>(0.0f, fConst26 + -1.0f)));
		fConst29 = std::floor(0.192303f * fConst0 + 0.5f);
		fConst30 = 6.9077554f * (fConst29 / fConst0);
		fConst31 = std::floor(0.029291f * fConst0 + 0.5f);
		iConst32 = int(std::min<float>(8192.0f, std::max<float>(0.0f, fConst29 - fConst31)));
		iConst33 = int(std::min<float>(2048.0f, std::max<float>(0.0f, fConst31 + -1.0f)));
		fConst34 = std::floor(0.256891f * fConst0 + 0.5f);
		fConst35 = 6.9077554f * (fConst34 / fConst0);
		fConst36 = std::floor(0.027333f * fConst0 + 0.5f);
		iConst37 = int(std::min<float>(16384.0f, std::max<float>(0.0f, fConst34 - fConst36)));
		iConst38 = int(std::min<float>(2048.0f, std::max<float>(0.0f, fConst36 + -1.0f)));
		fConst39 = std::floor(0.219991f * fConst0 + 0.5f);
		fConst40 = 6.9077554f * (fConst39 / fConst0);
		fConst41 = std::floor(0.019123f * fConst0 + 0.5f);
		iConst42 = int(std::min<float>(16384.0f, std::max<float>(0.0f, fConst39 - fConst41)));
		iConst43 = int(std::min<float>(1024.0f, std::max<float>(0.0f, fConst41 + -1.0f)));
		fConst44 = 44.1f / fConst0;
		fConst45 = 1.0f - fConst44;
	}
	
	void instanceResetUserInterface() {
		fVslider0 = FAUSTFLOAT(0.0f);
		fVslider1 = FAUSTFLOAT(1.5e+03f);
		fVslider2 = FAUSTFLOAT(0.0f);
		fVslider3 = FAUSTFLOAT(315.0f);
		fVslider4 = FAUSTFLOAT(2.0f);
		fVslider5 = FAUSTFLOAT(6e+03f);
		fVslider6 = FAUSTFLOAT(2e+02f);
		fVslider7 = FAUSTFLOAT(3.0f);
		fVslider8 = FAUSTFLOAT(6e+01f);
		fVslider9 = FAUSTFLOAT(0.0f);
		fVslider10 = FAUSTFLOAT(-2e+01f);
	}
	
	void instanceClear() {
		for (int l0 = 0; l0 < 2; l0 = l0 + 1) {
			fRec13[l0] = 0.0f;
		}
		for (int l1 = 0; l1 < 2; l1 = l1 + 1) {
			fRec12[l1] = 0.0f;
		}
		IOTA0 = 0;
		for (int l2 = 0; l2 < 16384; l2 = l2 + 1) {
			fVec0[l2] = 0.0f;
		}
		for (int l3 = 0; l3 < 16384; l3 = l3 + 1) {
			fVec1[l3] = 0.0f;
		}
		for (int l4 = 0; l4 < 4096; l4 = l4 + 1) {
			fVec2[l4] = 0.0f;
		}
		for (int l5 = 0; l5 < 2; l5 = l5 + 1) {
			fRec10[l5] = 0.0f;
		}
		for (int l6 = 0; l6 < 2; l6 = l6 + 1) {
			fRec17[l6] = 0.0f;
		}
		for (int l7 = 0; l7 < 2; l7 = l7 + 1) {
			fRec16[l7] = 0.0f;
		}
		for (int l8 = 0; l8 < 16384; l8 = l8 + 1) {
			fVec3[l8] = 0.0f;
		}
		for (int l9 = 0; l9 < 2048; l9 = l9 + 1) {
			fVec4[l9] = 0.0f;
		}
		for (int l10 = 0; l10 < 2; l10 = l10 + 1) {
			fRec14[l10] = 0.0f;
		}
		for (int l11 = 0; l11 < 2; l11 = l11 + 1) {
			fRec21[l11] = 0.0f;
		}
		for (int l12 = 0; l12 < 2; l12 = l12 + 1) {
			fRec20[l12] = 0.0f;
		}
		for (int l13 = 0; l13 < 16384; l13 = l13 + 1) {
			fVec5[l13] = 0.0f;
		}
		for (int l14 = 0; l14 < 4096; l14 = l14 + 1) {
			fVec6[l14] = 0.0f;
		}
		for (int l15 = 0; l15 < 2; l15 = l15 + 1) {
			fRec18[l15] = 0.0f;
		}
		for (int l16 = 0; l16 < 2; l16 = l16 + 1) {
			fRec25[l16] = 0.0f;
		}
		for (int l17 = 0; l17 < 2; l17 = l17 + 1) {
			fRec24[l17] = 0.0f;
		}
		for (int l18 = 0; l18 < 16384; l18 = l18 + 1) {
			fVec7[l18] = 0.0f;
		}
		for (int l19 = 0; l19 < 2048; l19 = l19 + 1) {
			fVec8[l19] = 0.0f;
		}
		for (int l20 = 0; l20 < 2; l20 = l20 + 1) {
			fRec22[l20] = 0.0f;
		}
		for (int l21 = 0; l21 < 2; l21 = l21 + 1) {
			fRec29[l21] = 0.0f;
		}
		for (int l22 = 0; l22 < 2; l22 = l22 + 1) {
			fRec28[l22] = 0.0f;
		}
		for (int l23 = 0; l23 < 32768; l23 = l23 + 1) {
			fVec9[l23] = 0.0f;
		}
		for (int l24 = 0; l24 < 16384; l24 = l24 + 1) {
			fVec10[l24] = 0.0f;
		}
		for (int l25 = 0; l25 < 4096; l25 = l25 + 1) {
			fVec11[l25] = 0.0f;
		}
		for (int l26 = 0; l26 < 2; l26 = l26 + 1) {
			fRec26[l26] = 0.0f;
		}
		for (int l27 = 0; l27 < 2; l27 = l27 + 1) {
			fRec33[l27] = 0.0f;
		}
		for (int l28 = 0; l28 < 2; l28 = l28 + 1) {
			fRec32[l28] = 0.0f;
		}
		for (int l29 = 0; l29 < 16384; l29 = l29 + 1) {
			fVec12[l29] = 0.0f;
		}
		for (int l30 = 0; l30 < 4096; l30 = l30 + 1) {
			fVec13[l30] = 0.0f;
		}
		for (int l31 = 0; l31 < 2; l31 = l31 + 1) {
			fRec30[l31] = 0.0f;
		}
		for (int l32 = 0; l32 < 2; l32 = l32 + 1) {
			fRec37[l32] = 0.0f;
		}
		for (int l33 = 0; l33 < 2; l33 = l33 + 1) {
			fRec36[l33] = 0.0f;
		}
		for (int l34 = 0; l34 < 32768; l34 = l34 + 1) {
			fVec14[l34] = 0.0f;
		}
		for (int l35 = 0; l35 < 4096; l35 = l35 + 1) {
			fVec15[l35] = 0.0f;
		}
		for (int l36 = 0; l36 < 2; l36 = l36 + 1) {
			fRec34[l36] = 0.0f;
		}
		for (int l37 = 0; l37 < 2; l37 = l37 + 1) {
			fRec41[l37] = 0.0f;
		}
		for (int l38 = 0; l38 < 2; l38 = l38 + 1) {
			fRec40[l38] = 0.0f;
		}
		for (int l39 = 0; l39 < 32768; l39 = l39 + 1) {
			fVec16[l39] = 0.0f;
		}
		for (int l40 = 0; l40 < 2048; l40 = l40 + 1) {
			fVec17[l40] = 0.0f;
		}
		for (int l41 = 0; l41 < 2; l41 = l41 + 1) {
			fRec38[l41] = 0.0f;
		}
		for (int l42 = 0; l42 < 3; l42 = l42 + 1) {
			fRec2[l42] = 0.0f;
		}
		for (int l43 = 0; l43 < 3; l43 = l43 + 1) {
			fRec3[l43] = 0.0f;
		}
		for (int l44 = 0; l44 < 3; l44 = l44 + 1) {
			fRec4[l44] = 0.0f;
		}
		for (int l45 = 0; l45 < 3; l45 = l45 + 1) {
			fRec5[l45] = 0.0f;
		}
		for (int l46 = 0; l46 < 3; l46 = l46 + 1) {
			fRec6[l46] = 0.0f;
		}
		for (int l47 = 0; l47 < 3; l47 = l47 + 1) {
			fRec7[l47] = 0.0f;
		}
		for (int l48 = 0; l48 < 3; l48 = l48 + 1) {
			fRec8[l48] = 0.0f;
		}
		for (int l49 = 0; l49 < 3; l49 = l49 + 1) {
			fRec9[l49] = 0.0f;
		}
		for (int l50 = 0; l50 < 3; l50 = l50 + 1) {
			fRec1[l50] = 0.0f;
		}
		for (int l51 = 0; l51 < 3; l51 = l51 + 1) {
			fRec0[l51] = 0.0f;
		}
		for (int l52 = 0; l52 < 2; l52 = l52 + 1) {
			fRec42[l52] = 0.0f;
		}
		for (int l53 = 0; l53 < 2; l53 = l53 + 1) {
			fRec43[l53] = 0.0f;
		}
		for (int l54 = 0; l54 < 3; l54 = l54 + 1) {
			fRec45[l54] = 0.0f;
		}
		for (int l55 = 0; l55 < 3; l55 = l55 + 1) {
			fRec44[l55] = 0.0f;
		}
	}
	
	void init(int sample_rate) {
		classInit(sample_rate);
		instanceInit(sample_rate);
	}
	
	void instanceInit(int sample_rate) {
		instanceConstants(sample_rate);
		instanceResetUserInterface();
		instanceClear();
	}
	
	
	int getSampleRate() {
		return fSampleRate;
	}
	
	
	void compute(int count, FAUSTFLOAT** RESTRICT inputs, FAUSTFLOAT** RESTRICT outputs) {
		FAUSTFLOAT* input0 = inputs[0];
		FAUSTFLOAT* input1 = inputs[1];
		FAUSTFLOAT* output0 = outputs[0];
		FAUSTFLOAT* output1 = outputs[1];
		float fSlow0 = std::pow(1e+01f, 0.05f * float(fVslider0));
		float fSlow1 = float(fVslider1);
		float fSlow2 = fConst1 * (fSlow1 / std::sqrt(std::max<float>(0.0f, fSlow0)));
		float fSlow3 = (1.0f - fSlow2) / (fSlow2 + 1.0f);
		float fSlow4 = std::cos(fConst1 * fSlow1) * (fSlow3 + 1.0f);
		float fSlow5 = std::pow(1e+01f, 0.05f * float(fVslider2));
		float fSlow6 = float(fVslider3);
		float fSlow7 = fConst1 * (fSlow6 / std::sqrt(std::max<float>(0.0f, fSlow5)));
		float fSlow8 = (1.0f - fSlow7) / (fSlow7 + 1.0f);
		float fSlow9 = std::cos(fConst1 * fSlow6) * (fSlow8 + 1.0f);
		float fSlow10 = float(fVslider4);
		float fSlow11 = std::exp(-(fConst3 / fSlow10));
		float fSlow12 = ZitaRev_faustpower2_f(fSlow11);
		float fSlow13 = 1.0f - fSlow12;
		float fSlow14 = std::cos(fConst1 * float(fVslider5));
		float fSlow15 = 1.0f - fSlow14 * fSlow12;
		float fSlow16 = std::sqrt(std::max<float>(0.0f, ZitaRev_faustpower2_f(fSlow15) / ZitaRev_faustpower2_f(fSlow13) + -1.0f));
		float fSlow17 = fSlow15 / fSlow13;
		float fSlow18 = fSlow17 - fSlow16;
		float fSlow19 = 1.0f / std::tan(fConst4 * float(fVslider6));
		float fSlow20 = 1.0f - fSlow19;
		float fSlow21 = 1.0f / (fSlow19 + 1.0f);
		float fSlow22 = float(fVslider7);
		float fSlow23 = std::exp(-(fConst3 / fSlow22)) / fSlow11 + -1.0f;
		float fSlow24 = fSlow11 * (fSlow16 + (1.0f - fSlow17));
		int iSlow25 = int(std::min<float>(8192.0f, std::max<float>(0.0f, fConst7 * float(fVslider8))));
		float fSlow26 = std::exp(-(fConst10 / fSlow10));
		float fSlow27 = ZitaRev_faustpower2_f(fSlow26);
		float fSlow28 = 1.0f - fSlow27;
		float fSlow29 = 1.0f - fSlow27 * fSlow14;
		float fSlow30 = std::sqrt(std::max<float>(0.0f, ZitaRev_faustpower2_f(fSlow29) / ZitaRev_faustpower2_f(fSlow28) + -1.0f));
		float fSlow31 = fSlow29 / fSlow28;
		float fSlow32 = fSlow31 - fSlow30;
		float fSlow33 = std::exp(-(fConst10 / fSlow22)) / fSlow26 + -1.0f;
		float fSlow34 = fSlow26 * (fSlow30 + (1.0f - fSlow31));
		float fSlow35 = std::exp(-(fConst15 / fSlow10));
		float fSlow36 = ZitaRev_faustpower2_f(fSlow35);
		float fSlow37 = 1.0f - fSlow36;
		float fSlow38 = 1.0f - fSlow14 * fSlow36;
		float fSlow39 = std::sqrt(std::max<float>(0.0f, ZitaRev_faustpower2_f(fSlow38) / ZitaRev_faustpower2_f(fSlow37) + -1.0f));
		float fSlow40 = fSlow38 / fSlow37;
		float fSlow41 = fSlow40 - fSlow39;
		float fSlow42 = std::exp(-(fConst15 / fSlow22)) / fSlow35 + -1.0f;
		float fSlow43 = fSlow35 * (fSlow39 + (1.0f - fSlow40));
		float fSlow44 = std::exp(-(fConst20 / fSlow10));
		float fSlow45 = ZitaRev_faustpower2_f(fSlow44);
		float fSlow46 = 1.0f - fSlow45;
		float fSlow47 = 1.0f - fSlow14 * fSlow45;
		float fSlow48 = std::sqrt(std::max<float>(0.0f, ZitaRev_faustpower2_f(fSlow47) / ZitaRev_faustpower2_f(fSlow46) + -1.0f));
		float fSlow49 = fSlow47 / fSlow46;
		float fSlow50 = fSlow49 - fSlow48;
		float fSlow51 = std::exp(-(fConst20 / fSlow22)) / fSlow44 + -1.0f;
		float fSlow52 = fSlow44 * (fSlow48 + (1.0f - fSlow49));
		float fSlow53 = std::exp(-(fConst25 / fSlow10));
		float fSlow54 = ZitaRev_faustpower2_f(fSlow53);
		float fSlow55 = 1.0f - fSlow54;
		float fSlow56 = 1.0f - fSlow14 * fSlow54;
		float fSlow57 = std::sqrt(std::max<float>(0.0f, ZitaRev_faustpower2_f(fSlow56) / ZitaRev_faustpower2_f(fSlow55) + -1.0f));
		float fSlow58 = fSlow56 / fSlow55;
		float fSlow59 = fSlow58 - fSlow57;
		float fSlow60 = std::exp(-(fConst25 / fSlow22)) / fSlow53 + -1.0f;
		float fSlow61 = fSlow53 * (fSlow57 + (1.0f - fSlow58));
		float fSlow62 = std::exp(-(fConst30 / fSlow10));
		float fSlow63 = ZitaRev_faustpower2_f(fSlow62);
		float fSlow64 = 1.0f - fSlow63;
		float fSlow65 = 1.0f - fSlow14 * fSlow63;
		float fSlow66 = std::sqrt(std::max<float>(0.0f, ZitaRev_faustpower2_f(fSlow65) / ZitaRev_faustpower2_f(fSlow64) + -1.0f));
		float fSlow67 = fSlow65 / fSlow64;
		float fSlow68 = fSlow67 - fSlow66;
		float fSlow69 = std::exp(-(fConst30 / fSlow22)) / fSlow62 + -1.0f;
		float fSlow70 = fSlow62 * (fSlow66 + (1.0f - fSlow67));
		float fSlow71 = std::exp(-(fConst35 / fSlow10));
		float fSlow72 = ZitaRev_faustpower2_f(fSlow71);
		float fSlow73 = 1.0f - fSlow72;
		float fSlow74 = 1.0f - fSlow14 * fSlow72;
		float fSlow75 = std::sqrt(std::max<float>(0.0f, ZitaRev_faustpower2_f(fSlow74) / ZitaRev_faustpower2_f(fSlow73) + -1.0f));
		float fSlow76 = fSlow74 / fSlow73;
		float fSlow77 = fSlow76 - fSlow75;
		float fSlow78 = std::exp(-(fConst35 / fSlow22)) / fSlow71 + -1.0f;
		float fSlow79 = fSlow71 * (fSlow75 + (1.0f - fSlow76));
		float fSlow80 = std::exp(-(fConst40 / fSlow10));
		float fSlow81 = ZitaRev_faustpower2_f(fSlow80);
		float fSlow82 = 1.0f - fSlow81;
		float fSlow83 = 1.0f - fSlow14 * fSlow81;
		float fSlow84 = std::sqrt(std::max<float>(0.0f, ZitaRev_faustpower2_f(fSlow83) / ZitaRev_faustpower2_f(fSlow82) + -1.0f));
		float fSlow85 = fSlow83 / fSlow82;
		float fSlow86 = fSlow85 - fSlow84;
		float fSlow87 = std::exp(-(fConst40 / fSlow22)) / fSlow80 + -1.0f;
		float fSlow88 = fSlow80 * (fSlow84 + (1.0f - fSlow85));
		float fSlow89 = fConst44 * float(fVslider9);
		float fSlow90 = fConst44 * std::pow(1e+01f, 0.05f * float(fVslider10));
		for (int i0 = 0; i0 < count; i0 = i0 + 1) {
			float fTemp0 = fSlow4 * fRec0[1];
			float fTemp1 = fSlow9 * fRec1[1];
			fRec13[0] = -(fSlow21 * (fSlow20 * fRec13[1] - (fRec6[1] + fRec6[2])));
			fRec12[0] = fSlow24 * (fRec6[1] + fSlow23 * fRec13[0]) + fSlow18 * fRec12[1];
			fVec0[IOTA0 & 16383] = 0.35355338f * fRec12[0] + 1e-20f;
			float fTemp2 = float(input0[i0]);
			fVec1[IOTA0 & 16383] = fTemp2;
			float fTemp3 = 0.3f * fVec1[(IOTA0 - iSlow25) & 16383];
			float fTemp4 = fTemp3 + fVec0[(IOTA0 - iConst6) & 16383] - 0.6f * fRec10[1];
			fVec2[IOTA0 & 4095] = fTemp4;
			fRec10[0] = fVec2[(IOTA0 - iConst8) & 4095];
			float fRec11 = 0.6f * fTemp4;
			fRec17[0] = -(fSlow21 * (fSlow20 * fRec17[1] - (fRec2[1] + fRec2[2])));
			fRec16[0] = fSlow34 * (fRec2[1] + fSlow33 * fRec17[0]) + fSlow32 * fRec16[1];
			fVec3[IOTA0 & 16383] = 0.35355338f * fRec16[0] + 1e-20f;
			float fTemp5 = fVec3[(IOTA0 - iConst12) & 16383] + fTemp3 - 0.6f * fRec14[1];
			fVec4[IOTA0 & 2047] = fTemp5;
			fRec14[0] = fVec4[(IOTA0 - iConst13) & 2047];
			float fRec15 = 0.6f * fTemp5;
			float fTemp6 = fRec15 + fRec11;
			fRec21[0] = -(fSlow21 * (fSlow20 * fRec21[1] - (fRec4[1] + fRec4[2])));
			fRec20[0] = fSlow43 * (fRec4[1] + fSlow42 * fRec21[0]) + fSlow41 * fRec20[1];
			fVec5[IOTA0 & 16383] = 0.35355338f * fRec20[0] + 1e-20f;
			float fTemp7 = fVec5[(IOTA0 - iConst17) & 16383] - (fTemp3 + 0.6f * fRec18[1]);
			fVec6[IOTA0 & 4095] = fTemp7;
			fRec18[0] = fVec6[(IOTA0 - iConst18) & 4095];
			float fRec19 = 0.6f * fTemp7;
			fRec25[0] = -(fSlow21 * (fSlow20 * fRec25[1] - (fRec8[1] + fRec8[2])));
			fRec24[0] = fSlow52 * (fRec8[1] + fSlow51 * fRec25[0]) + fSlow50 * fRec24[1];
			fVec7[IOTA0 & 16383] = 0.35355338f * fRec24[0] + 1e-20f;
			float fTemp8 = fVec7[(IOTA0 - iConst22) & 16383] - (fTemp3 + 0.6f * fRec22[1]);
			fVec8[IOTA0 & 2047] = fTemp8;
			fRec22[0] = fVec8[(IOTA0 - iConst23) & 2047];
			float fRec23 = 0.6f * fTemp8;
			float fTemp9 = fRec23 + fRec19 + fTemp6;
			fRec29[0] = -(fSlow21 * (fSlow20 * fRec29[1] - (fRec3[1] + fRec3[2])));
			fRec28[0] = fSlow61 * (fRec3[1] + fSlow60 * fRec29[0]) + fSlow59 * fRec28[1];
			fVec9[IOTA0 & 32767] = 0.35355338f * fRec28[0] + 1e-20f;
			float fTemp10 = float(input1[i0]);
			fVec10[IOTA0 & 16383] = fTemp10;
			float fTemp11 = 0.3f * fVec10[(IOTA0 - iSlow25) & 16383];
			float fTemp12 = fTemp11 + 0.6f * fRec26[1] + fVec9[(IOTA0 - iConst27) & 32767];
			fVec11[IOTA0 & 4095] = fTemp12;
			fRec26[0] = fVec11[(IOTA0 - iConst28) & 4095];
			float fRec27 = -(0.6f * fTemp12);
			fRec33[0] = -(fSlow21 * (fSlow20 * fRec33[1] - (fRec7[1] + fRec7[2])));
			fRec32[0] = fSlow70 * (fRec7[1] + fSlow69 * fRec33[0]) + fSlow68 * fRec32[1];
			fVec12[IOTA0 & 16383] = 0.35355338f * fRec32[0] + 1e-20f;
			float fTemp13 = fVec12[(IOTA0 - iConst32) & 16383] + fTemp11 + 0.6f * fRec30[1];
			fVec13[IOTA0 & 4095] = fTemp13;
			fRec30[0] = fVec13[(IOTA0 - iConst33) & 4095];
			float fRec31 = -(0.6f * fTemp13);
			fRec37[0] = -(fSlow21 * (fSlow20 * fRec37[1] - (fRec5[1] + fRec5[2])));
			fRec36[0] = fSlow79 * (fRec5[1] + fSlow78 * fRec37[0]) + fSlow77 * fRec36[1];
			fVec14[IOTA0 & 32767] = 0.35355338f * fRec36[0] + 1e-20f;
			float fTemp14 = 0.6f * fRec34[1] + fVec14[(IOTA0 - iConst37) & 32767];
			fVec15[IOTA0 & 4095] = fTemp14 - fTemp11;
			fRec34[0] = fVec15[(IOTA0 - iConst38) & 4095];
			float fRec35 = 0.6f * (fTemp11 - fTemp14);
			fRec41[0] = -(fSlow21 * (fSlow20 * fRec41[1] - (fRec9[1] + fRec9[2])));
			fRec40[0] = fSlow88 * (fRec9[1] + fSlow87 * fRec41[0]) + fSlow86 * fRec40[1];
			fVec16[IOTA0 & 32767] = 0.35355338f * fRec40[0] + 1e-20f;
			float fTemp15 = 0.6f * fRec38[1] + fVec16[(IOTA0 - iConst42) & 32767];
			fVec17[IOTA0 & 2047] = fTemp15 - fTemp11;
			fRec38[0] = fVec17[(IOTA0 - iConst43) & 2047];
			float fRec39 = 0.6f * (fTemp11 - fTemp15);
			fRec2[0] = fRec38[1] + fRec34[1] + fRec30[1] + fRec26[1] + fRec22[1] + fRec18[1] + fRec10[1] + fRec14[1] + fRec39 + fRec35 + fRec31 + fRec27 + fTemp9;
			fRec3[0] = fRec22[1] + fRec18[1] + fRec10[1] + fRec14[1] + fTemp9 - (fRec38[1] + fRec34[1] + fRec30[1] + fRec26[1] + fRec39 + fRec35 + fRec27 + fRec31);
			float fTemp16 = fRec19 + fRec23;
			fRec4[0] = fRec30[1] + fRec26[1] + fRec10[1] + fRec14[1] + fRec31 + fRec27 + fTemp6 - (fRec38[1] + fRec34[1] + fRec22[1] + fRec18[1] + fRec39 + fRec35 + fTemp16);
			fRec5[0] = fRec38[1] + fRec34[1] + fRec10[1] + fRec14[1] + fRec39 + fRec35 + fTemp6 - (fRec30[1] + fRec26[1] + fRec22[1] + fRec18[1] + fRec31 + fRec27 + fTemp16);
			float fTemp17 = fRec11 + fRec23;
			float fTemp18 = fRec15 + fRec19;
			fRec6[0] = fRec34[1] + fRec26[1] + fRec18[1] + fRec14[1] + fRec35 + fRec27 + fTemp18 - (fRec38[1] + fRec30[1] + fRec22[1] + fRec10[1] + fRec39 + fRec31 + fTemp17);
			fRec7[0] = fRec38[1] + fRec30[1] + fRec18[1] + fRec14[1] + fRec39 + fRec31 + fTemp18 - (fRec34[1] + fRec26[1] + fRec22[1] + fRec10[1] + fRec35 + fRec27 + fTemp17);
			float fTemp19 = fRec11 + fRec19;
			float fTemp20 = fRec15 + fRec23;
			fRec8[0] = fRec38[1] + fRec26[1] + fRec22[1] + fRec14[1] + fRec39 + fRec27 + fTemp20 - (fRec34[1] + fRec30[1] + fRec18[1] + fRec10[1] + fRec35 + fRec31 + fTemp19);
			fRec9[0] = fRec34[1] + fRec30[1] + fRec22[1] + fRec14[1] + fRec35 + fRec31 + fTemp20 - (fRec38[1] + fRec26[1] + fRec18[1] + fRec10[1] + fRec39 + fRec27 + fTemp19);
			float fTemp21 = 0.37f * (fRec3[0] + fRec4[0]);
			float fTemp22 = fTemp21 + fTemp1;
			fRec1[0] = fTemp22 - fSlow8 * fRec1[2];
			float fTemp23 = fSlow8 * fRec1[0];
			float fTemp24 = 0.5f * (fTemp23 + fTemp21 + fRec1[2] - fTemp1 + fSlow5 * (fRec1[2] + fTemp23 - fTemp22));
			float fTemp25 = fTemp24 + fTemp0;
			fRec0[0] = fTemp25 - fSlow3 * fRec0[2];
			float fTemp26 = fSlow3 * fRec0[0];
			fRec42[0] = fSlow89 + fConst45 * fRec42[1];
			float fTemp27 = fRec42[0] + 1.0f;
			float fTemp28 = 1.0f - 0.5f * fTemp27;
			fRec43[0] = fSlow90 + fConst45 * fRec43[1];
			output0[i0] = FAUSTFLOAT(0.5f * fRec43[0] * (fTemp2 * fTemp27 + fTemp28 * (fTemp26 + fTemp24 + fRec0[2] - fTemp0 + fSlow0 * (fRec0[2] + fTemp26 - fTemp25))));
			float fTemp29 = fSlow4 * fRec44[1];
			float fTemp30 = fSlow9 * fRec45[1];
			float fTemp31 = 0.37f * (fRec3[0] - fRec4[0]);
			float fTemp32 = fTemp31 + fTemp30;
			fRec45[0] = fTemp32 - fSlow8 * fRec45[2];
			float fTemp33 = fSlow8 * fRec45[0];
			float fTemp34 = 0.5f * (fTemp33 + fTemp31 + fRec45[2] - fTemp30 + fSlow5 * (fRec45[2] + fTemp33 - fTemp32));
			float fTemp35 = fTemp34 + fTemp29;
			fRec44[0] = fTemp35 - fSlow3 * fRec44[2];
			float fTemp36 = fSlow3 * fRec44[0];
			output1[i0] = FAUSTFLOAT(0.5f * fRec43[0] * (fTemp10 * fTemp27 + fTemp28 * (fTemp36 + fTemp34 + fRec44[2] - fTemp29 + fSlow0 * (fRec44[2] + fTemp36 - fTemp35))));
			fRec13[1] = fRec13[0];
			fRec12[1] = fRec12[0];
			IOTA0 = IOTA0 + 1;
			fRec10[1] = fRec10[0];
			fRec17[1] = fRec17[0];
			fRec16[1] = fRec16[0];
			fRec14[1] = fRec14[0];
			fRec21[1] = fRec21[0];
			fRec20[1] = fRec20[0];
			fRec18[1] = fRec18[0];
			fRec25[1] = fRec25[0];
			fRec24[1] = fRec24[0];
			fRec22[1] = fRec22[0];
			fRec29[1] = fRec29[0];
			fRec28[1] = fRec28[0];
			fRec26[1] = fRec26[0];
			fRec33[1] = fRec33[0];
			fRec32[1] = fRec32[0];
			fRec30[1] = fRec30[0];
			fRec37[1] = fRec37[0];
			fRec36[1] = fRec36[0];
			fRec34[1] = fRec34[0];
			fRec41[1] = fRec41[0];
			fRec40[1] = fRec40[0];
			fRec38[1] = fRec38[0];
			fRec2[2] = fRec2[1];
			fRec2[1] = fRec2[0];
			fRec3[2] = fRec3[1];
			fRec3[1] = fRec3[0];
			fRec4[2] = fRec4[1];
			fRec4[1] = fRec4[0];
			fRec5[2] = fRec5[1];
			fRec5[1] = fRec5[0];
			fRec6[2] = fRec6[1];
			fRec6[1] = fRec6[0];
			fRec7[2] = fRec7[1];
			fRec7[1] = fRec7[0];
			fRec8[2] = fRec8[1];
			fRec8[1] = fRec8[0];
			fRec9[2] = fRec9[1];
			fRec9[1] = fRec9[0];
			fRec1[2] = fRec1[1];
			fRec1[1] = fRec1[0];
			fRec0[2] = fRec0[1];
			fRec0[1] = fRec0[0];
			fRec42[1] = fRec42[0];
			fRec43[1] = fRec43[0];
			fRec45[2] = fRec45[1];
			fRec45[1] = fRec45[0];
			fRec44[2] = fRec44[1];
			fRec44[1] = fRec44[0];
		}
	}

};

#undef RESTRICT
}
