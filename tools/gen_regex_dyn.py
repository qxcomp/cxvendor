#!/usr/bin/env python3
"""生成 rtdb/pcre2_dyn.c 与 rtdb/tre_dyn.c。

符号集来自 nm -D，签名来自 gcc -aux-info；中间产物放在 /tmp/opencode/gen。
pcre2 有 8/16/32/POSIX 四个独立动态库，因此使用多 handle 加载器。
"""
import os
import re
import subprocess
import sys
import pathlib

HERE = os.path.dirname(os.path.abspath(__file__))
REPO_ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)

import gen_tls_dyn as G
import gen_tls_full as F

OUT = G.OUT
PCRE2_LIBS = {
    'g8': '/usr/lib/libpcre2-8.so',
    'g16': '/usr/lib/libpcre2-16.so',
    'g32': '/usr/lib/libpcre2-32.so',
    'posix': '/usr/lib/libpcre2-posix.so',
}
PCRE2_DEFAULTS = {
    'g8': ['libpcre2-8.so.0', 'libpcre2-8.so'],
    'g16': ['libpcre2-16.so.0', 'libpcre2-16.so'],
    'g32': ['libpcre2-32.so.0', 'libpcre2-32.so'],
    'posix': ['libpcre2-posix.so.3', 'libpcre2-posix.so'],
}
PCRE2_ENVS = {
    'g8': 'PCRE2_8_DYN_LIB',
    'g16': 'PCRE2_16_DYN_LIB',
    'g32': 'PCRE2_32_DYN_LIB',
    'posix': 'PCRE2_POSIX_DYN_LIB',
}
GROUP_SUFFIX = {'g8': 'G8', 'g16': 'G16', 'g32': 'G32', 'posix': 'POSIX'}
POSIX_SYMS = {'pcre2_regcomp', 'pcre2_regexec', 'pcre2_regerror', 'pcre2_regfree'}
FB_CFG = dict(ptr_fallback='NULL', unsigned_fallback='0', int_fallback='-1')


def run(cmd, **kw):
    return subprocess.run(cmd, capture_output=True, text=True, **kw)


def parse_aux(auxfile):
    protos = {}
    for line in open(auxfile, errors='ignore'):
        m = re.search(r'extern\s+(.*?)\b([A-Za-z_]\w*)\s*\((.*)\)\s*;', line)
        if not m:
            continue
        protos[m.group(2)] = (m.group(1).strip(), m.group(3).strip())
    return protos


def pcre2_protos(width):
    probe = os.path.join(OUT, 'pcre2_probe_%d.c' % width)
    aux = os.path.join(OUT, 'pcre2_proto_%d.txt' % width)
    with open(probe, 'w') as f:
        f.write('#define PCRE2_CODE_UNIT_WIDTH %d\n' % width)
        f.write('#include <pcre2.h>\n')
        if width == 8:
            f.write('#include <pcre2posix.h>\n')
    r = run(['gcc', '-std=c99', '-aux-info', aux, '-c', probe,
             '-o', os.path.join(OUT, 'pcre2_probe_%d.o' % width)])
    if r.returncode:
        print('pcre2 prototype probe failed:', r.stderr[:2000], file=sys.stderr)
        sys.exit(1)
    return parse_aux(aux)


def exported(lib, group):
    syms = set()
    r = run(['nm', '-D', '--defined-only', lib])
    if r.returncode:
        print('nm failed:', lib, r.stderr[:1000], file=sys.stderr)
        sys.exit(1)
    for line in r.stdout.splitlines():
        p = line.split()
        if len(p) < 3 or p[1] != 'T':
            continue
        name = p[2].split('@')[0]
        if group == 'posix':
            if name in POSIX_SYMS:
                syms.add(name)
        elif name.startswith('pcre2_') and name.endswith('_' + group[1:]):
            syms.add(name)
    return syms


