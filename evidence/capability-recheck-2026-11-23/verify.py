#!/usr/bin/env python3
"""Recheck pinned source claims and execute isolated vendor functions on a host."""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import re
import subprocess

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent.parent


def function(source, name):
    match = re.search(r'(?:static\s+inline\s+|static\s+)?(?:void|int)\s+'
                      + re.escape(name) + r'\s*\(', source)
    if not match:
        raise ValueError(f'Missing vendor function: {name}')
    start = source.index('{', match.start())
    end, depth = start + 1, 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[match.start():end]


def compile_run(cache, name, code):
    path = cache / f'{name}.c'
    path.write_text(code)
    binary = cache / name
    subprocess.run(['cc', '-O1', '-g', '-Wall', '-Wextra', '-Werror',
                    '-fsanitize=address,undefined', '-fno-pie', '-no-pie',
                    str(path), '-o', str(binary)], check=True)
    return subprocess.check_output([str(binary)], text=True)


def firmware_check(cache):
    spec = importlib.util.spec_from_file_location(
        'firmware', ROOT / 'scripts/extract-firmware.py')
    firmware = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(firmware)
    outputs = []
    for filename, array in [
        ('vendor/drivers/amlogic/amports/m8/ucode/mpeg4/vmpeg4_mc.c', 'h263_mc'),
        ('vendor/drivers/amlogic/amports/m8/ucode/avs/avs_linux.h', 'MicroCode'),
    ]:
        source = re.sub(r'/\*.*?\*/|//[^\n]*', '',
                        (cache / filename).read_text(), flags=re.S)
        declaration = re.search(r'const\s+u32\s+' + array + r'\[\].*?'
                                r'=\s*\{([^{}]*)\}', source, re.S)
        words = len(declaration[1].strip().rstrip(',').split(','))
        blob = firmware._words(source, words, array, sentinel=False)
        code = '#include <stdint.h>\n#include <stdio.h>\ntypedef uint32_t u32;\n'
        code += declaration[0] + ';\n'
        code += ('int main(void) { for(unsigned i=0;i<sizeof(' + array
                 + ')/sizeof(u32);i++) for(unsigned b=0;b<4;b++) putchar(('
                 + array + '[i]>>(8*b))&255);return ferror(stdout); }\n')
        path = cache / f'{array}-copy.c'
        path.write_text(code)
        binary = cache / f'{array}-copy'
        subprocess.run(['cc', '-Wall', '-Wextra', '-Werror', str(path),
                        '-o', str(binary)], check=True)
        assert subprocess.check_output([str(binary)]) == blob
        image = blob.ljust(16 * 1024, b'\0')
        assert len(image) == 16 * 1024
        (cache / f'{array}-research.bin').write_bytes(image)
        outputs.append({'array': array, 'words': words, 'bytes': len(blob),
                        'padded_sha256': hashlib.sha256(image).hexdigest()})
    return outputs


