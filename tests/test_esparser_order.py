"""A parser IRQ is allowed to run synchronously with the fetch-register write."""

import pathlib
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).parents[1]


class ParserOrderTests(unittest.TestCase):
    def test_immediate_completion_is_not_lost(self):
        driver = (ROOT / "vdec/esparser.c").read_text()
        start = driver.index("static int\nesparser_write_data(")
        end = driver.index("\nstatic u32 esparser_vififo", start)
        prefix = r"""
#include <assert.h>
#include <stdint.h>
typedef uint32_t u32; typedef uint32_t dma_addr_t;
struct amvdec_core {int unused;};
#define PFIFO_RD_PTR 0
#define PFIFO_WR_PTR 1
#define PARSER_CONTROL 2
#define PARSER_FETCH_ADDR 3
#define PARSER_FETCH_CMD 4
#define ES_WRITE 1
#define ES_PARSER_START 2
#define ES_SEARCH 4
#define ES_PACK_SIZE_BIT 8
#define FETCH_ENDIAN_BIT 27
#define SEARCH_PATTERN_LEN 512
#define HZ 100
#define WRITE_ONCE(x,v) ((x)=(v))
#define READ_ONCE(x) (x)
#define wait_event_interruptible_timeout(q,condition,t) ((void)(t),(condition)?1:0)
static int search_done;
static void amvdec_write_parser(struct amvdec_core *core,int reg,u32 value){
 (void)core;(void)value;if(reg==PARSER_FETCH_CMD)search_done=1;}
"""
        body = (
            prefix
            + driver[start:end]
            + "\nint main(void){struct amvdec_core c={0};search_done=1;assert(esparser_write_data(&c,0,4096)==1);return 0;}\n"
        )
        with tempfile.TemporaryDirectory() as directory:
            p = pathlib.Path(directory)
            (p / "test.c").write_text(body)
            subprocess.run(
                [
                    "cc",
                    "-Wall",
                    "-Wextra",
                    "-Werror",
                    str(p / "test.c"),
                    "-o",
                    str(p / "test"),
                ],
                check=True,
            )
            subprocess.run([str(p / "test")], check=True)
