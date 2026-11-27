#!/usr/bin/env python3
"""生成 rtdb/<lib>_dyn.c：全量封装公开头文件声明且实际导出的符号。
签名来自 gcc -aux-info，符号集来自 nm -D；中间产物放在临时目录。

用法（先生成本文件的符号/原型清单，再运行 gen_tls_full.py）：
    python3 tools/gen_tls_dyn.py
    python3 tools/gen_tls_full.py

中间目录默认 /tmp/opencode/gen，可用 TLS_GEN_OUT 覆盖。
"""
import os, re, subprocess, sys, pathlib

OUT = os.environ.get('TLS_GEN_OUT', '/tmp/opencode/gen')
pathlib.Path(OUT).mkdir(parents=True, exist_ok=True)

CFG = {
 'mbedtls': dict(
   prefix=r'^(mbedtls_|psa_)',
   headers=['mbedtls/build_info.h','mbedtls/ssl.h','mbedtls/ssl_ciphersuites.h',
            'mbedtls/ssl_cache.h','mbedtls/ssl_ticket.h','mbedtls/ssl_cookie.h',
            'mbedtls/md.h','mbedtls/pk.h','mbedtls/pkcs7.h',
            'mbedtls/x509.h','mbedtls/x509_crt.h','mbedtls/x509_crl.h','mbedtls/x509_csr.h',
            'mbedtls/asn1.h','mbedtls/asn1write.h','mbedtls/pem.h','mbedtls/oid.h',
            'mbedtls/base64.h','mbedtls/constant_time.h','mbedtls/nist_kw.h','mbedtls/lms.h',
            'mbedtls/platform.h','mbedtls/platform_util.h','mbedtls/platform_time.h',
            'mbedtls/timing.h','mbedtls/threading.h','mbedtls/memory_buffer_alloc.h',
            'mbedtls/net_sockets.h','mbedtls/debug.h','mbedtls/error.h','mbedtls/version.h',
            'mbedtls/compat-3-crypto.h','mbedtls/psa_util.h',
            'psa/crypto.h','psa/crypto_extra.h','psa/crypto_compat.h'],
   libs=['/usr/lib/libmbedtls.so','/usr/lib/libmbedcrypto.so','/usr/lib/libmbedx509.so'],
   so_candidates=['libmbedtls.so.23','libmbedtls.so.14','libmbedtls.so',
                  'libmbedtls.dylib','libmbedtls.dll'],
   env='MBEDTLS_DYN_LIB', tag='mbedtls',
   int_fallback='-1', ptr_fallback='NULL', unsigned_fallback='0',
 ),
 'wolfssl': dict(
   prefix=r'^wolf',
   headers=['wolfssl/options.h','wolfssl/ssl.h','wolfssl/version.h',
            'wolfssl/error-ssl.h','wolfssl/wolfcrypt/types.h',
            'wolfssl/wolfcrypt/hmac.h','wolfssl/wolfcrypt/asn.h'],
   libs=['/usr/lib/libwolfssl.so'],
   so_candidates=['libwolfssl.so.46','libwolfssl.so','libwolfssl.dylib','wolfssl.dll'],
   env='WOLFSSL_DYN_LIB', tag='wolfssl',
   int_fallback='0', ptr_fallback='NULL', unsigned_fallback='0',
 ),
 'gnutls': dict(
   prefix=r'^gnutls_',
   headers=['gnutls/abstract.h','gnutls/compat.h','gnutls/crypto.h','gnutls/dtls.h',
            'gnutls/gnutls.h','gnutls/hpke.h','gnutls/ocsp.h','gnutls/openpgp.h',
            'gnutls/openssl.h','gnutls/pkcs11.h','gnutls/pkcs12.h','gnutls/pkcs7.h',
            'gnutls/self-test.h','gnutls/socket.h','gnutls/system-keys.h',
            'gnutls/tpm.h','gnutls/urls.h','gnutls/x509-ext.h','gnutls/x509.h'],
   libs=['/usr/lib/libgnutls.so.30'],
   so_candidates=['libgnutls.so.30','libgnutls.so','libgnutls.dylib','libgnutls-30.dll'],
   env='GNUTLS_DYN_LIB', tag='gnutls',
   int_fallback='-1', ptr_fallback='NULL', unsigned_fallback='0',
 ),
}

def run(cmd, **kw):
    return subprocess.run(cmd, capture_output=True, text=True, **kw)

def exported(cfg):
    syms, data = set(), {}
    for lib in cfg['libs']:
        r = run(['nm','-D',lib])
        for line in r.stdout.splitlines():
            p = line.split()
            if len(p) < 3: continue
            name = p[2].split('@')[0]
            if not re.match(cfg['prefix'], name): continue
            if p[1] == 'T': syms.add(name)
            elif p[1] in ('D','B','R'): data[name] = p[1]
    return syms, data

