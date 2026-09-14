import math

# =========================================================
# ZEPHYR v2 CPU VALIDATION SIMULATOR
# Matches the GPU shader math exactly for verification.
# =========================================================

# Earth baseline (km)
Rg = 6371.0
Rt = Rg + 60.0
ShellKm = Rt - Rg

# Rayleigh scattering coefficients (km^-1) at 680/550/440 nm
SIGR = (0.0058, 0.0135, 0.0331)
HR = 8.0  # Rayleigh scale height (km)

# Mie scattering coefficients (km^-1) - neutral baseline
SIGM = (0.004, 0.004, 0.004)
HM = 1.2  # Mie scale height (km)
G = 0.76  # Mie anisotropy

# Ozone-like absorption (Chappuis band)
SIGA = (0.00065, 0.001881, 0.000085)
AC = 25.0  # Absorption layer center (km)
AW = 15.0  # Absorption layer width (km)

# Ground albedo
ALBEDO = 0.3

# Display exposure (replaces ZEPHYR_RADIANCE_SCALE)
EXPOSURE = 1.0

# Multi-scatter scale (1.0 = full multiple scattering)
MS_SCALE = 1.0

# Mie density scale (pressure-decoupled)
MIE_DENSITY_SCALE = 1.0

# Density profiles
def densR(a):
    return math.exp(-max(a, 0.0) / HR)

def densM(a):
    return math.exp(-max(a, 0.0) / HM) * MIE_DENSITY_SCALE

def absorb(a):
    g = math.exp(-((a - AC) / AW) ** 2)
    return (SIGA[0] * g, SIGA[1] * g, SIGA[2] * g)

def ext(a):
    r = densR(a)
    m = densM(a)
    ab = absorb(a)
    return (SIGR[0]*r + SIGM[0]*m + ab[0],
            SIGR[1]*r + SIGM[1]*m + ab[1],
            SIGR[2]*r + SIGM[2]*m + ab[2])

# Ray-sphere intersection
def ray_sphere(ro, rd, c, r):
    ox = ro[0] - c[0]; oy = ro[1] - c[1]; oz = ro[2] - c[2]
    b = ox*rd[0] + oy*rd[1] + oz*rd[2]
    cc = ox*ox + oy*oy + oz*oz - r*r
    h = b*b - cc
    if h < 0: return None
    h = math.sqrt(h)
    return (-b - h, -b + h)

# Transmittance along sun ray
def transmittance(p, sund):
    tg = ray_sphere(p, sund, (0,0,0), Rg)
    if tg and tg[0] > 0 and sund[1] < 0:
        return (0.0, 0.0, 0.0)
    tt = ray_sphere(p, sund, (0,0,0), Rt)
    if not tt or tt[1] <= 0:
        return (1.0, 1.0, 1.0)
    L = tt[1]
    N = 40
    od = [0.0, 0.0, 0.0]
    for i in range(N):
        s = L * (i + 0.5) / N
        q = (p[0] + sund[0]*s, p[1] + sund[1]*s, p[2] + sund[2]*s)
        a = math.sqrt(q[0]*q[0] + q[1]*q[1] + q[2]*q[2]) - Rg
        e = ext(max(a, 0.0))
        st = L / N
        od[0] += e[0] * st
        od[1] += e[1] * st
        od[2] += e[2] * st
    return (math.exp(-od[0]), math.exp(-od[1]), math.exp(-od[2]))

# Phase functions (physically normalized over 4π)
def phR(cosT):
    return (3.0 / (16.0 * math.pi)) * (1.0 + cosT * cosT)

def phM(cosT):
    g = G
    g2 = g * g
    denom = 1.0 + g2 - 2.0 * g * cosT
    return (1.0 / (4.0 * math.pi)) * (1.0 - g2) / (max(denom, 1e-4) ** 1.5)

