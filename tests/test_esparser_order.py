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
struct amvdec_core {int esparser_lock;};
#define mutex_lock(p) ((void)(p))
#define mutex_unlock(p) ((void)(p))
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
#define wait_event_interruptible_timeout(q,condition,t) ((void)(t),(condition)?1:wait_result)
static int search_done, complete_now=1, fetch_stopped, wait_result;
static void amvdec_write_parser(struct amvdec_core *core,int reg,u32 value){
 (void)core;if(reg==PARSER_FETCH_CMD){if(!value)fetch_stopped++;else if(complete_now)search_done=1;}}
"""
        body = (
            prefix
            + driver[start:end]
            + "\nint main(void){struct amvdec_core c={0};search_done=1;assert(esparser_write_data(&c,0,4096)==1);assert(!fetch_stopped);complete_now=0;assert(esparser_write_data(&c,0,4096)==0);assert(fetch_stopped==1);wait_result=-4;assert(esparser_write_data(&c,0,4096)==-4);assert(fetch_stopped==2);return 0;}\n"
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

    def test_parallel_parser_fetches_do_not_interleave(self):
        driver = (ROOT / "vdec/esparser.c").read_text()
        start = driver.index("static int\nesparser_write_data(")
        end = driver.index("\nstatic u32 esparser_vififo", start)
        prefix = r"""
#include <assert.h>
#include <pthread.h>
#include <stdint.h>
#include <unistd.h>
typedef uint32_t u32;typedef uint32_t dma_addr_t;
struct amvdec_core {pthread_mutex_t esparser_lock;};
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
#define mutex_lock(p) pthread_mutex_lock(p)
#define mutex_unlock(p) pthread_mutex_unlock(p)
static int search_done, active_fetch, complete_fetches;
static int wait_for_irq(void){usleep(5000);assert(active_fetch==1);active_fetch=0;complete_fetches++;return 1;}
#define wait_event_interruptible_timeout(q,c,t) ((void)(c),(void)(t),wait_for_irq())
static void amvdec_write_parser(struct amvdec_core *c,int reg,u32 value){
 (void)c;(void)value;if(reg==PFIFO_RD_PTR){assert(!active_fetch);active_fetch=1;}}
"""
        program = (
            prefix
            + driver[start:end]
            + r"""
static void *submit(void *p){assert(esparser_write_data(p,0,4096)==1);return 0;}
int main(void){struct amvdec_core c={.esparser_lock=PTHREAD_MUTEX_INITIALIZER};pthread_t a,b;
assert(!pthread_create(&a,0,submit,&c));assert(!pthread_create(&b,0,submit,&c));
assert(!pthread_join(a,0));assert(!pthread_join(b,0));assert(complete_fetches==2 && !active_fetch);return 0;}
"""
        )
        with tempfile.TemporaryDirectory() as directory:
            p = pathlib.Path(directory)
            (p / "test.c").write_text(program)
            subprocess.run(
                [
                    "cc",
                    "-Wall",
                    "-Wextra",
                    "-Werror",
                    "-pthread",
                    "-fsanitize=address,undefined",
                    "-fno-pie",
                    "-no-pie",
                    str(p / "test.c"),
                    "-o",
                    str(p / "test"),
                ],
                check=True,
            )
            subprocess.run([str(p / "test")], check=True)
