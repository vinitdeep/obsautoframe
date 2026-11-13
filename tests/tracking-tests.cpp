// Tracker: ID stability, crossing people, occlusion, low-confidence continuation.
#include "sim.hpp"

using namespace vmaf;

namespace {
TrackerConfig cfg()
{
	TrackerConfig c;
	c.highThresh = 0.4f;
	c.lowThresh = 0.16f;
	c.maxLostTime = 4.0f;
	return c;
}
} // namespace

TEST(tracker_keeps_one_id_for_a_walking_person_even_at_low_detection_rate)
{
	PersonTracker tr;
	tr.configure(cfg());
	// 2.5 detections/s (Low CPU mode), walking 1.4 m/s across a 6 m wide shot of 1920 px:
	// moves ~180 px between detections - about the person's own width.
	double t = 0;
	std::vector<int> ids;
	for (int i = 0; i < 25; ++i, t += 0.4) {
		tr.update({person(200.0f + 180.0f * i, 300, 520)}, t);
		for (const Track &k : tr.tracks())
			if (k.active())
				ids.push_back(k.id);
	}
	CHECK(!ids.empty());
	bool same = true;
	for (int id : ids)
		same = same && id == ids.front();
	CHECK(same);
	CHECK(tr.tracks().size() == 1);
}

TEST(tracker_new_track_needs_confirmation)
{
	PersonTracker tr;
	tr.configure(cfg());
	tr.update({person(500, 300, 500)}, 0.0); // first frame: confirmed immediately (start-up)
	tr.update({person(500, 300, 500), person(1500, 300, 500)}, 0.2);
	int confirmed = 0, tentative = 0;
	for (const Track &k : tr.tracks())
		(k.state == TrackState::Tentative ? tentative : confirmed)++;
	CHECK(confirmed == 1);
	CHECK(tentative == 1);
	// a one-frame false positive disappears without ever being confirmed
	tr.update({person(500, 300, 500)}, 0.4);
	CHECK(tr.tracks().size() == 1);
}

TEST(tracker_low_confidence_detections_keep_a_partly_hidden_person)
{
	PersonTracker tr;
	tr.configure(cfg());
	double t = 0;
	for (int i = 0; i < 5; ++i, t += 0.2)
		tr.update({person(900, 300, 500, 0.9f)}, t);
	const int id = tr.tracks().front().id;
	// walks behind the pulpit: detector confidence drops to 0.25 (below the 0.4 threshold)
	for (int i = 0; i < 10; ++i, t += 0.2)
		tr.update({person(900 + 5.0f * i, 300, 500, 0.25f)}, t);
	const Track *k = tr.find(id);
	CHECK(k != nullptr);
	CHECK(k && k->state == TrackState::Confirmed);
	CHECK(tr.tracks().size() == 1);
}

TEST(tracker_two_people_crossing_keep_their_ids)
{
	PersonTracker tr;
	tr.configure(cfg());
	double t = 0;
	int idA = -1, idB = -1;
	for (int i = 0; i <= 30; ++i, t += 0.2) {
		const float ax = 300.0f + 45.0f * i;  // A walks right
		const float bx = 1650.0f - 45.0f * i; // B walks left, slightly further back
		std::vector<Detection> d;
		const bool overlapping = std::fabs(ax - bx) < 120.0f;
		d.push_back(person(ax, 300, 520));
		if (!overlapping) // B hidden behind A while they cross
			d.push_back(person(bx, 330, 470));
		tr.update(d, t);
		if (i == 3) {
			for (const Track &k : tr.tracks()) {
				if (k.box.cx() < 960)
					idA = k.id;
				else
					idB = k.id;
			}
		}
	}
	CHECK(idA > 0 && idB > 0 && idA != idB);
	const Track *a = tr.find(idA);
	const Track *b = tr.find(idB);
	CHECK(a != nullptr);
	CHECK(b != nullptr);
	if (a && b) {
		CHECK_MSG(a->box.cx() > 1400.0f, "A should now be on the right, is at %.0f", a->box.cx());
		CHECK_MSG(b->box.cx() < 600.0f, "B should now be on the left, is at %.0f", b->box.cx());
	}
}

TEST(tracker_lost_track_expires)
{
	PersonTracker tr;
	tr.configure(cfg());
	tr.update({person(500, 300, 500)}, 0.0);
	tr.update({person(505, 300, 500)}, 0.2);
	tr.update({}, 0.4);
	CHECK(tr.tracks().size() == 1);
	CHECK(tr.tracks().front().state == TrackState::Lost);
	tr.update({}, 5.0);
	CHECK(tr.tracks().empty());
}

TEST(tracker_lost_person_reappearing_nearby_gets_old_id_back)
{
	PersonTracker tr;
	tr.configure(cfg());
	double t = 0;
	for (int i = 0; i < 5; ++i, t += 0.2)
		tr.update({person(800, 300, 500)}, t);
	const int id = tr.tracks().front().id;
	for (int i = 0; i < 6; ++i, t += 0.2) // 1.2 s fully hidden
		tr.update({}, t);
	tr.update({person(830, 300, 500)}, t);
	CHECK(tr.find(id) != nullptr);
	CHECK(tr.find(id) && tr.find(id)->state == TrackState::Confirmed);
}
