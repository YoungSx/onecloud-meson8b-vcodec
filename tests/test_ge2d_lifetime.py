"""Production module/device acquisition must pair every successful reference."""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class Ge2dLifetimeTests(unittest.TestCase):
    def test_supplier_get_and_serialized_unavailable_conversion(self):
        source = (ROOT / "ge2d/meson8b-ge2d.c").read_text()
        body = source[source.index("static struct device *ge2d_get_device("):
                      source.index("const struct meson8b_ge2d_api *meson8b_ge2d_get_api(")]
        code = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <errno.h>
#include <stddef.h>
#define EPROBE_DEFER 517
#define ERR_PTR(n) ((void *)(intptr_t)(n))
#define PTR_ERR(p) ((int)(intptr_t)(p))
#define READ_ONCE(x) (x)
#define DL_FLAG_AUTOREMOVE_CONSUMER 1
struct device_node {int unused;};struct device {void *of_node;};
struct platform_device {struct device dev;};
struct meson8b_ge2d {bool ready;int hardware_lock;};
struct meson8b_ge2d_image {int unused;};struct meson8b_ge2d_crop {int unused;};
static struct device_node node;static struct platform_device supplier;static struct meson8b_ge2d core;
static bool has_node,compatible,has_device,has_core,link_ok;
static int node_refs,device_refs,links,locked,commands;
static struct device_node *of_parse_phandle(void *n,const char *p,int i){(void)n;(void)p;(void)i;if(!has_node)return NULL;node_refs++;return &node;}
static bool of_device_is_compatible(struct device_node *n,const char *s){(void)n;(void)s;return compatible;}
static void of_node_put(struct device_node *n){assert(n==&node);node_refs--;}
static struct platform_device *of_find_device_by_node(struct device_node *n){assert(n==&node);if(!has_device)return NULL;device_refs++;return &supplier;}
static struct meson8b_ge2d *platform_get_drvdata(struct platform_device *p){assert(p==&supplier);return has_core?&core:NULL;}
static void put_device(struct device *d){assert(d==&supplier.dev);device_refs--;}
static void *device_link_add(struct device *c,struct device *s,int flags){(void)c;assert(s==&supplier.dev&&flags==1);if(!link_ok)return NULL;links++;return &core;}
static struct meson8b_ge2d *dev_get_drvdata(struct device *d){assert(d==&supplier.dev);return &core;}
static void mutex_lock(int *l){(void)l;assert(!locked);locked=1;}
static void mutex_unlock(int *l){(void)l;assert(locked);locked=0;}
static int ge2d_blit(struct meson8b_ge2d *c,const struct meson8b_ge2d_image *i,const struct meson8b_ge2d_image *o,const struct meson8b_ge2d_crop *r){(void)c;(void)i;(void)o;(void)r;assert(locked);commands++;return 0;}
''' + body + r'''
int main(void){struct device consumer={0};
 assert(PTR_ERR(ge2d_get_device(&consumer))==-ENODEV&&!node_refs&&!device_refs);
 has_node=true;assert(PTR_ERR(ge2d_get_device(&consumer))==-ENODEV&&!node_refs&&!device_refs);
 compatible=true;assert(PTR_ERR(ge2d_get_device(&consumer))==-EPROBE_DEFER&&!node_refs&&!device_refs);
 has_device=true;assert(PTR_ERR(ge2d_get_device(&consumer))==-EPROBE_DEFER&&!device_refs);
 has_core=true;assert(PTR_ERR(ge2d_get_device(&consumer))==-EPROBE_DEFER&&!device_refs);
 core.ready=true;assert(PTR_ERR(ge2d_get_device(&consumer))==-ENOMEM&&!device_refs);
 link_ok=true;struct device *d=ge2d_get_device(&consumer);assert(d==&supplier.dev&&device_refs==1&&links==1&&!node_refs);
 assert(!ge2d_convert(d,NULL,NULL,NULL)&&commands==1&&!locked);
 core.ready=false;assert(ge2d_convert(d,NULL,NULL,NULL)==-ENODEV&&commands==1&&!locked);
 ge2d_put_device(d);assert(!device_refs);return 0;}
'''
        self.compile_run(code)

    def compile_run(self, code):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "test.c").write_text(code)
            subprocess.run(["cc", "-Wall", "-Wextra", "-Werror", "-O1",
                            "-fsanitize=address,undefined", "-fno-pie", "-no-pie",
                            str(root / "test.c"), "-o", str(root / "test")], check=True)
            subprocess.run([str(root / "test")], check=True)

    def test_decoder_optional_api_and_failure_references(self):
        source = (ROOT / "vdec/vdec.c").read_text()
        body = source[source.index("static void vdec_release_ge2d("):
                      source.index("/*\n * Cover the whole canvas")]
        code = r'''
#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#define EPROBE_DEFER 517
struct device {void *of_node;};
struct meson8b_ge2d_api {struct device *(*get)(struct device *);void (*put)(struct device *);};
struct amvdec_core {struct device *dev,*ge2d_dev;const struct meson8b_ge2d_api *ge2d_api;};
static bool capture_ge2d,property,present;
static int symbols,devices,get_error,action_error;
static void (*action)(void *);static void *action_data;
#define ERR_PTR(n) ((void *)(intptr_t)(n))
#define PTR_ERR(p) ((int)(intptr_t)(p))
#define IS_ERR(p) ((uintptr_t)(p)>=(uintptr_t)-4095)
static struct device supplier;
static struct device *get(struct device *d){(void)d;if(get_error)return ERR_PTR(get_error);devices++;return &supplier;}
static void put(struct device *d){assert(d==&supplier);devices--;}
static const struct meson8b_ge2d_api api={get,put};
static const struct meson8b_ge2d_api *meson8b_ge2d_get_api(void){return &api;}
static void *of_find_property(void *node,const char *name,void *len){(void)node;(void)name;(void)len;return property?&supplier:NULL;}
#define symbol_get(name) (present?(symbols++,&name):NULL)
#define symbol_put(name) (symbols--)
static int devm_add_action_or_reset(struct device *d,void (*fn)(void *),void *data){(void)d;
 if(action_error){fn(data);return action_error;}action=fn;action_data=data;return 0;}
''' + body + r'''
int main(void){struct device consumer={0};struct amvdec_core c={.dev=&consumer};
 assert(!vdec_acquire_ge2d(&c)&&!symbols&&!devices);
 capture_ge2d=true;assert(vdec_acquire_ge2d(&c)==-ENODEV&&!symbols);
 property=true;assert(vdec_acquire_ge2d(&c)==-EPROBE_DEFER&&!symbols);
 present=true;get_error=-EPROBE_DEFER;assert(vdec_acquire_ge2d(&c)==-EPROBE_DEFER&&!symbols&&!devices&&!c.ge2d_api);
 get_error=0;action_error=-ENOMEM;assert(vdec_acquire_ge2d(&c)==-ENOMEM&&!symbols&&!devices);
 action_error=0;assert(!vdec_acquire_ge2d(&c)&&symbols==1&&devices==1);
 action(action_data);assert(!symbols&&!devices);
 return 0;}
'''
        self.compile_run(code)
