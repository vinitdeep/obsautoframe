// Minimal self-contained test framework (no external dependency).
#pragma once

#include <cmath>
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

namespace vt {

struct Case {
	const char *name;
	void (*fn)();
};

inline std::vector<Case> &registry()
{
	static std::vector<Case> r;
	return r;
}

inline int &failures()
{
	static int n = 0;
	return n;
}

inline int &checks()
{
	static int n = 0;
	return n;
}

struct Registrar {
	Registrar(const char *name, void (*fn)()) { registry().push_back({name, fn}); }
};

inline void fail(const char *file, int line, const std::string &msg)
{
	++failures();
	std::printf("    FAIL %s:%d: %s\n", file, line, msg.c_str());
}

} // namespace vt

#define TEST(name)                                         \
	static void name();                                \
	static vt::Registrar vt_reg_##name(#name, &name); \
	static void name()

#define CHECK(cond)                                         \
	do {                                                \
		++vt::checks();                             \
		if (!(cond))                                \
			vt::fail(__FILE__, __LINE__, #cond); \
	} while (0)

#define CHECK_NEAR(a, b, eps)                                                                              \
	do {                                                                                               \
		++vt::checks();                                                                            \
		const double va_ = (double)(a), vb_ = (double)(b);                                         \
		if (!(std::fabs(va_ - vb_) <= (double)(eps))) {                                           \
			char buf_[256];                                                                    \
			std::snprintf(buf_, sizeof(buf_), "%s = %.6f, expected %s = %.6f (+-%g)", #a, va_, #b, \
				      vb_, (double)(eps));                                                 \
			vt::fail(__FILE__, __LINE__, buf_);                                                \
		}                                                                                          \
	} while (0)

#define CHECK_MSG(cond, ...)                                     \
	do {                                                     \
		++vt::checks();                                  \
		if (!(cond)) {                                   \
			char buf_[512];                          \
			std::snprintf(buf_, sizeof(buf_), __VA_ARGS__); \
			vt::fail(__FILE__, __LINE__, buf_);      \
		}                                                \
	} while (0)
