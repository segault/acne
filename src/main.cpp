// hello
#include "utah.h"
#include <cstdio>

/*
code:
(out(grnd(cap(47000, in)),res(100, in)))


OUT GRND IDEN INT IN IDEN INT IN

            OUT
        GRND    RES [100]
  CAP [47000]      IN
     IN


cir lpf {
    gnd = cap(47000, in) + res(50, res(50, in));
    out = res(100, in);
}

out = lpf(in);

int f(in):

*/

int main(int argc, char *argv[]) {
    printf("Hello, world.\n");
    return 0;
}
