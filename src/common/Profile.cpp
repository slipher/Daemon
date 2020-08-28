#include "Common.h"

#include <Windows.h>

static std::map<std::string, std::vector<int64_t> > tickCounts;

int64_t prof::now() {
    LARGE_INTEGER count;
    QueryPerformanceCounter(&count);
    return count.QuadPart;
}

prof::~prof() {
    int64_t elapsed = now() - start;
    tickCounts[name].push_back(elapsed);
}

class ProfileStatsCmd : public Cmd::StaticCmd
{
public:
    ProfileStatsCmd() : StaticCmd("profstats", 0, "Print profile stats") {}

    void Run(const Cmd::Args&) const override
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
            Print("%s N=%d min=%dus max=%dus avg=%dus tot=%dus", kv.first, v.size(), toMicros(mn), toMicros(mx), toMicros(tot / v.size()), toMicros(tot));
        }
    }
};
static ProfileStatsCmd profstatsRegistration;

class ProfileStatsClearCmd : public Cmd::StaticCmd
{
public:
    ProfileStatsClearCmd() : StaticCmd("profclear", 0, "Clear profile stats") {}

    void Run(const Cmd::Args&) const override
    {
        tickCounts.clear();
    }
};
static ProfileStatsClearCmd profclearRegistration;
