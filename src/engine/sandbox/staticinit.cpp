#include <emscripten.h>

extern "C" void print_str(const char* ptr, int len);

int x;
struct c {
    c() { print_str("C", 1); ++x; }
};

c d;
c j;

extern "C" {
EMSCRIPTEN_KEEPALIVE
int jj() {
    print_str("jj", 2);
    return x;
}
}