#include <stdio.h>

#include "memcached.h"

static void display(const char *name, size_t size) {
    printf("%s\t%d\n", name, (int)size);
}

int main(int argc, char **argv) {

    display("Slab Stats", sizeof(struct slab_stats));
    display("Thread stats",
            sizeof(struct thread_stats)
            - (200 * sizeof(struct slab_stats)));
    display("Global stats", sizeof(struct stats));
    display("Settings", sizeof(struct settings));
    display("Item (no cas)", sizeof(item));
    display("Item (cas)", sizeof(item) + sizeof(uint64_t));
#ifdef EXTSTORE
    display("extstore header", sizeof(item_hdr));
#endif
    printf("----------------------------------------\n");

    display("Thread stats cumulative\t", sizeof(struct thread_stats));

    return 0;
}
