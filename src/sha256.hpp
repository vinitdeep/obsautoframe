// V Media AI AutoFrame - portable SHA-256 (FIPS 180-4), used to verify bundled models.
// Copyright (C) 2026 V Media Production. GPL-2.0-or-later.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace vmaf {

class Sha256 {
public:
	Sha256();
	void update(const void *data, size_t len);
	std::string hex(); // finalises; lowercase hex digest

private:
	void block(const uint8_t *p);
	uint32_t h_[8];
	uint8_t buf_[64];
	size_t bufLen_ = 0;
	uint64_t total_ = 0;
};

/* SHA-256 of a file (UTF-8 path) as lowercase hex; empty string if it cannot be read. */
std::string sha256File(const std::string &utf8Path);

} // namespace vmaf
