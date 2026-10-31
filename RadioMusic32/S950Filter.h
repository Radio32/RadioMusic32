#ifndef S950Filter_h
#define S950Filter_h

#include <Arduino.h>
#include <math.h>

// 6-pole Butterworth low-pass (3 cascaded biquads), modelled on the MF6
// switched-capacitor filter in the Akai S900/S950.
//
// Teensy 3.2 has no FPU, so the audio path is integer only. Coefficients are
// computed in float from the main loop, and only when the cutoff changes.
// Samples are int32 with 8 guard bits (16-bit audio << 8) to keep rounding
// noise below the 16-bit floor at low cutoffs.
class S950Filter {
public:
	S950Filter() { setCutoff(0.45f); reset(); }

	// Cutoff as a fraction of the rate this filter runs at (clamped 0.005..0.47)
	void setCutoff(float normalized) {
		if (normalized < 0.005f) normalized = 0.005f;
		if (normalized > 0.47f) normalized = 0.47f;
		if (normalized == cutoff) return;
		cutoff = normalized;

		// Butterworth Q distribution for 6 poles, lowest Q first to limit internal peaking
		static const float Q[3] = { 0.5176f, 0.7071f, 1.9319f };
		const float w = 2.0f * (float)M_PI * normalized;
		const float cs = cosf(w);
		const float sn = sinf(w);
		int32_t c[3][3];
		for (int i = 0; i < 3; i++) {
			const float a0 = 1.0f + sn / (2.0f * Q[i]);
			c[i][0] = toQ30((1.0f - cs) * 0.5f / a0);
			c[i][1] = toQ30(-2.0f * cs / a0);
			c[i][2] = toQ30((1.0f - sn / (2.0f * Q[i])) / a0);
		}
		__disable_irq();
		for (int i = 0; i < 3; i++) {
			stage[i].b0 = c[i][0];
			stage[i].a1 = c[i][1];
			stage[i].a2 = c[i][2];
		}
		__enable_irq();
	}

	void reset() {
		for (int i = 0; i < 3; i++) {
			stage[i].x1 = stage[i].x2 = stage[i].y1 = stage[i].y2 = 0;
		}
	}

	// Direct form 1. Low-pass numerator is b0 * (1, 2, 1).
	inline int32_t process(int32_t x) {
		for (int i = 0; i < 3; i++) {
			Stage& s = stage[i];
			int64_t acc = (int64_t)s.b0 * (x + 2 * s.x1 + s.x2)
					- (int64_t)s.a1 * s.y1
					- (int64_t)s.a2 * s.y2;
			int32_t y = (int32_t)((acc + (1 << 29)) >> 30);
			s.x2 = s.x1; s.x1 = x;
			s.y2 = s.y1; s.y1 = y;
			x = y;
		}
		return x;
	}

private:
	struct Stage {
		int32_t b0, a1, a2;
		int32_t x1, x2, y1, y2;
	};

	static int32_t toQ30(float v) { return (int32_t)lrintf(v * 1073741824.0f); }

	Stage stage[3];
	float cutoff = -1.0f;
};

#endif
