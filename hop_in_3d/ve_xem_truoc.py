"""Vẽ hình xem trước nắp hộp: nap_hop_xem_truoc.png.

Chạy: python ve_xem_truoc.py
Script chạy tao_nap_hop.py trước (nắp được tạo lại theo các số đang đặt trong đó), rồi vẽ bằng matplotlib: nắp nhìn
từ trên, nhìn từ dưới, nắp nhấc trên miệng hộp, và mặt cắt chỗ gờ gặp thành hộp. Mặt quay lưng bị bỏ (nắp là khối
kín nên đủ đúng), cạnh bị che được giấu bằng cách dò tia.
"""
import math
import os
import struct
import sys

import numpy as np
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
from matplotlib.patches import Polygon, Rectangle
from mpl_toolkits.mplot3d.art3d import Line3DCollection, Poly3DCollection

THU_MUC = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, THU_MUC)
import tao_nap_hop as N  # noqa: E402  chạy luôn tao_nap_hop.py: ghi lại nap_hop.stl và nap_hop.step

RA = os.path.join(THU_MUC, "nap_hop_xem_truoc.png")
DAY_HOP, SAN_HOP = -3.0, 0.0   # mặt dưới hộp và sàn trong lòng hộp (mm, đọc từ hop.step)

TG, PT = [], []
with open(os.path.join(THU_MUC, "nap_hop.stl"), "rb") as f:
    f.read(80)
    n = struct.unpack("<I", f.read(4))[0]
    for _ in range(n):
        d = struct.unpack("<12fH", f.read(50))
        PT.append(d[0:3])
        TG.append([d[3:6], d[6:9], d[9:12]])
PT = np.array(PT)

# cạnh gãy: cạnh giữa 2 tam giác có pháp tuyến lệch nhau
ke = {}
for i, t in enumerate(TG):
    for a, b in ((t[0], t[1]), (t[1], t[2]), (t[2], t[0])):
        ke.setdefault(tuple(sorted((a, b))), []).append(i)
CANH_GAY = [(k, v) for k, v in ke.items() if len(v) == 2 and np.dot(PT[v[0]], PT[v[1]]) < 0.95]


def so(x):
    """Số viết kiểu Việt Nam: 2.4 → '2,4', 4.0 → '4'."""
    return f"{round(x, 2):g}".replace(".", ",")


def mau(n, goc, mat_nhin):
    den = np.array([-0.35, -0.55, 0.76]); den /= np.linalg.norm(den)
    sang = 0.38 + 0.62 * max(0.0, float(np.dot(n, den)))
    c = np.array(goc) * sang + 0.12 * max(0.0, float(np.dot(n, mat_nhin)))
    return tuple(np.clip(c, 0, 1))


def chia_nho(t, toi_da=5.0):
    """Chia tam giác tới khi cạnh dài nhất ≤ toi_da mm: thứ tự vẽ theo độ sâu của matplotlib mới đúng."""
    a, b, c = (np.array(p) for p in t)
    ds = [(a, b), (b, c), (c, a)]
    i = max(range(3), key=lambda k: np.linalg.norm(ds[k][0] - ds[k][1]))
    if np.linalg.norm(ds[i][0] - ds[i][1]) <= toi_da:
        return [t]
    p, q = ds[i]
    r = [x for x in (a, b, c) if not (x is p or x is q)][0]
    m = (p + q) / 2
    return chia_nho((tuple(p), tuple(m), tuple(r)), toi_da) + chia_nho((tuple(m), tuple(q), tuple(r)), toi_da)


def bi_che(p, huong, bo_qua):
    """Tia từ p về phía người xem có cắt tam giác nào của nắp không (Möller–Trumbore)."""
    for i, (a, b, c) in enumerate(TG):
        if i in bo_qua:
            continue
        a, b, c = np.array(a), np.array(b), np.array(c)
        e1, e2 = b - a, c - a
        h = np.cross(huong, e2)
        det = np.dot(e1, h)
        if abs(det) < 1e-9:
            continue
        s = p - a
        u = np.dot(s, h) / det
        if u < 0 or u > 1:
            continue
        q = np.cross(s, e1)
        v = np.dot(huong, q) / det
        if v < 0 or u + v > 1:
            continue
        if np.dot(e2, q) / det > 1e-3:
            return True
    return False


