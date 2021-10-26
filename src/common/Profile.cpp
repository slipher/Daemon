#include "Common.h"


static std::map<std::string, std::vector<int64_t> > tickCounts;

int64_t prof::now() {
    return Sys::Milliseconds();
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
        auto toMicros = [](int64_t ticks) {
            return ticks * 1000;
        };
        // HACK: Print all lines as one giant log message so that the whole thing can show up in a screenshot
        std::string message;
        for (const auto& kv: tickCounts) {
            auto& v = kv.second;
            int64_t mn = std::numeric_limits<int64_t>::max(), mx = std::numeric_limits<int64_t>::min(), tot = 0;
            for (int64_t ticks: kv.second) {
                mn = std::min(mn, ticks);
                mx = std::max(mx, ticks);
                tot += ticks;
            }
            if (!message.empty()) {
                message.push_back('\n');
            }
            message += Str::Format("%s N=%d min=%dus max=%dus avg=%dus tot=%dus",
                                   kv.first, v.size(), toMicros(mn), toMicros(mx), toMicros(tot / v.size()), toMicros(tot));
        }
        Print(message);
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
