// V Media AI AutoFrame - YOLOX pre/post-processing (no ONNX Runtime dependency).
// Copyright (C) 2026 V Media Production. GPL-2.0-or-later.
#pragma once

#include "geometry.hpp"
#include "person-tracker.hpp"

#include <cstdint>
#include <vector>

namespace vmaf {

struct YoloxParams {
	int inputW = 416;
	int inputH = 416;
	int numClasses = 80;
	int personClass = 0; // COCO "person"
	float scoreThresh = 0.15f;
	float nmsThresh = 0.45f;
	int maxDetections = 32;
};

/* CPU equivalent of the plugin's GPU letterbox pass (area-average downscale to the
 * top-left, grey 114 padding). Used by the offline harness and the detector tests. */
void letterboxRgba(const uint8_t *src, int srcW, int srcH, int srcStride, uint8_t *dst, const LetterboxMap &lb);

/* Converts a letterboxed RGBA image (inputW x inputH, OBS readback) into the planar
 * float BGR tensor YOLOX expects (values 0..255, no mean/std normalisation). */
void rgbaToBgrPlanar(const uint8_t *rgba, int width, int height, int stride, float *dst);

/* Decodes the raw YOLOX head output [numAnchors x (5 + numClasses)] (strides 8/16/32,
 * grid offsets + exp sizes) into person detections in network-input pixels, then NMS.
 * Returns false if the tensor shape does not match the input size. */
bool decodeYolox(const float *out, int numAnchors, int stride, const YoloxParams &p, std::vector<Detection> &dets);

/* Greedy non-maximum suppression, in place, sorted by score. */
void nms(std::vector<Detection> &dets, float iouThresh, int maxKeep);

} // namespace vmaf
