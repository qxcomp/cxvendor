#!/usr/bin/env python3
"""完整生成 rtdb/<lib>_dyn.c。
签名: gcc -aux-info；符号集: nm -D；类型: 预处理官方头后提取。

本脚本依赖同目录的 gen_tls_dyn.py（先运行它生成 *_wraps.txt 等中间产物）。
生成的 *_dyn.c 固定写到仓库根的 rtdb/，中间目录由 TLS_GEN_OUT 指定。
"""
import os, re, subprocess, sys, pathlib

HERE = os.path.dirname(os.path.abspath(__file__))
REPO_ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)
import gen_tls_dyn as G

OUT = G.OUT
STD_HEADERS = ['stdio.h','stdlib.h','string.h','stdint.h','stddef.h','stdarg.h',
               'time.h','sys/types.h','errno.h','signal.h']

def preprocess(headers, outfile):
    probe = os.path.join(OUT, 'x_probe.c')
    with open(probe, 'w') as f:
        f.write('#define _POSIX_C_SOURCE 200809L\n')
        for h in headers: f.write('#include <%s>\n' % h)
    r = subprocess.run(['gcc','-std=c99','-E','-P',probe], capture_output=True, text=True)
    if r.returncode:
        print('preprocess failed:', r.stderr[:1000], file=sys.stderr); sys.exit(1)
    open(outfile,'w').write(r.stdout)
    return r.stdout

def split_decls(text):
    decls, start, brace, paren = [], 0, 0, 0
    for i, ch in enumerate(text):
        if ch == '{': brace += 1
        elif ch == '}':
            brace -= 1
            # static inline 函数体结束（无分号）：立即结束并丢弃该块，
            # 否则会吞掉紧随其后的 typedef（如 psa_key_bits_t）
            if brace == 0 and text[start:i+1].lstrip().startswith('static'):
                start = i + 1
        elif ch == '(': paren += 1
        elif ch == ')': paren -= 1
        elif ch == ';' and brace == 0 and paren == 0:
            d = text[start:i+1].strip()
            if d: decls.append(d)
            start = i + 1
    return decls

FPTR = re.compile(r'\(\s*\*\s*([A-Za-z_]\w*)\s*\)')
TAIL = re.compile(r'([A-Za-z_]\w*)\s*$')
TAG  = re.compile(r'^(?:struct|union|enum)\s+([A-Za-z_]\w*)')

def decl_name(d):
    # 预处理残留的 #pragma/#line 行会被拼进声明块，先剥离
    d = re.sub(r'^\s*#.*$', '', d, flags=re.M).strip()
    d = re.sub(r'__attribute__\s*\(\(.*?\)\)\s*', '', d)
    if d.startswith('typedef'):
        # 无花括号才是函数指针/函数类型 typedef；struct 内部的 (*cb) 不算
        if '{' not in d:
            m = FPTR.search(d)
            if m: return ('typedef', m.group(1))
            # 函数类型: typedef int gnutls_params_function(gnutls_session_t, ...);
            mf = re.match(r'typedef\s+(.+?)\s+([A-Za-z_]\w*)\s*\(', d)
            if mf: return ('typedef', mf.group(2))
        body = d[7:].strip().rstrip(';').strip()
        m = TAIL.search(body)
        if m: return ('typedef', m.group(1))
    m = TAG.match(d)
    if m: return ('tag', m.group(1))
    return None

def parse_types(text):
    """name -> (kind, decl_text, order)；typedef 优先于 tag 前向声明"""
    out = {}
    for i, d in enumerate(split_decls(text)):
        x = decl_name(d)
        if not x: continue
        kind, k = x
        prev = out.get(k)
        if prev is None or (kind == 'typedef' and prev[0] == 'tag'):
            out[k] = (kind, d, i)
    return out