def pcre2_preprocess(width, outfile):
    probe = os.path.join(OUT, 'pcre2_types_probe_%d.c' % width)
    with open(probe, 'w') as f:
        f.write('#define _POSIX_C_SOURCE 200809L\n')
        f.write('#define PCRE2_CODE_UNIT_WIDTH %d\n' % width)
        f.write('#include <pcre2.h>\n')
        if width == 8:
            f.write('#include <pcre2posix.h>\n')
    r = run(['gcc', '-std=c99', '-E', '-P', probe])
    if r.returncode:
        print('pcre2 preprocess failed:', r.stderr[:1000], file=sys.stderr)
        sys.exit(1)
    open(outfile, 'w').write(r.stdout)
    return r.stdout


def pcre2_types(wraps):
    texts = []
    for width in (8, 16, 32):
        texts.append(pcre2_preprocess(width, os.path.join(OUT, 'pcre2_types_%d.i' % width)))
    systxt = F.preprocess(F.STD_HEADERS, os.path.join(OUT, 'pcre2_std_types.i'))
    sysnames = set(F.parse_types(systxt))
    alltypes = {}
    for text in texts:
        for name, val in F.parse_types(text).items():
            if name in sysnames:
                continue
            prev = alltypes.get(name)
            if prev is None or (val[0] == 'typedef' and prev[0] == 'tag'):
                alltypes[name] = val
    needed = set()
    for line in wraps:
        for word in re.findall(r'\b[A-Za-z_]\w*\b', line):
            if word in alltypes:
                needed.add(word)
    changed = True
    while changed:
        changed = False
        for name in list(needed):
            for word in re.findall(r'\b[A-Za-z_]\w*\b', alltypes[name][1]):
                if word in alltypes and word not in needed:
                    needed.add(word)
                    changed = True
    ordered = sorted(needed, key=lambda k: alltypes[k][2])
    return '\n'.join(alltypes[k][1] for k in ordered), len(ordered)


def wraps_for(syms, protos, group):
    lines, skipped = [], []
    suffix = GROUP_SUFFIX[group]
    for fn in sorted(syms):
        if fn not in protos:
            skipped.append(fn + ' (头未声明)')
            continue
        ret, params = protos[fn]
        decl, args = G.rename_params(params)
        fallback = G.fallback(FB_CFG, ret)
        if fallback is None:
            lines.append('WRAP_VOID_%s(%s, (%s), (%s))' % (suffix, fn, decl, args))
        else:
            lines.append('WRAP_RET_%s(%s, %s, (%s), (%s), %s)'
                         % (suffix, ret, fn, decl, args, fallback))
    return lines, skipped


