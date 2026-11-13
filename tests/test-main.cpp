#include "test-framework.hpp"

#include <cstring>

int main(int argc, char **argv)
{
	const char *filter = argc > 1 ? argv[1] : nullptr;
	int run = 0, failedCases = 0;
	for (const vt::Case &c : vt::registry()) {
		if (filter && !std::strstr(c.name, filter))
			continue;
		const int before = vt::failures();
		std::printf("[ RUN  ] %s\n", c.name);
		c.fn();
		++run;
		if (vt::failures() != before) {
			++failedCases;
			std::printf("[ FAIL ] %s\n", c.name);
		} else {
			std::printf("[  OK  ] %s\n", c.name);
		}
	}
	std::printf("\n%d test cases, %d checks, %d failed case(s), %d failed check(s)\n", run, vt::checks(),
		    failedCases, vt::failures());
	return failedCases == 0 ? 0 : 1;
}
