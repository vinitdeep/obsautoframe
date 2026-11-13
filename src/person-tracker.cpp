// V Media AI AutoFrame - lightweight multi-person tracker.
// Copyright (C) 2026 V Media Production. GPL-2.0-or-later.
#include "person-tracker.hpp"

#include <algorithm>
#include <cmath>
#include <tuple>

namespace vmaf {

RectF Track::predict(double now, float maxExtrap) const
{
	const float dt = (float)std::clamp(now - lastUpdate, 0.0, (double)maxExtrap);
	const float h = std::max(1.0f, box.h + vh * dt);
	const float w = box.h > 0.0f ? box.w * (h / box.h) : box.w;
	return RectF::fromCenter(box.cx() + vx * dt, box.cy() + vy * dt, w, h);
}

float PersonTracker::similarity(const RectF &pred, const RectF &det)
{
	const float overlap = iou(pred, det);
	const float hmax = std::max(pred.h, det.h);
	const float hmin = std::min(pred.h, det.h);
	if (hmax <= 0.0f)
		return overlap;
	const float heightRatio = hmin / hmax;
	if (heightRatio < 0.5f)
		return overlap; // very different sizes: only trust real overlap
	// Motion gate: at low detection rates a walking person may move more than their own
	// width between detections, so IoU alone can drop to 0. Accept nearby boxes of similar
	// size with a reduced score; IoU still wins whenever it is available.
	const float d = std::hypot(pred.cx() - det.cx(), pred.cy() - det.cy()) / hmax;
	const float motion = d < 0.8f ? 0.35f * (1.0f - d / 0.8f) * heightRatio : 0.0f;
	return std::max(overlap, motion);
}

void PersonTracker::reset()
{
	tracks_.clear();
	nextId_ = 1;
	lastTs_ = -1.0;
	firstFrame_ = true;
}

const Track *PersonTracker::find(int id) const
{
	for (const Track &t : tracks_)
		if (t.id == id)
			return &t;
	return nullptr;
}

namespace {

using Pair = std::tuple<float, size_t, size_t>; // similarity, track index, detection index

/* Greedy best-first assignment. With a handful of people per frame this matches the
 * Hungarian result in practice and is trivially fast. */
void greedyMatch(const std::vector<RectF> &preds, const std::vector<size_t> &trackIdx,
		 const std::vector<Detection> &dets, const std::vector<size_t> &detIdx, float minSim,
		 std::vector<bool> &trackUsed, std::vector<bool> &detUsed, std::vector<std::pair<size_t, size_t>> &out)
{
	std::vector<Pair> pairs;
	for (size_t ti : trackIdx) {
		if (trackUsed[ti])
			continue;
		for (size_t di : detIdx) {
			if (detUsed[di])
				continue;
			const float s = PersonTracker::similarity(preds[ti], dets[di].box);
			if (s >= minSim)
				pairs.emplace_back(s, ti, di);
		}
	}
	std::sort(pairs.begin(), pairs.end(), [](const Pair &a, const Pair &b) { return std::get<0>(a) > std::get<0>(b); });
	for (const Pair &p : pairs) {
		const size_t ti = std::get<1>(p), di = std::get<2>(p);
		if (trackUsed[ti] || detUsed[di])
			continue;
		trackUsed[ti] = true;
		detUsed[di] = true;
		out.emplace_back(ti, di);
	}
}

} // namespace

void PersonTracker::update(const std::vector<Detection> &dets, double ts)
{
	std::vector<RectF> preds(tracks_.size());
	for (size_t i = 0; i < tracks_.size(); ++i)
		preds[i] = tracks_[i].predict(ts);

	std::vector<size_t> high, low;
	for (size_t i = 0; i < dets.size(); ++i) {
		if (dets[i].score >= cfg_.highThresh)
			high.push_back(i);
		else if (dets[i].score >= cfg_.lowThresh)
			low.push_back(i);
	}

	std::vector<size_t> established, tentative;
	for (size_t i = 0; i < tracks_.size(); ++i)
		(tracks_[i].state == TrackState::Tentative ? tentative : established).push_back(i);

	std::vector<bool> trackUsed(tracks_.size(), false), detUsed(dets.size(), false);
	std::vector<std::pair<size_t, size_t>> matches;

	// Stage 1: confident detections <-> confirmed/lost tracks
	greedyMatch(preds, established, dets, high, cfg_.matchThresh, trackUsed, detUsed, matches);
	// Stage 2 (ByteTrack): low-confidence detections keep existing tracks alive through
	// partial occlusion (pulpit, music stand, another person) - stricter overlap required.
	greedyMatch(preds, established, dets, low, std::max(cfg_.matchThresh, 0.30f), trackUsed, detUsed, matches);
	// Stage 3: confident detections <-> unconfirmed tracks
	greedyMatch(preds, tentative, dets, high, cfg_.matchThresh, trackUsed, detUsed, matches);

	for (const auto &m : matches) {
		Track &t = tracks_[m.first];
		const Detection &d = dets[m.second];
		const float dt = (float)std::max(1e-3, ts - t.lastUpdate);
		float mvx = (d.box.cx() - t.box.cx()) / dt;
		float mvy = (d.box.cy() - t.box.cy()) / dt;
		float mvh = (d.box.h - t.box.h) / dt;
		// physically plausible limit (~3 body heights per second) rejects glitches
		const float vmax = 3.0f * std::max(d.box.h, 1.0f);
		mvx = std::clamp(mvx, -vmax, vmax);
		mvy = std::clamp(mvy, -vmax, vmax);
		mvh = std::clamp(mvh, -vmax, vmax);
		const float a = t.hits > 1 ? 0.5f : 0.3f;
		t.vx = lerp(t.vx, mvx, a);
		t.vy = lerp(t.vy, mvy, a);
		t.vh = lerp(t.vh, mvh, 0.3f);
		t.box = d.box;
		t.score = d.score;
		t.lastUpdate = ts;
		++t.hits;
		if (t.state == TrackState::Lost || (t.state == TrackState::Tentative && t.hits >= cfg_.minHits))
			t.state = TrackState::Confirmed;
	}

	// Unmatched tracks: tentative ones vanish, confirmed ones become lost, old lost ones expire.
	std::vector<Track> kept;
	kept.reserve(tracks_.size() + high.size());
	for (size_t i = 0; i < tracks_.size(); ++i) {
		Track &t = tracks_[i];
		if (!trackUsed[i]) {
			if (t.state == TrackState::Tentative)
				continue;
			t.state = TrackState::Lost;
			// motion fades out while lost so predictions do not drift away
			t.vx *= 0.5f;
			t.vy *= 0.5f;
			t.vh = 0.0f;
			if (ts - t.lastUpdate > cfg_.maxLostTime)
				continue;
		}
		kept.push_back(t);
	}

	for (size_t di : high) {
		if (detUsed[di])
			continue;
		Track t;
		t.id = nextId_++;
		t.box = dets[di].box;
		t.score = dets[di].score;
		t.hits = 1;
		t.firstSeen = ts;
		t.lastUpdate = ts;
		t.state = (firstFrame_ || cfg_.minHits <= 1) ? TrackState::Confirmed : TrackState::Tentative;
		kept.push_back(t);
	}

	tracks_.swap(kept);
	lastTs_ = ts;
	if (!dets.empty())
		firstFrame_ = false;
}

} // namespace vmaf
