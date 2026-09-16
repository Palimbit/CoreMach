/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "text_input.h"

#include <assert.h>
#include <stdint.h>

static headless_text_key_t
decode_one(const char *text)
{
    const char *cursor = text;
    headless_text_key_t key = { 0 };
    assert(headless_text_decode_next(&cursor, &key) == 1);
    assert(*cursor == '\0');
    return key;
}

int
main(void)
{
    const char *invalid = "\\q";
    headless_text_key_t key;
    uint64_t duration = 0U;

    key = decode_one("a");
    assert(key.key == BM_KEY_A && !key.shifted);
    key = decode_one("Z");
    assert(key.key == BM_KEY_Z && key.shifted);
    key = decode_one("!");
    assert(key.key == BM_KEY_1 && key.shifted);
    key = decode_one("\\\\");
    assert(key.key == BM_KEY_BACKSLASH && !key.shifted);
    key = decode_one("\\n");
    assert(key.key == BM_KEY_ENTER && !key.shifted);

    assert(headless_text_decode_next(&invalid, &key) == -1);
    assert(headless_text_duration("ab\\n", UINT64_C(10), &duration) ==
           BM_STATUS_OK);
    assert(duration == UINT64_C(60));
    assert(headless_text_duration("A", UINT64_C(10), &duration) ==
           BM_STATUS_OK);
    assert(duration == UINT64_C(40));
    assert(headless_text_duration("", UINT64_C(10), &duration) ==
           BM_STATUS_INVALID_ARGUMENT);
    assert(headless_text_duration("\\q", UINT64_C(10), &duration) ==
           BM_STATUS_INVALID_ARGUMENT);
    assert(headless_text_duration("a", 0U, &duration) ==
           BM_STATUS_INVALID_ARGUMENT);
    assert(headless_text_duration("a", UINT64_MAX, &duration) ==
           BM_STATUS_CAPACITY_EXCEEDED);
    return 0;
}
