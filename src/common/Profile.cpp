#include "Common.h"

#include <Windows.h>

static std::map<std::string, std::vector<int64_t> > tickCounts;

int64_t prof::now() {
    LARGE_INTEGER count;
    QueryPerformanceCounter(&count);
    return count.QuadPart;
}

prof::~prof() {
    tickCounts[name].push_back(now() - start);
}

class ProfileStatsCmd : public Cmd::StaticCmd
{
public:
    ProfileStatsCmd() : StaticCmd("profstats", 0, "Print profile stats") {}

    void Run(const Cmd::Args&) const OVERRIDE
    {
        LARGE_INTEGER frequency;
        QueryPerformanceFrequency(&frequency);
        auto toMicros = [frequency](int64_t ticks) {
            return ticks * 1000 * 1000 / frequency.QuadPart;
        };
        for (const auto& kv: tickCounts) {
            auto& v = kv.second;
            int64_t mn = std::numeric_limits<int64_t>::max(), mx = std::numeric_limits<int64_t>::min(), tot = 0;
            for (int64_t ticks: kv.second) {
                mn = std::min(mn, ticks);
                mx = std::max(mx, ticks);
                tot += ticks;
            }
            Print("%s N=%d min=%dus max=%dus tot=%dus", kv.first, v.size(), toMicros(mn), toMicros(mx), toMicros(tot));
        }
    }
};
static ProfileStatsCmd profstatsRegistration;
