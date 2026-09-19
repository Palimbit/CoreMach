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
    const headless_text_action_t valid_actions[] = {
        { "ab", UINT64_C(100) },
        { "\\n", UINT64_C(200) }
    };
    const headless_text_action_t overlapping_actions[] = {
        { "ab", UINT64_C(100) },
        { "c", UINT64_C(130) }
    };
    const headless_key_action_t key_actions[] = {
        { BM_KEY_LEFT_SHIFT, UINT64_C(160), 1 },
        { BM_KEY_LEFT_SHIFT, UINT64_C(180), 0 }
    };

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
    assert(headless_key_code_from_name("f2", &key.key));
    assert(key.key == BM_KEY_F2);
    assert(headless_key_code_from_name("left-shift", &key.key));
    assert(key.key == BM_KEY_LEFT_SHIFT);
    assert(headless_key_code_from_name("z", &key.key));
    assert(key.key == BM_KEY_Z);
    assert(headless_key_code_from_name("0", &key.key));
    assert(key.key == BM_KEY_0);
    assert(!headless_key_code_from_name("not-a-key", &key.key));
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
    assert(headless_text_schedule_validate(valid_actions, 2U, UINT64_C(10),
                                           UINT64_C(300)) == BM_STATUS_OK);
    assert(headless_text_schedule_validate(overlapping_actions, 2U,
                                           UINT64_C(10), UINT64_C(300)) ==
           BM_STATUS_INVALID_ARGUMENT);
    assert(headless_text_schedule_validate(valid_actions, 2U, UINT64_C(10),
                                           UINT64_C(210)) ==
           BM_STATUS_INVALID_ARGUMENT);
    assert(headless_text_schedule_validate(NULL, 1U, UINT64_C(10),
                                           UINT64_C(300)) ==
           BM_STATUS_INVALID_ARGUMENT);
    assert(headless_input_schedule_validate(valid_actions, 1U, key_actions,
                                             2U, UINT64_C(10),
                                             UINT64_C(300)) == BM_STATUS_OK);
    assert(headless_input_schedule_validate(valid_actions, 2U, key_actions,
                                             2U, UINT64_C(10),
                                             UINT64_C(300)) == BM_STATUS_OK);
    assert(headless_input_schedule_validate(overlapping_actions, 2U,
                                             key_actions, 2U,
                                             UINT64_C(10), UINT64_C(300)) ==
           BM_STATUS_INVALID_ARGUMENT);
    return 0;
}
