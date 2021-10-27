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

// Internal macros
#define PROF_VARIABLE_NAME_INNER(n) _prof_ ## n
#define PROF_VARIABLE_NAME(n) PROF_VARIABLE_NAME_INNER(n)

// Macros to use
#define PROF(name, ...) { prof PROF_VARIABLE_NAME(__COUNTER__)(#name); __VA_ARGS__; }
#define PROFC(name, cond, ...) { auto PROF_VARIABLE_NAME(__COUNTER__) = (cond) ? Util::optional<prof>(#name) : Util::nullopt; __VA_ARGS__; }
#define PROFB(name) prof PROF_VARIABLE_NAME(__COUNTER__) (#name);
#define PROFBC(name, cond) auto PROF_VARIABLE_NAME(__COUNTER__) = (cond) ? Util::optional<prof>(#name) : Util::nullopt;

#endif // COMMON_PROFILE_H_