# Multiple scattering source at point p (dual-scattering approximation)
def ms_source(p, sund):
    # Fibonacci sphere directions (8 dirs like GPU)
    dirs = []
    for i in range(8):
        k = 8.0
        golden = 2.39996322972865332  # pi * (3 - sqrt(5))
        y = 1.0 - (i + 0.5) * (2.0 / k)
        radius = math.sqrt(max(0.0, 1.0 - y * y))
        theta = golden * i
        dirs.append((math.cos(theta) * radius, y, math.sin(theta) * radius))

    acc = [0.0, 0.0, 0.0]
    # Gather length: 3 * Rayleigh scale height (transport scale)
    GL = 3.0 * HR
    NS = 8  # steps per direction
    sl = GL / NS

    for om in dirs:
        g = [0.0, 0.0, 0.0]
        for i in range(NS):
            s = sl * (i + 0.5)
            q = (p[0] + om[0]*s, p[1] + om[1]*s, p[2] + om[2]*s)
            rq = math.sqrt(q[0]*q[0] + q[1]*q[1] + q[2]*q[2])
            if rq > Rt or rq < Rg:
                break
            a = rq - Rg
            ct = max(-1.0, min(1.0, om[0]*sund[0] + om[1]*sund[1] + om[2]*sund[2]))
            ts = transmittance(q, sund)
            j0 = (densR(a)*SIGR[0]*phR(ct) + densM(a)*SIGM[0]*phM(ct)) * ts[0]
            j1 = (densR(a)*SIGR[1]*phR(ct) + densM(a)*SIGM[1]*phM(ct)) * ts[1]
            j2 = (densR(a)*SIGR[2]*phR(ct) + densM(a)*SIGM[2]*phM(ct)) * ts[2]
            e = ext(a)
            seg = math.exp(-(e[0] + e[1] + e[2]) / 3.0 * s)
            g[0] += j0 * seg * sl
            g[1] += j1 * seg * sl
            g[2] += j2 * seg * sl
        acc[0] += g[0]
        acc[1] += g[1]
        acc[2] += g[2]

    acc = [a / 8.0 for a in acc]
    a = math.sqrt(p[0]*p[0] + p[1]*p[1] + p[2]*p[2]) - Rg
    return ((densR(a)*SIGR[0] + densM(a)*SIGM[0]) * acc[0] * MS_SCALE,
            (densR(a)*SIGR[1] + densM(a)*SIGM[1]) * acc[1] * MS_SCALE,
            (densR(a)*SIGR[2] + densM(a)*SIGM[2]) * acc[2] * MS_SCALE)

# Sky radiance integral (matches ZephyrSkyViewCS)
def sky(cam, sund, view):
    tt = ray_sphere(cam, view, (0,0,0), Rt)
    if not tt or tt[1] <= 0:
        return (0,0,0), (0,0,0), (0,0,0), (1,1,1)
    tg = ray_sphere(cam, view, (0,0,0), Rg)
    ex = tt[1]
    if tg and tg[0] > 0:
        ex = min(ex, tg[0])
    en = max(tt[0], 0.0)
    if ex <= en:
        return (0,0,0), (0,0,0), (0,0,0), (1,1,1)

    N = 24
    SS = [0.0, 0.0, 0.0]
    MS = [0.0, 0.0, 0.0]
    T = [1.0, 1.0, 1.0]

    # View-sun angle for phase functions (constant along ray for distant sun)
    ct = max(-1.0, min(1.0, view[0]*sund[0] + view[1]*sund[1] + view[2]*sund[2]))
    pr = phR(ct)
    pm = phM(ct)

    # Quadratic step distribution (clusters near camera)
    for i in range(N):
        mf = (i + 0.5) / N
        s = en + (ex - en) * mf * mf
        cur = (ex - en) * (2*i + 1) / (N * N)
        q = (cam[0] + view[0]*s, cam[1] + view[1]*s, cam[2] + view[2]*s)
        a = math.sqrt(q[0]*q[0] + q[1]*q[1] + q[2]*q[2]) - Rg
        ts = transmittance(q, sund)
        j0 = (densR(a)*SIGR[0]*pr + densM(a)*SIGM[0]*pm) * ts[0]
        j1 = (densR(a)*SIGR[1]*pr + densM(a)*SIGM[1]*pm) * ts[1]
        j2 = (densR(a)*SIGR[2]*pr + densM(a)*SIGM[2]*pm) * ts[2]
        m2 = ms_source(q, sund)
        SS[0] += T[0] * j0 * cur
        SS[1] += T[1] * j1 * cur
        SS[2] += T[2] * j2 * cur
        MS[0] += T[0] * m2[0] * cur
        MS[1] += T[1] * m2[1] * cur
        MS[2] += T[2] * m2[2] * cur
        e = ext(max(a, 0.0))
        T[0] *= math.exp(-e[0] * cur)
        T[1] *= math.exp(-e[1] * cur)
        T[2] *= math.exp(-e[2] * cur)

    # Output LINEAR radiance (no RSC scaling - Exposure applied later)
    return (SS[0]+MS[0], SS[1]+MS[1], SS[2]+MS[2]), tuple(SS), tuple(MS), transmittance(cam, sund)

