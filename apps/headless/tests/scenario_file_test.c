/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "scenario_file.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static int
parse_text(const char *text, headless_run_options_t *options)
{
    FILE *file = tmpfile();
    int result;
    assert(file != NULL);
    assert(fputs(text, file) >= 0);
    rewind(file);
    *options = (headless_run_options_t) { 0 };
    options->key_ticks = UINT64_C(2000);
    result = headless_scenario_parse(file, options);
    assert(fclose(file) == 0);
    return result;
}

int
main(void)
{
    headless_run_options_t options;
    const char valid[] =
        "# portable pilot\n"
        "blumach-headless-scenario-v1\n"
        "ticks=12000000\n"
        "key_ticks=2000\n"
        "type=10000000:ver\\n\n"
        "type=10500000:echo pilot\\n\n"
        "expect_frame_crc32=7f7c3f7f\n";

    assert(parse_text(valid, &options));
    assert(options.ticks == UINT64_C(12000000));
    assert(options.key_ticks == UINT64_C(2000));
    assert(options.text_action_count == 2U);
    assert(options.text_actions[0].at == UINT64_C(10000000));
    assert(strcmp(options.text_actions[0].text, "ver\\n") == 0);
    assert(options.text_actions[1].at == UINT64_C(10500000));
    assert(strcmp(options.text_actions[1].text, "echo pilot\\n") == 0);
    assert(options.expect_frame_crc32);
    assert(options.expected_frame_crc32 == UINT32_C(0x7f7c3f7f));

    assert(parse_text("blumach-headless-scenario-v2\n"
                      "ticks=10000\nkey_ticks=100\n"
                      "key=1000:left-shift:down\n"
                      "tap=1200:f2\n"
                      "key=1500:left-shift:up\n", &options));
    assert(options.key_action_count == 4U);
    assert(options.key_actions[0].key == BM_KEY_LEFT_SHIFT);
    assert(options.key_actions[0].pressed);
    assert(options.key_actions[1].key == BM_KEY_F2);
    assert(options.key_actions[1].pressed);
    assert(options.key_actions[2].key == BM_KEY_F2);
    assert(!options.key_actions[2].pressed);
    assert(!options.key_actions[3].pressed);

    assert(!parse_text("blumach-headless-scenario-v1\n", &options));
    assert(!parse_text("wrong-header\nticks=10\n", &options));
    assert(!parse_text("blumach-headless-scenario-v1\n"
                       "ticks=100\nticks=200\n", &options));
    assert(!parse_text("blumach-headless-scenario-v1\n"
                       "ticks=300\ntype=100:ab\ntype=130:c\n",
                       &options));
    assert(!parse_text("blumach-headless-scenario-v1\n"
                       "ticks=300\nunknown=value\n", &options));
    assert(!parse_text("blumach-headless-scenario-v1\n"
                       "ticks=300\ntap=100:f2\n", &options));
    assert(!parse_text("blumach-headless-scenario-v2\n"
                       "ticks=300\nkey=100:no-such-key:down\n", &options));
    assert(!parse_text("blumach-headless-scenario-v2\n"
                       "ticks=300\nkey_ticks=100\n"
                       "tap=100:f2\ntap=250:escape\n", &options));
    return 0;
}