def ve_nap(ax, nang, elev, azim):
    e, a = math.radians(elev), math.radians(azim)
    mat_nhin = np.array([math.cos(e) * math.cos(a), math.cos(e) * math.sin(a), math.sin(e)])
    ds, cs = [], []
    for t, n in zip(TG, PT):
        if np.dot(n, mat_nhin) <= 1e-6:
            continue                                   # mặt quay lưng: bị che, không vẽ
        c = mau(n, (0.30, 0.56, 0.85), mat_nhin)
        for tt in chia_nho(t):
            ds.append([(p[0], p[1], p[2] + nang) for p in tt])
            cs.append(c)
    ax.add_collection3d(Poly3DCollection(ds, facecolors=cs, edgecolors=cs, linewidths=0.3))
    duong = []
    for (p0, p1), v in CANH_GAY:                       # cạnh gãy: chỉ vẽ đoạn không bị nắp che
        if max(np.dot(PT[v[0]], mat_nhin), np.dot(PT[v[1]], mat_nhin)) <= 1e-6:
            continue
        p0, p1 = np.array(p0), np.array(p1)
        mau_thu = [p0 + (p1 - p0) * k / 20 for k in range(21)]
        thay = [not bi_che(p, mat_nhin, set(v)) for p in mau_thu]
        for k in range(20):
            if thay[k] and thay[k + 1]:
                duong.append([(*(mau_thu[k][:2]), mau_thu[k][2] + nang), (*(mau_thu[k + 1][:2]), mau_thu[k + 1][2] + nang)])
    ax.add_collection3d(Line3DCollection(duong, colors="#16365f", linewidths=0.8))


def ve_hop(ax):
    o0, o1 = N.hcn(N.NGOAI_X, N.NGOAI_Y, DAY_HOP), N.hcn(N.NGOAI_X, N.NGOAI_Y, N.MIENG_Z)
    i0, i1 = N.hcn(N.TRONG_X, N.TRONG_Y, SAN_HOP), N.hcn(N.TRONG_X, N.TRONG_Y, N.MIENG_Z)
    duong = []
    for p in (o0, o1, i0, i1):
        duong += [[p[k], p[(k + 1) % 4]] for k in range(4)]
    duong += [[o0[k], o1[k]] for k in range(4)] + [[i0[k], i1[k]] for k in range(4)]
    mats = [[o0[k], o0[(k + 1) % 4], o1[(k + 1) % 4], o1[k]] for k in range(4)] + [o0]
    ax.add_collection3d(Poly3DCollection(mats, facecolors=(0.83, 0.80, 0.73, 0.35), edgecolors="none"))
    ax.add_collection3d(Line3DCollection(duong, colors="#6f695c", linewidths=0.9))


def khung(ax, elev, azim, z_lim, tieu_de):
    ax.set_xlim(-66, 66); ax.set_ylim(-66, 66); ax.set_zlim(*z_lim)
    ax.set_box_aspect((132, 132, z_lim[1] - z_lim[0]))
    ax.view_init(elev=elev, azim=azim)
    ax.set_proj_type("ortho")                          # chiếu song song: khớp với phép dò tia ở trên
    ax.set_axis_off()
    ax.set_title(tieu_de, fontsize=12, pad=-4)


fig = plt.figure(figsize=(15, 10), dpi=120)
a = fig.add_subplot(2, 2, 1, projection="3d"); ve_nap(a, 0, 32, -58); khung(a, 32, -58, (25, 45), "Nắp nhìn từ trên")
a = fig.add_subplot(2, 2, 2, projection="3d"); ve_nap(a, 0, -38, -58)
khung(a, -38, -58, (25, 45), "Nắp nhìn từ dưới: gờ chèn vào miệng hộp")
a = fig.add_subplot(2, 2, 3, projection="3d"); ve_hop(a); ve_nap(a, 28, 24, -58)
khung(a, 24, -58, (-5, 70), "Nắp nhấc lên trên miệng hộp")