# Display-referred tonemap (for debugging only - not in shader pipeline)
def tonemap(c):
    return tuple(max(0.0, min(1.0, 1.0 - math.exp(-x * 0.55))) for x in c)

# Test cases
CASES = [
    ("NOON",      90.0),
    ("LOW_SUN",   10.0),
    ("SUNSET",     0.0),
    ("TWILIGHT",  -5.0),
    ("NIGHT",    -30.0),
]

# Camera at 2m above ground
cam = (0.0, Rg + 0.002, 0.0)

out = ["ZEPHYR v2 CPU TRANSPORT SIMULATION (Earth baseline, cam h=2m)"]
out.append("Linear radiance * Exposure, then tonemapped for display")
out.append("=" * 80)

for name, el in CASES:
    er = math.radians(el)
    # Sun direction: elevation angle from horizon
    # el=90 = zenith (0,1,0), el=0 = horizon (1,0,0), el=-90 = nadir (0,-1,0)
    sund = (math.cos(er), math.sin(er), 0.0)
    mus = math.sin(er)

    # View directions: zenith, horizon toward sun, horizon anti-sun
    views = [
        ("ZENITH",    (0, 1, 0)),
        ("HOR_SUN",   (1, 0, 0)),
        ("HOR_ANTI", (-1, 0, 0)),
    ]

    for vname, view in views:
        tot, ss, ms, ts = sky(cam, sund, view)
        lum = 0.2126*tot[0] + 0.7152*tot[1] + 0.0722*tot[2]
        tn = tonemap(tot)
        out.append(
            f"{name:10s} el={el:+6.0f} mu_s={mus:+.3f} {vname:8s}: "
            f"TOT=({tot[0]:.4f},{tot[1]:.4f},{tot[2]:.4f}) "
            f"lum={lum:.4f} "
            f"SS=({ss[0]:.4f},{ss[1]:.4f},{ss[2]:.4f}) "
            f"MS=({ms[0]:.4f},{ms[1]:.4f},{ms[2]:.4f}) "
            f"Tsun=({ts[0]:.4f},{ts[1]:.4f},{ts[2]:.4f}) "
            f"DISP=({tn[0]:.4f},{tn[1]:.4f},{tn[2]:.4f})"
        )

# Write report
with open(r"C:\Users\aless\Documents\Unreal Projects\Andromeda\ZephyrSim_Report.txt", "w") as f:
    f.write("\n".join(out))

# Generate swatches for visual comparison
W, H = 240, 30
rows = []
for name, el in CASES:
    er = math.radians(el)
    sund = (math.cos(er), math.sin(er), 0.0)
    for v in [(0,1,0), (1,0,0), (-1,0,0)]:
        tot, _, _, _ = sky(cam, sund, v)
        tn = tonemap(tot)
        rows.append(tuple(int(x * 255) for x in tn))

ppm = ["P3", f"{W} {H * 15}", "255"]
for r in rows:
    for _ in range(H):
        ppm.append(" ".join(f"{r[0]} {r[1]} {r[2]}" for _ in range(W)))

with open(r"C:\Users\aless\Documents\Unreal Projects\Andromeda\ZephyrSim_Swatches.ppm", "w") as f:
    f.write("\n".join(ppm))

print("\n".join(out))