def lib_types(name, cfg, extra_seed=()):
    libtxt = preprocess(cfg['headers'], os.path.join(OUT, name+'_types.i'))
    systxt = preprocess(STD_HEADERS, os.path.join(OUT, name+'_std.i'))
    sysnames = set(parse_types(systxt))
    alltypes = parse_types(libtxt)
    libonly = {k:v for k,v in alltypes.items() if k not in sysnames}
    wraps = open(os.path.join(OUT, name+'_wraps.txt')).read().splitlines()
    needed = set()
    for line in list(wraps) + list(extra_seed):
        for w in re.findall(r'\b[A-Za-z_]\w*\b', line):
            if w in libonly: needed.add(w)
    changed = True
    while changed:
        changed = False
        for n in list(needed):
            for w in re.findall(r'\b[A-Za-z_]\w*\b', libonly[n][1]):
                if w in libonly and w not in needed:
                    needed.add(w); changed = True
    ordered = sorted(needed, key=lambda k: libonly[k][2])
    return '\n'.join(libonly[k][1] for k in ordered), len(ordered)

def data_defs(name, cfg, data_syms):
    """返回 [(符号名, 定义语句, 'memcpy'|'assign'), ...]。

    有公开声明的按原类型定义；数组和未声明符号用指针承载并赋值同步，
    其余标量/函数指针用 memcpy 同步真实库的值。
    """
    txt = open(os.path.join(OUT, name+'_types.i')).read()
    found = {}
    for d in split_decls(txt):
        body0 = re.sub(r'^\s*#.*$', '', d, flags=re.M).strip()
        if not body0.startswith('extern '): continue
        typ = body0[7:].strip().rstrip(';').strip()
        typ = re.sub(r'__attribute__\s*\(\(.*?\)\)\s*', '', typ).strip()
        typ = re.sub(r'^_SYM_EXPORT\s+', '', typ).strip()
        # 函数指针变量: char *(*sym)(...)；const 属于函数签名，必须保留
        mf = re.search(r'\(\s*\*\s*([A-Za-z_]\w*)\s*\)\s*\(', typ)
        if not mf:
            typ = re.sub(r'\bconst\s+', '', typ).strip()
        if mf:
            sym = mf.group(1)
            if sym in data_syms:
                defline = re.sub(r'\(\s*\*\s*[A-Za-z_]\w*\s*\)\s*\(',
                                 '(*%s)(' % sym, typ, count=1) + ' = {0}'
                found[sym] = (sym, defline, 'memcpy')
            continue
        # 数组: T sym[...] —— 以指针承载，指向真实库中的数组
        ma = re.search(r'\b([A-Za-z_]\w*)\s*\[[^\]]*\]$', typ)
        if ma:
            sym = ma.group(1)
            if sym in data_syms:
                found[sym] = (sym, '%s *%s = NULL' % (typ[:ma.start()].strip(), sym), 'assign')
            continue
        # 普通标量或指针变量: T sym / T *sym
        mv = re.search(r'\b([A-Za-z_]\w*)\s*$', typ)
        if mv:
            sym = mv.group(1)
            if sym in data_syms:
                found[sym] = (sym, '%s = {0}' % typ, 'memcpy')
    # 仅在私有头声明或未声明的导出全局变量：以 void * 承载真实对象地址
    for sym in data_syms:
        if sym not in found:
            found[sym] = (sym, 'void *%s = NULL' % sym, 'assign')
    return [found[s] for s in sorted(found)], []

PLATFORM = r'''
/* ============================================================
 * 平台适配层：统一动态库 API 与默认库名
 * ============================================================ */
#ifdef _WIN32
 #include <windows.h>
 typedef HMODULE lib_handle_t;
 static lib_handle_t load_lib(const char *path) { return LoadLibraryA(path); }
 static void *get_sym(lib_handle_t h, const char *name) {
     return (void *)GetProcAddress(h, name);
 }
 static void free_lib(lib_handle_t h) { if (h) FreeLibrary(h); }
 static const char *lib_error(void) { return "LoadLibrary failed"; }
 #define PATH_SEP ';'
#else
 #include <dlfcn.h>
 typedef void *lib_handle_t;
 static lib_handle_t load_lib(const char *path) {
     return dlopen(path, RTLD_LAZY | RTLD_LOCAL);
 }
 static void *get_sym(lib_handle_t h, const char *name) { return dlsym(h, name); }
 static void free_lib(lib_handle_t h) { if (h) dlclose(h); }
 static const char *lib_error(void) { return dlerror(); }
 #define PATH_SEP ':'
#endif
'''