LOADER_GROUP = r'''
static lib_handle_t PCRE_handle_TAG = NULL;
static int PCRE_init_state_TAG = 0;

static const char *PCRE_defaults_TAG[] = {
DEFAULTS
};

static int PCRE_do_load_TAG(const char *library_path) {
    if (PCRE_handle_TAG) return 0;
    static char bufs[MAX_CANDIDATES][PATH_BUF_LEN];
    const char *candidates[MAX_CANDIDATES];
    int n = 0, i;

    if (library_path && *library_path && n < MAX_CANDIDATES - 1) {
        snprintf(bufs[n], PATH_BUF_LEN, "%s", library_path);
        candidates[n] = bufs[n]; n++;
    }
    const char *explicit_lib = getenv(ENV_NAME);
    if (explicit_lib && *explicit_lib && n < MAX_CANDIDATES - 1) {
        snprintf(bufs[n], PATH_BUF_LEN, "%s", explicit_lib);
        candidates[n] = bufs[n]; n++;
    }
    const char *env_path = getenv(
#ifdef _WIN32
        "PATH"
#elif defined(__APPLE__)
        "DYLD_LIBRARY_PATH"
#else
        "LD_LIBRARY_PATH"
#endif
    );
    if (env_path && *env_path) {
        char *copy = strdup(env_path);
        if (copy) {
            char *p = copy;
            while (p && *p && n < MAX_CANDIDATES - 2) {
                char *sep = strchr(p, PATH_SEP);
                if (sep) *sep = '\0';
                if (*p) {
                    for (i = 0; i < (int)(sizeof(PCRE_defaults_TAG)/sizeof(PCRE_defaults_TAG[0])); i++) {
                        if (n >= MAX_CANDIDATES - 2) break;
                        snprintf(bufs[n], PATH_BUF_LEN, "%s/%s", p, PCRE_defaults_TAG[i]);
                        candidates[n] = bufs[n]; n++;
                    }
                }
                p = sep ? sep + 1 : NULL;
            }
            free(copy);
        }
    }
    for (i = 0; i < (int)(sizeof(PCRE_defaults_TAG)/sizeof(PCRE_defaults_TAG[0])) && n < MAX_CANDIDATES - 1; i++)
        candidates[n++] = PCRE_defaults_TAG[i];
    const char *home = getenv("HOME");
    if (home && *home && n < MAX_CANDIDATES - 1) {
        snprintf(bufs[n], PATH_BUF_LEN, "%s/.nix-profile/lib/%s", home, PCRE_defaults_TAG[0]);
        candidates[n] = bufs[n]; n++;
    }
    if (n < MAX_CANDIDATES - 1) {
        snprintf(bufs[n], PATH_BUF_LEN, "/nix/var/nix/profiles/default/lib/%s", PCRE_defaults_TAG[0]);
        candidates[n] = bufs[n]; n++;
    }
    candidates[n] = NULL;

    for (i = 0; i < n; i++) {
        PCRE_handle_TAG = load_lib(candidates[i]);
        if (PCRE_handle_TAG) break;
    }
    if (!PCRE_handle_TAG) {
        fprintf(stderr, "pcre2_dyn: cannot load %s library (%s)\n", "TAG", lib_error());
        return -1;
    }
    if (!get_sym(PCRE_handle_TAG, PROBE_SYM)) {
        fprintf(stderr, "pcre2_dyn: %s library does not export %s\n", "TAG", PROBE_SYM);
        free_lib(PCRE_handle_TAG); PCRE_handle_TAG = NULL;
        return -1;
    }
    fprintf(stderr, "pcre2_dyn: loaded %s %d %s\n", "TAG", i, candidates[i]);
    return 0;
}

void *PCRE_sym_TAG(const char *name) {
    return PCRE_handle_TAG ? get_sym(PCRE_handle_TAG, name) : NULL;
}

int PCRE_libok_TAG(void) {
    if (PCRE_init_state_TAG == 0)
        PCRE_init_state_TAG = (PCRE_do_load_TAG(NULL) == 0) ? 1 : -1;
    return (PCRE_init_state_TAG == 1 && PCRE_handle_TAG != NULL) ? 1 : 0;
}
'''


WRAPMAC_GROUP = r'''
#define WRAP_RET_SUFFIX(ret, name, params, args, fallback)                 \
ret name params {                                                         \
    static ret (*fn) params = NULL;                                       \
    if (!fn) {                                                            \
        if (!PCRE_libok_TAG()) return fallback;                           \
        *(void **)(&fn) = PCRE_sym_TAG(#name);                            \
        if (!fn) {                                                        \
            fprintf(stderr, "pcre2_dyn: dlsym failed: %s\n", #name);      \
            return fallback;                                              \
        }                                                                 \
    }                                                                     \
    return fn args;                                                       \
}

#define WRAP_VOID_SUFFIX(name, params, args)                              \
void name params {                                                        \
    static void (*fn) params = NULL;                                      \
    if (!fn) {                                                            \
        if (!PCRE_libok_TAG()) return;                                    \
        *(void **)(&fn) = PCRE_sym_TAG(#name);                            \
        if (!fn) {                                                        \
            fprintf(stderr, "pcre2_dyn: dlsym failed: %s\n", #name);      \
            return;                                                       \
        }                                                                 \
    }                                                                     \
    fn args;                                                              \
}
'''


