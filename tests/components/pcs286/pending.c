/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <stdio.h>
int main(int argc, char **argv)
{
    fprintf(stderr, "PENDING: %s has no implementation; acceptance NOT executed.\n",
            argc > 1 ? argv[1] : "component");
    return 77;
}
