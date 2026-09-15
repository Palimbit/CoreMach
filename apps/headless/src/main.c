/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <blumach/platforms/null_host.h>
#include <blumach/runtime/runtime.h>
#include <blumach/engine/version.h>

#include <stdio.h>
#include <string.h>

int
main(int argc, char **argv)
{
    bm_host_services_t host = bm_null_host_services();
    bm_session_t *session = NULL;

    if ((argc == 2) && (strcmp(argv[1], "--version") == 0)) {
        puts("BluMach portable engine " BM_ENGINE_VERSION);
        return 0;
    }
    if (bm_session_create(&host, &session) != BM_STATUS_OK)
        return 1;
    bm_session_destroy(session);
    puts("BluMach portable engine is ready; no machine was selected.");
    return 0;
}