def prototypes(cfg, name):
    probe = os.path.join(OUT, 'probe_'+name+'.c')
    with open(probe, 'w') as f:
        for h in cfg['headers']: f.write('#include <%s>\n' % h)
    aux = os.path.join(OUT, 'proto_'+name+'.txt')
    r = run(['gcc','-std=c99','-aux-info',aux,'-c',probe,
             '-o',os.path.join(OUT,'probe_'+name+'.o')])
    if r.returncode:
        print('probe failed for',name,r.stderr[:2000], file=sys.stderr)
        sys.exit(1)
    protos = {}
    for line in open(aux, errors='ignore'):
        # 函数名是紧跟 '(' 的标识符；返回类型可以含 '*'（如 const char *fn(void)）
        m = re.search(r'extern\s+(.*?)\b([A-Za-z_]\w*)\s*\((.*)\)\s*;', line)
        if not m: continue
        ret, fn, params = m.group(1).strip(), m.group(2), m.group(3).strip()
        protos[fn] = (ret, params)
    return protos

def split_params(s):
    if not s or s == 'void': return []
    out, depth, cur = [], 0, ''
    for ch in s:
        if ch in '([': depth += 1
        elif ch in ')]': depth -= 1
        if ch == ',' and depth == 0:
            out.append(cur.strip()); cur = ''
        else: cur += ch
    if cur.strip(): out.append(cur.strip())
    return out

def rename_params(params):
    """返回 (带 aN 名字的参数声明表, 实参表)；保留函数指针参数的内部名字。"""
    ps = split_params(params)
    decl, args = [], []
    for i, p in enumerate(ps):
        if p == '...':
            continue
        # 函数指针参数: void (*cb)(int x) -> void (*aN)(int x)
        m = re.match(r'^(.*\(\s*\*\s*)([A-Za-z_]\w*)(\s*\)\s*\(.*)$', p)
        # aux-info 形式: int (*) (void *, int) -> int (*aN)(void *, int)
        maux = re.match(r'^(.*?\(\s*\*\s*)(\)\s*\(.*)$', p)
        if m:
            np = m.group(1) + 'a%d' % i + m.group(3)
        elif maux:
            np = maux.group(1) + 'a%d' % i + ' ' + maux.group(2)
        else:
            # aux-info 形式: gnutls_params_function (*) -> gnutls_params_function (*aN)
            maux2 = re.match(r'^(.*?\(\s*\*\s*)(\)\s*)$', p)
            if maux2:
                np = maux2.group(1) + 'a%d' % i + maux2.group(2)
            else:
                # aux-info 参数字段只有类型（无参数名），直接追加 aN
                np = p + ' a%d' % i
        decl.append(np); args.append('a%d' % i)
    return ', '.join(decl), ', '.join(args)

def fallback(cfg, ret):
    r = ret.strip()
    if r == 'void': return None
    if '*' in r or re.search(r'\b(Ptr|Handle|Session|CTX|Method)\b', r): return cfg['ptr_fallback']
    if re.search(r'^(unsigned|size_t|uint|bool|u?intptr)', r): return cfg['unsigned_fallback']
    if r in ('int','long','short','ssize_t','intmax_t','pid_t'): return cfg['int_fallback']
    if re.search(r'status', r): return cfg['unsigned_fallback']
    if r == 'void': return None
    # 指针/函数指针 typedef（无 * 但本质是指针）：用类型强制转换得到空值
    return '((%s)0)' % r

def write_wraps(cfg, name, syms, protos):
    lines = []
    skipped = []
    for fn in sorted(syms):
        if fn not in protos:
            skipped.append(fn + ' (头未声明)'); continue
        ret, params = protos[fn]
        # 去掉尾部注释里的参数名干扰（aux-info 的 params 已只含类型）
        decl, args = rename_params(params)
        fb = fallback(cfg, ret)
        if fb is None:
            lines.append('WRAP_VOID(%s, (%s), (%s))' % (fn, decl, args))
        else:
            lines.append('WRAP_RET(%s, %s, (%s), (%s), %s)' % (ret, fn, decl, args, fb))
    return lines, skipped

if __name__ == '__main__':
    for name, cfg in CFG.items():
        syms, data = exported(cfg)
        protos = prototypes(cfg, name)
        wraps, skipped = write_wraps(cfg, name, syms, protos)
        print('%s: 导出函数=%d 原型=%d 生成WRAP=%d 跳过=%d 数据符号=%d'
              % (name, len(syms), len(protos), len(wraps), len(skipped), len(data)))
        with open(os.path.join(OUT, name+'_wraps.txt'),'w') as f:
            f.write('\n'.join(wraps)+'\n')
        with open(os.path.join(OUT, name+'_skip.txt'),'w') as f:
            f.write('\n'.join(skipped)+'\n')
        with open(os.path.join(OUT, name+'_data.txt'),'w') as f:
            for k,v in sorted(data.items()): f.write('%s %s\n' % (v,k))