def generate_pcre2():
    protos = {}
    for width in (8, 16, 32):
        protos.update(pcre2_protos(width))
    groups = {}
    for group in ('g8', 'g16', 'g32', 'posix'):
        groups[group] = exported(PCRE2_LIBS[group], group)
    all_wraps, all_skipped = [], []
    per_group_wraps = {}
    for group in ('g8', 'g16', 'g32', 'posix'):
        wraps, skipped = wraps_for(groups[group], protos, group)
        per_group_wraps[group] = wraps
        all_wraps.extend(wraps)
        all_skipped.extend(skipped)
    typetext, ntype = pcre2_types(all_wraps)

    n_export = sum(len(v) for v in groups.values())
    header = '''/* ============================================================
 * pcre2_dyn.c — PCRE2 动态加载封装层（公开头文件 API 全量封装）
 *
 * 统计：导出函数 %d / 封装 %d / 跳过 %d / 数据符号 0 / 内联类型 %d
 * 设计要点（与 rtdb/libxml2_dyn.c 一致）：
 *   1. 单文件、无 .h、无 wrapper 层；链接期不链接目标库（只 -ldl）。
 *   2. 8/16/32/POSIX 四个库各自懒加载；每个函数首次调用才 dlsym。
 *   3. 编译期内联官方头类型（由 gcc -aux-info 核对签名），无目标库依赖。
 *   4. 加载顺序：显式路径 > *_DYN_LIB > LD_LIBRARY_PATH > soname > Nix > 平台默认。
 *   5. 未封装符号（头文件未声明 / 宏或内联 / 未导出）见下方清单。
 * ============================================================ */
''' % (n_export, len(all_wraps), len(all_skipped), ntype)

    s = [header]
    s.append('#define _POSIX_C_SOURCE 200809L\n#include <stdio.h>\n#include <stdlib.h>\n#include <string.h>\n#include <stdint.h>\n#include <stddef.h>\n#include <stdarg.h>\n#include <errno.h>\n#include <time.h>\n#include <sys/types.h>\n')
    s.append('/* ---------- 官方头文件内联类型 ---------- */\n' + typetext + '\n')
    s.append(F.PLATFORM)
    s.append('/* ============================================================\n * 多动态库加载器\n * ============================================================ */\n')
    s.append('#define MAX_CANDIDATES 64\n#define PATH_BUF_LEN   1024\n')

    probes = {}
    for group, syms in groups.items():
        probes[group] = sorted(syms)[0] if syms else ''
        defaults = ', '.join('"%s"' % x for x in PCRE2_DEFAULTS[group])
        text = (LOADER_GROUP.replace('TAG', group)
                .replace('PCRE', 'pcre2')
                .replace('SUFFIX', GROUP_SUFFIX[group])
                .replace('DEFAULTS', defaults)
                .replace('ENV_NAME', '"%s"' % PCRE2_ENVS[group])
                .replace('PROBE_SYM', '"%s"' % probes[group]))
        s.append(text)

    s.append('''
int pcre2_libok(void) {
    return (pcre2_libok_g8() && pcre2_libok_g16() &&
            pcre2_libok_g32() && pcre2_libok_posix()) ? 1 : 0;
}

int pcre2_dyn_init(const char *library_path) {
    int rc[4];
    rc[0] = pcre2_do_load_g8(library_path);
    pcre2_init_state_g8 = (rc[0] == 0) ? 1 : -1;
    rc[1] = pcre2_do_load_g16(NULL);
    pcre2_init_state_g16 = (rc[1] == 0) ? 1 : -1;
    rc[2] = pcre2_do_load_g32(NULL);
    pcre2_init_state_g32 = (rc[2] == 0) ? 1 : -1;
    rc[3] = pcre2_do_load_posix(NULL);
    pcre2_init_state_posix = (rc[3] == 0) ? 1 : -1;
    return (rc[0] == 0 && rc[1] == 0 && rc[2] == 0 && rc[3] == 0) ? 0 : -1;
}

void pcre2_dyn_cleanup(void) {
    if (!pcre2_handle_g8) pcre2_init_state_g8 = 0;
    else if (pcre2_init_state_g8 == -1) pcre2_init_state_g8 = 0;
    if (!pcre2_handle_g16) pcre2_init_state_g16 = 0;
    else if (pcre2_init_state_g16 == -1) pcre2_init_state_g16 = 0;
    if (!pcre2_handle_g32) pcre2_init_state_g32 = 0;
    else if (pcre2_init_state_g32 == -1) pcre2_init_state_g32 = 0;
    if (!pcre2_handle_posix) pcre2_init_state_posix = 0;
    else if (pcre2_init_state_posix == -1) pcre2_init_state_posix = 0;
}

int pcre2_dyn_has_symbol(const char *name) {
    if (!pcre2_libok_g8()) return 0;
    return (pcre2_sym_g8(name) != NULL || pcre2_sym_g16(name) != NULL ||
            pcre2_sym_g32(name) != NULL || pcre2_sym_posix(name) != NULL);
}
''')

    s.append('/* ============================================================\n * WRAP 宏：首次调用 dlsym 并缓存；失败打印 stderr 并返回安全值\n * ============================================================ */\n')
    for group in ('g8', 'g16', 'g32', 'posix'):
        text = (WRAPMAC_GROUP.replace('TAG', group)
                .replace('PCRE', 'pcre2')
                .replace('SUFFIX', GROUP_SUFFIX[group]))
        s.append(text)

    s.append('/* ---------- 全量函数封装：8 位 ---------- */\n')
    s.append('\n'.join(per_group_wraps['g8']) + '\n')
    s.append('/* ---------- 全量函数封装：16 位 ---------- */\n')
    s.append('\n'.join(per_group_wraps['g16']) + '\n')
    s.append('/* ---------- 全量函数封装：32 位 ---------- */\n')
    s.append('\n'.join(per_group_wraps['g32']) + '\n')
    s.append('/* ---------- 全量函数封装：POSIX ---------- */\n')
    s.append('\n'.join(per_group_wraps['posix']) + '\n')
    if all_skipped:
        s.append('/* ---------- 未封装（头文件未声明或不可 dlsym，共 %d 个） ---------- */\n' % len(all_skipped))
        s.append('\n'.join('/* %s */' % x for x in all_skipped) + '\n')
    out = os.path.join(REPO_ROOT, 'rtdb', 'pcre2_dyn.c')
    content = ''.join(s)
    open(out, 'w').write(content)
    print('pcre2 -> %s: %d 行, %d WRAP, %d 数据符号, skip %d'
          % (out, len(content.splitlines()), len(all_wraps), 0, len(all_skipped)))


