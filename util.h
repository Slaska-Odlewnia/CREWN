// Shared math, vector, timing and deterministic-noise helpers for CREWN.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

constexpr float PI = 3.14159265358979323846f;
constexpr float TAU = 2.f * PI;

inline float clampf(float value, float low, float high) { return std::max(low, std::min(value, high)); }
inline float sat(float value) { return clampf(value, 0.f, 1.f); }
inline float lerpf(float a, float b, float t) { return a + (b - a) * t; }
inline float approachK(float rate, float dt) { return 1.f - std::exp(-rate * dt); }
inline float smooth01(float t) { t = sat(t); return t * t * (3.f - 2.f * t); }
inline float wrapPi(float angle) { return angle - TAU * std::floor((angle + PI) / TAU); }

struct V3 {
	float x, y, z;
	V3(float X = 0.f, float Y = 0.f, float Z = 0.f) : x(X), y(Y), z(Z) {}
	V3 operator+(const V3& other) const { return V3(x + other.x, y + other.y, z + other.z); }
	V3 operator-(const V3& other) const { return V3(x - other.x, y - other.y, z - other.z); }
	V3 operator*(float scale) const { return V3(x * scale, y * scale, z * scale); }
};

inline float dot3(const V3& a, const V3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline V3 cross3(const V3& a, const V3& b) {
	return V3(a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x);
}
inline float len3(const V3& value) { return std::sqrt(dot3(value, value)); }
inline V3 norm3(const V3& value) {
	float length = len3(value);
	return length > 1e-8f ? value * (1.f / length) : V3();
}

inline uint32_t hash2(int x, int y, uint32_t seed) {
	uint32_t value = seed ^ (static_cast<uint32_t>(x) * 0x8da6b343u) ^ (static_cast<uint32_t>(y) * 0xd8163841u);
	value ^= value >> 16;
	value *= 0x7feb352du;
	value ^= value >> 15;
	value *= 0x846ca68bu;
	value ^= value >> 16;
	return value;
}
inline float nz(int x, int y, uint32_t seed) { return static_cast<float>(hash2(x, y, seed) >> 8) * (1.f / 16777216.f); }

class Rng {
public:
	explicit Rng(uint32_t seed = 1u) : state(seed ? seed : 1u) {}
	uint32_t u() {
		state ^= state << 13;
		state ^= state >> 17;
		state ^= state << 5;
		return state;
	}
	float f() { return static_cast<float>(u() >> 8) * (1.f / 16777216.f); }
	float r(float low, float high) { return lerpf(low, high, f()); }
private:
	uint32_t state;
};

inline std::string fmtTime(float seconds) {
	if (!std::isfinite(seconds) || seconds < 0.f) seconds = 0.f;
	int milliseconds = static_cast<int>(seconds * 1000.f + 0.5f);
	int minutes = milliseconds / 60000;
	int secs = (milliseconds / 1000) % 60;
	int millis = milliseconds % 1000;
	char text[32];
	std::snprintf(text, sizeof(text), "%d:%02d.%03d", minutes, secs, millis);
	return text;
}
