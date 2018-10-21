#ifndef COMMON_PROFILE_H_
#define COMMON_PROFILE_H_

#include "Compiler.h"

struct prof {
    const char* name;
    int64_t start;
    static int64_t now();
    prof(const char* name): name(name), start(now()) { }
    ~prof();
};

#define PROF(name, ...) { prof _prof_##__COUNTER__(#name); __VA_ARGS__; }

#endif // COMMON_PROFILE_H_