TRE_CFG = dict(
    prefix=r'^tre_',
    headers=['tre/tre.h'],
    libs=['/usr/lib/libtre.so'],
    so_candidates=['libtre.so.5', 'libtre.so', 'libtre.dylib', 'tre.dll'],
    env='TRE_DYN_LIB', tag='tre',
    int_fallback='0', ptr_fallback='NULL', unsigned_fallback='0',
)


def main():
    pathlib.Path(OUT).mkdir(parents=True, exist_ok=True)
    generate_pcre2()

    # F.generate 依赖 gen_tls_dyn.py 的中间件；这里为 tre 单独补齐。
    tre_syms, tre_data = G.exported(TRE_CFG)
    tre_protos = G.prototypes(TRE_CFG, 'tre')
    tre_wraps, tre_skipped = G.write_wraps(TRE_CFG, 'tre', tre_syms, tre_protos)
    open(os.path.join(OUT, 'tre_wraps.txt'), 'w').write('\n'.join(tre_wraps) + '\n')
    open(os.path.join(OUT, 'tre_skip.txt'), 'w').write('\n'.join(tre_skipped) + '\n')
    with open(os.path.join(OUT, 'tre_data.txt'), 'w') as f:
        for key, value in sorted(tre_data.items()):
            f.write('%s %s\n' % (value, key))
    F.preprocess(TRE_CFG['headers'], os.path.join(OUT, 'tre_types.i'))

    # F.generate 的通用替换会把字符串里的 LIB 一起替换掉；用占位符保护路径环境变量名。
    original_loader = F.LOADER
    F.LOADER = (original_loader
                .replace('"LD_LIBRARY_PATH"', '"@LD_PATH@"')
                .replace('"DYLD_LIBRARY_PATH"', '"@DYLD_PATH@"'))
    try:
        F.generate('tre', TRE_CFG)
    finally:
        F.LOADER = original_loader
    tre_out = os.path.join(REPO_ROOT, 'rtdb', 'tre_dyn.c')
    tre_text = open(tre_out).read()
    tre_text = (tre_text.replace('"@LD_PATH@"', '"LD_LIBRARY_PATH"')
                .replace('"@DYLD_PATH@"', '"DYLD_LIBRARY_PATH"'))
    open(tre_out, 'w').write(tre_text)


if __name__ == '__main__':
    main()
