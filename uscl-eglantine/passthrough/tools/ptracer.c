// Lets any process of the same user attach (eu-stack sampling) to the process this is preloaded into.
#include <sys/prctl.h>
__attribute__((constructor)) static void allow_ptracer(void) { prctl(PR_SET_PTRACER, PR_SET_PTRACER_ANY, 0, 0, 0); }
