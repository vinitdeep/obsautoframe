// V Media AI AutoFrame - lightweight multi-person tracker (ByteTrack-style association).
// Copyright (C) 2026 V Media Production. GPL-2.0-or-later.
//
// Tracking IDs are temporary in-memory counters. No appearance features, faces or any
// biometric data are computed or stored: association is purely geometric (box overlap
// and motion), which is what keeps the selected presenter from swapping with a passer-by.
#pragma once

#include "geometry.hpp"

#include <vector>

namespace vmaf {

struct Detection {
	RectF box; // source pixels
	float score = 0.0f;
};

enum class TrackState : int { Tentative = 0, Confirmed = 1, Lost = 2 };

struct Track {
	int id = 0;
	RectF box;               // last matched box (source pixels)
	float vx = 0.0f, vy = 0.0f; // center velocity, px/s
	float vh = 0.0f;         // height velocity, px/s
	float score = 0.0f;
	TrackState state = TrackState::Tentative;
	int hits = 0;
	double firstSeen = 0.0;
	double lastUpdate = 0.0;

	/* Box extrapolated to time `now` (velocity extrapolation capped at maxExtrap seconds). */
	RectF predict(double now, float maxExtrap = 0.6f) const;
	bool active() const { return state == TrackState::Confirmed; }
};

struct TrackerConfig {
	float highThresh = 0.40f;  // detections >= this are "high" (stage 1, may spawn tracks)
	float lowThresh = 0.15f;   // detections in [low, high) only extend existing tracks (stage 2)
	float matchThresh = 0.10f; // minimum association similarity
	int minHits = 2;           // hits before a new track is confirmed
	float maxLostTime = 4.0f;  // seconds a lost track is kept for re-association
};

class PersonTracker {
public:
	void configure(const TrackerConfig &cfg) { cfg_ = cfg; }
	const TrackerConfig &config() const { return cfg_; }

	/* Feed one detector result. `timestamp` = capture time of the analysed frame (seconds). */
	void update(const std::vector<Detection> &dets, double timestamp);

	void reset();

	const std::vector<Track> &tracks() const { return tracks_; }
	const Track *find(int id) const;
	double lastTimestamp() const { return lastTs_; }

	/* Association score between a (predicted) track box and a detection, 0..1. */
	static float similarity(const RectF &pred, const RectF &det);

private:
	TrackerConfig cfg_;
	std::vector<Track> tracks_;
	int nextId_ = 1;
	double lastTs_ = -1.0;
	bool firstFrame_ = true;
};

} // namespace vmaf