LOADER = r'''
/* ============================================================
 * 全局状态与库加载
 * ============================================================ */
static lib_handle_t LIB_handle = NULL;
static int LIB_init_state = 0;

#define MAX_CANDIDATES 64
#define PATH_BUF_LEN   1024

static const char *LIB_defaults[] = {
DEFAULT_LIST
};

static int LIB_do_load(const char *library_path) {
    if (LIB_handle) return 0;
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
                    for (i = 0; i < (int)(sizeof(LIB_defaults)/sizeof(LIB_defaults[0])); i++) {
                        if (n >= MAX_CANDIDATES - 2) break;
                        snprintf(bufs[n], PATH_BUF_LEN, "%s/%s", p, LIB_defaults[i]);
                        candidates[n] = bufs[n]; n++;
                    }
                }
                p = sep ? sep + 1 : NULL;
            }
            free(copy);
        }
    }
    for (i = 0; i < (int)(sizeof(LIB_defaults)/sizeof(LIB_defaults[0])) && n < MAX_CANDIDATES - 1; i++)
        candidates[n++] = LIB_defaults[i];
    const char *home = getenv("HOME");
    if (home && *home && n < MAX_CANDIDATES - 1) {
        snprintf(bufs[n], PATH_BUF_LEN, "%s/.nix-profile/lib/%s", home, LIB_defaults[0]);
        candidates[n] = bufs[n]; n++;
    }
    if (n < MAX_CANDIDATES - 1) {
        snprintf(bufs[n], PATH_BUF_LEN, "/nix/var/nix/profiles/default/lib/%s", LIB_defaults[0]);
        candidates[n] = bufs[n]; n++;
    }
    candidates[n] = NULL;

    for (i = 0; i < n; i++) {
        LIB_handle = load_lib(candidates[i]);
        if (LIB_handle) break;
    }
    if (!LIB_handle) {
        fprintf(stderr, "LIB_dyn: cannot load library (%s)\n", lib_error());
        return -1;
    }
    if (!get_sym(LIB_handle, PROBE_SYM)) {
        fprintf(stderr, "LIB_dyn: loaded library does not export %s\n", PROBE_SYM);
        free_lib(LIB_handle); LIB_handle = NULL;
        return -1;
    }
    fprintf(stderr, "LIB_dyn: loaded %d %s\n", i, candidates[i]);
    LIB_dyn_sync_globals();
    return 0;
}

void *LIB_sym(const char *name) { return LIB_handle ? get_sym(LIB_handle, name) : NULL; }

int LIB_libok(void) {
    if (LIB_init_state == 0)
        LIB_init_state = (LIB_do_load(NULL) == 0) ? 1 : -1;
    return (LIB_init_state == 1 && LIB_handle != NULL) ? 1 : 0;
}

int LIB_dyn_init(const char *library_path) {
    if (LIB_do_load(library_path) == 0) { LIB_init_state = 1; return 0; }
    if (LIB_init_state == 0) LIB_init_state = -1;
    return -1;
}

void LIB_dyn_cleanup(void) {
    /* 不卸载已成功加载的库（dlclose 不安全，且函数指针可能仍被持有） */
    if (!LIB_handle) LIB_init_state = 0;
    else if (LIB_init_state == -1) LIB_init_state = 0;
}

int LIB_dyn_has_symbol(const char *name) {
    if (!LIB_libok()) return 0;
    return LIB_sym(name) != NULL;
}
'''

WRAPMAC = r'''
/* ============================================================
 * WRAP 宏：首次调用 dlsym 并缓存；失败打印 stderr 并返回安全值
 * ============================================================ */
#define WRAP_RET(ret, name, params, args, fallback)                        \
ret name params {                                                         \
    static ret (*fn) params = NULL;                                       \
    if (!fn) {                                                            \
        if (!LIB_libok()) return fallback;                                \
        *(void **)(&fn) = LIB_sym(#name);                                 \
        if (!fn) {                                                        \
            fprintf(stderr, "LIB_dyn: dlsym failed: %s\n", #name);        \
            return fallback;                                              \
        }                                                                 \
    }                                                                     \
    return fn args;                                                       \
}

#define WRAP_VOID(name, params, args)                                     \
void name params {                                                        \
    static void (*fn) params = NULL;                                      \
    if (!fn) {                                                            \
        if (!LIB_libok()) return;                                         \
        *(void **)(&fn) = LIB_sym(#name);                                 \
        if (!fn) {                                                        \
            fprintf(stderr, "LIB_dyn: dlsym failed: %s\n", #name);        \
            return;                                                       \
        }                                                                 \
    }                                                                     \
    fn args;                                                              \
}
'''