def ge2d_check(cache):
    header = (cache / 'vendor/include/linux/amlogic/ge2d/ge2d.h').read_text()
    constants = '\n'.join(line for line in header.splitlines()
                          if line.lstrip().startswith(('#define GE2D_', '#define MATRIX_')))
    cpu = (cache / 'vendor/arch/arm/plat-meson/include/plat/cpu.h').read_text()
    cpu_constants = '\n'.join(line for line in cpu.splitlines()
                              if line.startswith('#define MESON_CPU_TYPE_'))
    types = []
    for name in ('ge2d_src1_data_t', 'ge2d_src1_gen_t', 'ge2d_src2_dst_data_t',
                 'ge2d_src2_dst_gen_t', 'ge2d_dp_gen_t'):
        types.append(re.search(r'typedef\s+struct\s*\{[^{}]*\}\s*'
                               + name + r'\s*;', header, re.S)[0])
    gen = (cache / 'vendor/drivers/amlogic/display/ge2d/ge2dgen.c').read_text()
    hw = (cache / 'vendor/drivers/amlogic/display/ge2d/ge2d_hw.c').read_text()
    # Braces in vendor // comments must not affect function extraction.
    hw = re.sub(r'//[^\n]*', '', hw)
    code = '#include <assert.h>\n#include <stdint.h>\n#include <stdio.h>\n'
    code += cpu_constants + '\n#define MESON_CPU_TYPE MESON_CPU_TYPE_MESON8B\n'
    code += constants + '\n' + '\n'.join(types) + '\n'
    code += '''
enum { GE2D_GEN_CTRL0, GE2D_GEN_CTRL1, GE2D_GEN_CTRL2, GE2D_GEN_CTRL3,
       GE2D_SRC1_CANVAS, GE2D_SRC1_DEF_COLOR, GE2D_SRC1_FMT_CTRL,
       GE2D_SRC_OUTSIDE_ALPHA, GE2D_SRC2_DST_CANVAS, GE2D_SRC2_DEF_COLOR, REG_COUNT };
static uint32_t regs[REG_COUNT];
static void write_bits(unsigned r, unsigned v, unsigned s, unsigned n)
{ uint32_t m=((1u<<n)-1)<<s; regs[r]=(regs[r]&~m)|((v<<s)&m); }
#define WRITE_MPEG_REG_BITS(r,v,s,n) write_bits(r,v,s,n)
#define WRITE_MPEG_REG(r,v) (regs[r]=(v))
'''
    code += '\n'.join(function(gen, name) for name in ('_set_src1_format', '_set_dst_format'))
    code += '\n' + '\n'.join(function(hw, name) for name in
                             ('ge2d_set_src1_data', 'ge2d_set_src2_dst_data'))
    code += '''
int main(void) {
 for(unsigned le=0;le<2;le++) {
  ge2d_src1_data_t s={0}; ge2d_src1_gen_t sg={0};
  ge2d_src2_dst_data_t d={0}; ge2d_src2_dst_gen_t dg={0}; ge2d_dp_gen_t dp={0};
  _set_src1_format(&s,&sg,&dp,GE2D_FORMAT_S8_Y,GE2D_FORMAT_S8_Y|(le<<GE2D_ENDIAN_SHIFT));
  _set_dst_format(&d,&dg,&dp,GE2D_FORMAT_S8_Y,GE2D_FORMAT_S8_Y|(le<<GE2D_ENDIAN_SHIFT));
  assert(!dp.conv_matrix_en && !s.sep_en && !s.format && !d.dst_format && !d.dst2_enable);
  regs[GE2D_GEN_CTRL2]=0xffffffff;
  ge2d_set_src1_data(&s); ge2d_set_src2_dst_data(&d);
  assert(((regs[GE2D_GEN_CTRL2]>>7)&1)==0);
  assert(((regs[GE2D_GEN_CTRL2]>>23)&1)==le);
  printf("PASS: S8 src endian=0 dst endian=%u, register bits 7/23, no matrix\\n",le);
 }
 ge2d_src2_dst_data_t d={0}; ge2d_src2_dst_gen_t dg={0}; ge2d_dp_gen_t dp={0};
 _set_dst_format(&d,&dg,&dp,GE2D_FORMAT_M24_NV12,GE2D_FORMAT_M24_NV12|GE2D_LITTLE_ENDIAN);
 assert(d.dst2_enable && d.dst2_pixel_byte_width==1 && d.dst2_discard_mode==15);
 assert(d.dst2_color_map==10 && !dp.conv_matrix_en);
 puts("PASS: vendor Meson8b branch configures NV12 secondary output; pixels untested");
 return 0;
}
'''
    return compile_run(cache, 'ge2d-register-check', code)


