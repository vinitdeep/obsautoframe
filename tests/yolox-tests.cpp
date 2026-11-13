// YOLOX decoding, NMS and tensor layout (synthetic tensors, no model needed).
#include "test-framework.hpp"
#include "yolox.hpp"

#include <cmath>
#include <vector>

using namespace vmaf;

namespace {
constexpr int kStride = 85;
int anchorCount(int w, int h)
{
	return (w / 8) * (h / 8) + (w / 16) * (h / 16) + (w / 32) * (h / 32);
}
} // namespace

TEST(yolox_decodes_grid_offsets_and_sizes)
{
	const int W = 416, H = 416, N = anchorCount(W, H);
	CHECK(N == 3549);
	std::vector<float> out((size_t)N * kStride, 0.0f);
	// stride-16 level starts after the 52x52 stride-8 grid; take cell (gx=10, gy=5)
	const int idx = 52 * 52 + 5 * 26 + 10;
	float *a = &out[(size_t)idx * kStride];
	a[0] = 0.5f;            // x offset inside the cell
	a[1] = 0.25f;           // y offset
	a[2] = std::log(4.0f);  // w = 4 * 16 = 64
	a[3] = std::log(10.0f); // h = 10 * 16 = 160
	a[4] = 0.9f;            // objectness
	a[5] = 0.8f;            // class 0 = person
	a[5 + 2] = 0.99f;       // "car" score must not matter
	YoloxParams p;
	p.scoreThresh = 0.3f;
	std::vector<Detection> d;
	CHECK(decodeYolox(out.data(), N, kStride, p, d));
	CHECK(d.size() == 1);
	if (d.size() == 1) {
		CHECK_NEAR(d[0].score, 0.72, 1e-5);
		CHECK_NEAR(d[0].box.cx(), (10 + 0.5) * 16, 1e-3);
		CHECK_NEAR(d[0].box.cy(), (5 + 0.25) * 16, 1e-3);
		CHECK_NEAR(d[0].box.w, 64, 1e-3);
		CHECK_NEAR(d[0].box.h, 160, 1e-3);
	}
}

TEST(yolox_ignores_non_person_classes_and_low_scores)
{
	const int N = anchorCount(416, 416);
	std::vector<float> out((size_t)N * kStride, 0.0f);
	out[4] = 0.95f;
	out[5 + 15] = 0.95f; // a cat
	out[kStride + 4] = 0.3f;
	out[kStride + 5] = 0.3f; // person but 0.09 total
	YoloxParams p;
	p.scoreThresh = 0.15f;
	std::vector<Detection> d;
	CHECK(decodeYolox(out.data(), N, kStride, p, d));
	CHECK(d.empty());
}

TEST(yolox_rejects_mismatched_tensor)
{
	std::vector<float> out(100 * kStride, 0.0f);
	YoloxParams p;
	std::vector<Detection> d;
	CHECK(!decodeYolox(out.data(), 100, kStride, p, d));
}

TEST(yolox_supports_other_input_sizes)
{
	YoloxParams p;
	p.inputW = 640;
	p.inputH = 384;
	const int N = anchorCount(640, 384);
	std::vector<float> out((size_t)N * kStride, 0.0f);
	std::vector<Detection> d;
	CHECK(decodeYolox(out.data(), N, kStride, p, d));
}

TEST(nms_suppresses_duplicates_keeps_separate_people)
{
	std::vector<Detection> d = {
		{{100, 100, 50, 150}, 0.9f},
		{{104, 102, 50, 150}, 0.8f}, // duplicate of the first
		{{400, 100, 50, 150}, 0.7f}, // another person
	};
	nms(d, 0.45f, 10);
	CHECK(d.size() == 2);
	CHECK_NEAR(d[0].score, 0.9, 1e-6);
	CHECK_NEAR(d[1].score, 0.7, 1e-6);
}

TEST(rgba_to_bgr_planar_layout)
{
	const uint8_t px[2 * 4] = {10, 20, 30, 255, 40, 50, 60, 255}; // 2x1 image
	float dst[6];
	rgbaToBgrPlanar(px, 2, 1, 8, dst);
	CHECK(dst[0] == 30 && dst[1] == 60); // B plane
	CHECK(dst[2] == 20 && dst[3] == 50); // G plane
	CHECK(dst[4] == 10 && dst[5] == 40); // R plane
}