# mặt cắt thành hộp bên phải (+x): mọi kích thước lấy từ tao_nap_hop.py
xt, xn, z0 = N.TRONG_X[1], N.NGOAI_X[1], N.MIENG_Z   # thành trong, mép ngoài hộp; miệng hộp
gn = xt - N.KHE                                      # mặt ngoài gờ
gt = gn - N.DAY_GO                                   # mặt trong gờ
zg = z0 - N.CAO_GO                                   # đáy gờ
ax = fig.add_subplot(2, 2, 4)
ax.add_patch(Rectangle((xt, z0 - 13), xn - xt, 13.0, facecolor="#cfc8b8", edgecolor="#6f695c", lw=1.2))
ax.add_patch(Polygon([(xn - 13.5, z0), (xn, z0), (xn, z0 + N.DAY_NAP), (xn - 13.5, z0 + N.DAY_NAP)], closed=True,
                     facecolor="#4c8ed9", edgecolor="#1d3f6e", lw=1.2))
ax.add_patch(Polygon([(gt, z0), (gn, z0), (gn, zg + N.VAT), (gn - N.VAT, zg), (gt, zg)], closed=True,
                     facecolor="#4c8ed9", edgecolor="#1d3f6e", lw=1.2))
ax.text((xt + xn) / 2, z0 - 7, f"thành hộp\n{so(xn - xt)} mm", ha="center", va="center", fontsize=10)
ax.text(xn - 9, z0 + N.DAY_NAP / 2, f"tấm nắp {so(N.DAY_NAP)} mm", ha="center", va="center", fontsize=10,
        color="white")
ax.annotate(f"gờ dày {so(N.DAY_GO)} mm, cao {so(N.CAO_GO)} mm", xy=((gt + gn) / 2, z0 - 2),
            xytext=(xn - 13.2, z0 - 4.8), fontsize=10, arrowprops=dict(arrowstyle="->", lw=0.8))
ax.annotate(f"khe {so(N.KHE)} mm", xy=(xt - N.KHE / 2, z0 - 1.4), xytext=(xn + 0.5, z0 + 4.7), fontsize=10,
            arrowprops=dict(arrowstyle="->", lw=0.8))
ax.annotate(f"vát {so(N.VAT)} mm", xy=(gn - N.VAT / 2, zg + N.VAT / 2), xytext=(xn - 8.9, z0 - 8.8), fontsize=10,
            arrowprops=dict(arrowstyle="->", lw=0.8))
ax.annotate("mép nắp bằng mép ngoài hộp", xy=(xn, z0 + N.DAY_NAP), xytext=(xn - 12.7, z0 + 5.8), fontsize=10,
            arrowprops=dict(arrowstyle="->", lw=0.8))
ax.set_xlim(xn - 15.5, xn + 4.5); ax.set_ylim(z0 - 13, z0 + 7); ax.set_aspect("equal")
ax.set_xlabel("mm, từ tâm hộp ra thành phải"); ax.set_ylabel(f"mm, chiều cao (miệng hộp ở {so(z0)})")
ax.set_title("Mặt cắt chỗ nắp gặp thành hộp", fontsize=12)
ax.grid(alpha=0.25)
fig.suptitle(f"Nắp hộp {so(N.NGOAI_X[1] - N.NGOAI_X[0])} × {so(N.NGOAI_Y[1] - N.NGOAI_Y[0])} mm · "
             "in úp mặt trên xuống bàn in, không cần support", fontsize=15)
fig.subplots_adjust(left=0.02, right=0.98, top=0.93, bottom=0.06, wspace=0.06, hspace=0.1)
plt.savefig(RA, facecolor="white")
print("đã ghi", RA)