def tile_check(cache):
    source = (cache / 'vendor/drivers/amlogic/amports/vh265.c').read_text()
    start = source.index('#define MAX_TILE_COL_NUM')
    end = source.index('static void update_tile_info', start)
    code = '''#include <assert.h>
#include <stdio.h>
#define H265_DEBUG_BUFMGR 1
#define printk(...) ((void)0)
static const unsigned debug=0;
typedef struct { struct { unsigned short tiles_enabled_flag,num_tile_columns_minus1,
 num_tile_rows_minus1,tile_width[4],tile_height[4]; } p; } param_t;
typedef struct { unsigned long buf_start; } buffer_t;
typedef struct { buffer_t sao_vb,sao_abv; } workspace_t;
typedef struct { int tile_enabled,num_tile_col,num_tile_row,pic_w,pic_h;
 workspace_t *work_space_buf; } hevc_stru_t;
'''
    code += source[start:end] + function(source, 'update_tile_info')
    code += '\n' + function(source, 'get_tile_index')
    code += '''
int main(void) {
 workspace_t w={ .sao_vb={0x100000}, .sao_abv={0x200000} };
 hevc_stru_t h={ .work_space_buf=&w }; param_t p={0};
 p.p.tiles_enabled_flag=3; p.p.num_tile_columns_minus1=3; p.p.num_tile_rows_minus1=1;
 update_tile_info(&h,30,17,384,&p);
 int different=0;
 for(int x=0;x<h.num_tile_col;x++) {
  int expected=((x+1)*30)/4-(x*30)/4;
  printf("col %d: vendor=%d reference=%d\\n",x,m_tile[0][x].width,expected);
  different+=m_tile[0][x].width!=expected;
 }
 assert(different==2);
 assert(m_tile[1][3].start_cu_y+m_tile[1][3].height==17);
 assert(get_tile_index(&h,14,30)==2); /* reference column 1 ends at CTU 15 */
 puts("PASS: vendor uniform tile spacing discrepancy reproduced; hardware untested");
 return 0;
}
'''
    return compile_run(cache, 'hevc-tile-check', code)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cache', type=Path, default=ROOT / 'build/capability-recheck-2026-11-23')
    parser.add_argument('--fetch', action='store_true')
    args = parser.parse_args()
    cache = args.cache.resolve()
    cache.mkdir(parents=True, exist_ok=True)
    manifest = json.loads((HERE / 'sources.json').read_text())
    spec = importlib.util.spec_from_file_location('fetcher', ROOT / 'scripts/extract-firmware.py')
    fetcher = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(fetcher)
    for entry in manifest['files']:
        path = cache / entry['file']
        if not path.exists() and args.fetch:
            # Existing fetcher retries transient errors and supports the raw API.
            data = fetcher._fetch(entry['url'])
            if hashlib.sha256(data).hexdigest() != entry['sha256']:
                raise ValueError(f'Download hash mismatch: {entry["file"]}')
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(data)
        data = path.read_bytes()
        if len(data) != entry['bytes'] or hashlib.sha256(data).hexdigest() != entry['sha256']:
            raise ValueError(f'Source hash mismatch: {entry["file"]}')
    tree = json.loads((cache / 'vendor-tree.json').read_text())
    assert not tree['truncated']
    assert (cache / 'vendor/drivers/amlogic/amports/m8b/ucode').read_text() == '../m8/ucode'
    for version in ('', '8'):
        assert 'M2MDEC(h263,' in (cache / f'ffmpeg{version}-dec.c').read_text()
        assert 'AV_CODEC(H263)' in (cache / f'ffmpeg{version}-fmt.c').read_text()
    assert 'HENC_B_PICTURE' in (cache / 'vendor/drivers/amlogic/amports/encoder.h').read_text()
    assert 'HENC_B_PICTURE' not in (cache / 'vendor/drivers/amlogic/amports/encoder.c').read_text()
    result = {'status': 'host-source-checks-passed', 'hardware_tested': False,
              'source_files_verified': len(manifest['files']),
              'firmware_arrays': firmware_check(cache),
              'ge2d_register_check': ge2d_check(cache), 'tile_check': tile_check(cache)}
    (cache / 'host-results.json').write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps(result, indent=2))


if __name__ == '__main__':
    main()
