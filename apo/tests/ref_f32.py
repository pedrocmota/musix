import math, os, struct, subprocess, sys, tempfile
import numpy as np

f32 = np.float32
N = 4096

VC_TABLE = [0, 16383, 16296, 16292, 16264, 16224, 16216, 16164, 16100, 16020, 15880]
PS_TABLE = {-4: (13, -53248, -832), -3: (10, -40960, -640), -2: (7, -28672, -448), -1: (4, -16384, -256),
             0: (0, 0, 0), 1: (-4, 16384, 256), 2: (-8, 32768, 512), 3: (-12, 49152, 768), 4: (-16, 65536, 1024)}
WIN = np.array([f32(math.sin(float(i) * math.pi * 0.0001220703125) * 0.70710678) for i in range(N + 1)], dtype=f32)

def sr_index(rate):
    for i, th in enumerate((48000, 44100, 32000, 24000, 22050, 16000, 12000, 11025, 8000), start=1):
        if rate > th:
            return i
    return 10

def voice_cancel(x, rate):
    a = f32(f32(VC_TABLE[sr_index(rate)]) * f32(6.103515625e-05))
    half = f32(0.5)
    y = np.empty_like(x)
    s = f32(0)
    for n in range(len(x)):
        L, R = x[n, 0], x[n, 1]
        sm = f32(R + L)
        d = f32(L - R)
        m = f32(sm * half)
        t = f32(s - m)
        t = f32(t * a)
        s = f32(m + t)
        y[n, 0] = f32(d + s)
        y[n, 1] = f32(s - d)
    return y

def pitch_shift(x, st):
    a, b, c = PS_TABLE[st]
    if a == 0:
        return x.copy()
    D = -b if a > 0 else b
    k = 0
    pos = 0
    bufL = np.zeros(N, f32); bufR = np.zeros(N, f32)
    q6 = f32(0.015625)
    y = np.empty_like(x)
    for n in range(len(x)):
        pos = (pos + 1) & (N - 1)
        wi = (pos + 0x4F8) & (N - 1)
        bufL[wi] = x[n, 0]; bufR[wi] = x[n, 1]
        fr = D & 63; di = D >> 6
        i1 = (wi - di) & (N - 1); i2 = (wi - c - di) & (N - 1)
        i1m = (i1 - 1) & (N - 1); i2m = (i2 - 1) & (N - 1)
        frac = f32(f32(fr) * q6)
        w2 = WIN[k]; w1 = WIN[N - k]
        hL2 = f32(f32(f32(bufL[i2m] - bufL[i2]) * frac) + bufL[i2])
        hR2 = f32(f32(f32(bufR[i2m] - bufR[i2]) * frac) + bufR[i2])
        hL1 = f32(f32(f32(bufL[i1m] - bufL[i1]) * frac) + bufL[i1])
        hR1 = f32(f32(f32(bufR[i1m] - bufR[i1]) * frac) + bufR[i1])
        y[n, 0] = f32(f32(hL2 * w2) + f32(hL1 * w1))
        y[n, 1] = f32(f32(hR2 * w2) + f32(hR1 * w1))
        k += 1
        D += (k >> 12) * b + a
        k &= N - 1
    return y

def write_wav(path, x, rate):
    data = x.astype('<f4').tobytes()
    with open(path, 'wb') as f:
        f.write(b'RIFF' + struct.pack('<I', 36 + len(data)) + b'WAVE')
        f.write(b'fmt ' + struct.pack('<IHHIIHH', 16, 3, x.shape[1], rate, rate * x.shape[1] * 4, x.shape[1] * 4, 32))
        f.write(b'data' + struct.pack('<I', len(data)) + data)

def read_wav_f32(path):
    with open(path, 'rb') as f:
        d = f.read()
    ch = struct.unpack_from('<H', d, 22)[0]
    pos = 12
    while pos + 8 <= len(d):
        tag = d[pos:pos + 4]; ln = struct.unpack_from('<I', d, pos + 4)[0]
        if tag == b'data':
            return np.frombuffer(d[pos + 8:pos + 8 + ln], dtype='<f4').reshape(-1, ch)
        pos += 8 + ln + (ln & 1)
    raise ValueError('sem data')

def main():
    if len(sys.argv) < 2:
        print("uso: ref_f32.py <caminho de musix_cli.exe>"); sys.exit(2)
    cli = sys.argv[1]
    rate = 48000
    rng = np.random.default_rng(1)
    t = np.arange(rate) / rate
    L = 0.5 * np.sin(2 * np.pi * 1000 * t) + 0.3 * np.sin(2 * np.pi * 300 * t) + 0.2 * np.sin(2 * np.pi * 30 * t) + 0.05 * rng.standard_normal(rate)
    R = 0.5 * np.sin(2 * np.pi * 1000 * t) + 0.2 * np.sin(2 * np.pi * 30 * t) + 0.05 * rng.standard_normal(rate)
    x = np.stack([L, R], 1).astype(f32)

    tmp = tempfile.mkdtemp()
    inp = os.path.join(tmp, 'in.wav'); write_wav(inp, x, rate)
    fails = 0
    cases = [('--vc', lambda s: voice_cancel(s, rate))]
    for st in (-4, -3, -2, -1, 1, 2, 3, 4):
        cases.append((f'--pitch {st}', lambda s, st=st: pitch_shift(s, st)))
    cases.append(('--vc --pitch 2', lambda s: pitch_shift(voice_cancel(s, rate), 2)))
    cases.append(('--vc --pitch -3 --order ps-vc', lambda s: voice_cancel(pitch_shift(s, -3), rate)))
    for args, ref in cases:
        outp = os.path.join(tmp, 'out.wav')
        subprocess.run([cli, inp, outp] + args.split(), check=True, capture_output=True)
        got = read_wav_f32(outp)
        exp = ref(x)
        same = np.array_equal(got.view(np.uint32), exp.view(np.uint32))
        maxdiff = float(np.max(np.abs(got.astype(np.float64) - exp.astype(np.float64))))
        print(f'{args:32s} bit-exato={same}  maxdiff={maxdiff:.3g}')
        if not same:
            fails += 1
    outa = os.path.join(tmp, 'a.wav'); outb = os.path.join(tmp, 'b.wav')
    subprocess.run([cli, inp, outa, '--vc', '--pitch', '3', '--block', '480'], check=True, capture_output=True)
    subprocess.run([cli, inp, outb, '--vc', '--pitch', '3', '--block', '1000'], check=True, capture_output=True)
    same = np.array_equal(read_wav_f32(outa).view(np.uint32), read_wav_f32(outb).view(np.uint32))
    print(f'{"bloco 480 vs 1000":32s} bit-exato={same}')
    fails += 0 if same else 1
    print('FALHAS:', fails)
    sys.exit(1 if fails else 0)

if __name__ == '__main__':
    main()