def generate(name, cfg):
    syms, data = G.exported(cfg)
    protos = G.prototypes(cfg, name)
    wraps, skipped = G.write_wraps(cfg, name, syms, protos)
    datadefs, dataskip = data_defs(name, cfg, data)
    typetext, ntype = lib_types(name, cfg, [d[1] for d in datadefs])

    defaults = ', '.join('"%s"' % s for s in cfg['so_candidates'])
    header = '''/* ============================================================
 * %s_dyn.c — %s 动态加载封装层（公开头文件 API 全量封装）
 *
 * 统计：导出函数 %d / 封装 %d / 跳过 %d / 数据符号 %d / 内联类型 %d
 * 设计要点（与 rtdb/libxml2_dyn.c 一致）：
 *   1. 单文件、无 .h、无 wrapper 层；链接期不链接目标库（只 -ldl）。
 *   2. 每个封装函数首次调用时才 dlsym 并缓存；未调用的 API 不解析。
 *   3. 编译期内联官方头类型（由 gcc -aux-info 核对签名），无目标库依赖。
 *   4. 加载顺序：显式路径 > %s > LD_LIBRARY_PATH > soname > Nix > 平台默认。
 *   5. 数据符号从目标库同步值；const 符号以可写副本承载。
 *   6. 未封装符号（头文件未声明 / 宏或内联 / 未导出）见下方清单。
 * ============================================================ */
''' % (name, name.upper(), len(syms), len(wraps), len(skipped), len(data), ntype, cfg['env'])

    s = [header]
    s.append('#define _POSIX_C_SOURCE 200809L\n#include <stdio.h>\n#include <stdlib.h>\n#include <string.h>\n#include <stdint.h>\n#include <stddef.h>\n#include <stdarg.h>\n#include <errno.h>\n#include <time.h>\n#include <sys/types.h>\n')
    s.append('/* ---------- 官方头文件内联类型 ---------- */\n' + typetext + '\n')
    s.append(PLATFORM)
    s.append('/* ---------- 数据符号副本 ---------- */\n')
    for sym, defline, _ in datadefs:
        s.append('%s;\n' % defline)
    s.append('\n/* 前向声明（LOADER 在下方定义 LIB_sym） */\nvoid *%s_sym(const char *name);\n' % name)
    s.append('\nvoid %s_dyn_sync_globals(void) {\n    void *p;\n    (void)p;\n' % name)
    for sym, defline, mode in datadefs:
        if mode == 'assign':
            s.append('    if ((p = %s_sym("%s")) != NULL) %s = p;\n' % (name, sym, sym))
        else:
            s.append('    if ((p = %s_sym("%s")) != NULL) memcpy(&%s, p, sizeof(%s));\n'
                     % (name, sym, sym, sym))
    s.append('}\n')
    s.append(LOADER.replace('LIB_', name+'_').replace('LIB', name).
             replace('DEFAULT_LIST', defaults).replace('ENV_NAME', '"%s"' % cfg['env']).
             replace('PROBE_SYM', '"%s"' % sorted(syms)[0]))
    s.append(WRAPMAC.replace('LIB_', name+'_').replace('LIB', name))
    s.append('/* ---------- 全量函数封装 ---------- */\n' + '\n'.join(wraps) + '\n')
    if skipped:
        s.append('/* ---------- 未封装（头文件未声明或不可 dlsym，共 %d 个） ---------- */\n' % len(skipped))
        s.append('\n'.join('/* %s */' % x for x in skipped) + '\n')
    if dataskip:
        s.append('/* ---------- 数据符号未封装 ---------- */\n' + '\n'.join('/* %s */' % x for x in dataskip) + '\n')
    out = os.path.join(REPO_ROOT, 'rtdb', name+'_dyn.c')
    open(out, 'w').write(''.join(s))
    print('%s -> %s: %d 行, %d WRAP, %d 数据符号' % (name, out, len(''.join(s).splitlines()), len(wraps), len(datadefs)))

if __name__ == '__main__':
    for name, cfg in G.CFG.items(): generate(name, cfg)
