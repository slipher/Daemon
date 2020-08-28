#ifndef COMMON_PROFILE_H_
#define COMMON_PROFILE_H_

#include "Compiler.h"
#include "Util.h"

struct prof {
    const char* name;
    int64_t start;
    static int64_t now();
    prof(const char* name): name(name), start(now()) { }
    ~prof();
};

#define PROF(name, ...) { prof _prof_##__COUNTER__(#name); __VA_ARGS__; }
#define PROFC(name, cond, ...) { auto _prof_##__COUNTER__ = (cond) ? Util::optional<prof>(#name) : Util::nullopt; __VA_ARGS__; }
#define PROFB(name) prof _prof_##__COUNTER__(#name);
#define PROFBC(name, cond) auto _prof_##__COUNTER__ = (cond) ? Util::optional<prof>(#name) : Util::nullopt;

#endif // COMMON_PROFILE_H_
