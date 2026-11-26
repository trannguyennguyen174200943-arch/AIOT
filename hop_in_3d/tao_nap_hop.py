"""Tạo nắp ấn khít cho hộp `hop.step` (Shapr3D, hở miệng trên): xuất STEP (mở lại trong Shapr3D) và STL (in 3D).

Hộp (đọc từ file STEP): ngoài x -63,4…63,5, y -45,5…45,5 mm; lòng hộp x ±60,5, y ±42,5 mm; miệng hộp ở z = 35 mm.
Nắp dùng cùng toạ độ với hộp: mở 2 file cùng lúc là nắp nằm đúng trên miệng hộp.

Nắp gồm:
  - tấm nắp bằng đúng mép ngoài hộp, dày DAY_NAP;
  - gờ chèn vào lòng hộp, cách thành trong KHE mỗi bên, thành gờ DAY_GO, cao CAO_GO, mép dưới vát VAT cho dễ ấn vào.
In: úp mặt trên của nắp xuống bàn in (gờ hướng lên), không cần support.
Chạy: python tao_nap_hop.py   (sửa các số dưới đây rồi chạy lại nếu nắp chặt hoặc lỏng)
"""
import math
import os
import struct
import sys

sys.stdout.reconfigure(encoding="utf-8")
THU_MUC = os.path.dirname(os.path.abspath(__file__))

# ---- kích thước hộp (mm), đọc từ hop.step ----
NGOAI_X = (-63.4, 63.5)
NGOAI_Y = (-45.5, 45.5)
TRONG_X = (-60.5, 60.5)
TRONG_Y = (-42.5, 42.5)
MIENG_Z = 35.0

# ---- thông số nắp (mm) ----
DAY_NAP = 2.4   # độ dày tấm nắp
KHE = 0.2       # khe hở giữa gờ và thành trong hộp, mỗi bên: chặt quá thì tăng 0,25–0,3; lỏng thì giảm 0,15
DAY_GO = 1.6    # độ dày thành gờ (4 đường in với đầu phun 0,4)
CAO_GO = 4.0    # chiều cao gờ chèn vào hộp
VAT = 0.6       # vát mép dưới phía ngoài của gờ


def hcn(x, y, z):
    """4 góc hình chữ nhật, ngược chiều kim đồng hồ khi nhìn từ trên xuống (+z)."""
    return [(x[0], y[0], z), (x[1], y[0], z), (x[1], y[1], z), (x[0], y[1], z)]


def co(x, d):
    return (x[0] + d, x[1] - d)


z_tren, z_day, z_vat, z_go = MIENG_Z + DAY_NAP, MIENG_Z, MIENG_Z - CAO_GO + VAT, MIENG_Z - CAO_GO
go_ngoai_x, go_ngoai_y = co(TRONG_X, KHE), co(TRONG_Y, KHE)
go_trong_x, go_trong_y = co(go_ngoai_x, DAY_GO), co(go_ngoai_y, DAY_GO)

P_tren, P_duoi = hcn(NGOAI_X, NGOAI_Y, z_tren), hcn(NGOAI_X, NGOAI_Y, z_day)
G_ngoai_tren, G_ngoai_duoi = hcn(go_ngoai_x, go_ngoai_y, z_day), hcn(go_ngoai_x, go_ngoai_y, z_vat)
G_vat = hcn(co(go_ngoai_x, VAT), co(go_ngoai_y, VAT), z_go)
G_trong_tren, G_trong_duoi = hcn(go_trong_x, go_trong_y, z_day), hcn(go_trong_x, go_trong_y, z_go)


def thanh(duoi, tren, ngoai=True):
    """4 mặt bên nối 2 hình chữ nhật; pháp tuyến hướng ra xa tâm (ngoai) hoặc vào tâm."""
    ds = []
    for i in range(4):
        j = (i + 1) % 4
        ds.append([duoi[i], duoi[j], tren[j], tren[i]] if ngoai else [tren[i], tren[j], duoi[j], duoi[i]])
    return ds


# Mỗi mặt: (pháp tuyến ra ngoài, [vòng ngoài, các vòng lỗ...]); vòng ngoài ngược chiều kim đồng hồ khi nhìn theo
# pháp tuyến, vòng lỗ thì ngược lại.
MAT = [((0, 0, 1), [P_tren])]
MAT += [(None, [m]) for m in thanh(P_duoi, P_tren)]                                   # 4 cạnh tấm nắp
MAT += [((0, 0, -1), [P_duoi[::-1], G_ngoai_tren])]                                   # đáy tấm nắp, ngoài gờ
MAT += [((0, 0, -1), [G_trong_tren[::-1]])]                                           # đáy tấm nắp, trong gờ
MAT += [(None, [m]) for m in thanh(G_ngoai_duoi, G_ngoai_tren)]                       # mặt ngoài gờ
MAT += [(None, [m]) for m in thanh(G_vat, G_ngoai_duoi)]                              # mặt vát
MAT += [(None, [m]) for m in thanh(G_trong_duoi, G_trong_tren, ngoai=False)]          # mặt trong gờ
MAT += [((0, 0, -1), [G_vat[::-1], G_trong_duoi])]                                    # đáy gờ


def tru(a, b): return tuple(a[k] - b[k] for k in range(3))
def nhan(a, b): return (a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0])
def chuan(v):
    d = math.sqrt(sum(c * c for c in v))
    return tuple(c / d for c in v)


def phap_tuyen(vong):  # Newell
    n = [0.0, 0.0, 0.0]
    for i in range(len(vong)):
        a, b = vong[i], vong[(i + 1) % len(vong)]
        n[0] += (a[1] - b[1]) * (a[2] + b[2])
        n[1] += (a[2] - b[2]) * (a[0] + b[0])
        n[2] += (a[0] - b[0]) * (a[1] + b[1])
    return chuan(n)


MAT = [(phap_tuyen(v[0]) if n is None else n, v) for n, v in MAT]

# ---- kiểm tra kín: mỗi cạnh có hướng dùng đúng 1 lần và cạnh ngược lại cũng đúng 1 lần ----
canh = {}
for _, vongs in MAT:
    for v in vongs:
        for i in range(len(v)):
            k = (v[i], v[(i + 1) % len(v)])
            canh[k] = canh.get(k, 0) + 1
assert all(c == 1 for c in canh.values()), "cạnh bị dùng 2 lần cùng chiều"
assert all((b, a) in canh for a, b in canh), "mặt nắp chưa kín"


# ---- STL: chia mặt thành tam giác ----
def tam_giac(n, vongs):
    if len(vongs) == 1:
        v = vongs[0]
        return [(v[0], v[i], v[i + 1]) for i in range(1, len(v) - 1)]
    ngoai, lo = vongs[0][::-1], vongs[1]   # cả 2 về thứ tự ngược chiều kim đồng hồ nhìn từ +z, góc thứ k khớp nhau
    ds = []
    for k in range(4):
        q = [ngoai[k], lo[k], lo[(k + 1) % 4], ngoai[(k + 1) % 4]]   # dải giữa 2 hình chữ nhật, pháp tuyến -z
        ds += [(q[0], q[1], q[2]), (q[0], q[2], q[3])]
    return ds


tgs = [(n, t) for n, vongs in MAT for t in tam_giac(n, vongs)]
the_tich = sum(nhan(a, b)[0] * c[0] + nhan(a, b)[1] * c[1] + nhan(a, b)[2] * c[2] for _, (a, b, c) in tgs) / 6
for n, (a, b, c) in tgs:  # tam giác phải cùng chiều pháp tuyến mặt
    assert sum(x * y for x, y in zip(nhan(tru(b, a), tru(c, a)), n)) > 0
assert the_tich > 0

with open(os.path.join(THU_MUC, "nap_hop.stl"), "wb") as f:
    f.write(b"AIoT nap hop - in up mat tren xuong ban".ljust(80, b" "))
    f.write(struct.pack("<I", len(tgs)))
    for n, (a, b, c) in tgs:
        f.write(struct.pack("<12fH", *n, *a, *b, *c, 0))

# ---- STEP (AP214): khối đặc gồm các mặt phẳng ----
dong, so = [], [0]


def them(s):
    so[0] += 1
    dong.append(f"#{so[0]}={s};")
    return so[0]


def f6(x): return f"{x:.6f}".rstrip("0").rstrip(".") + ("." if "." not in f"{x:.6f}".rstrip("0").rstrip(".") else "")


def diem(p): return them(f"CARTESIAN_POINT('',({','.join(f6(c) for c in p)}))")
def huong(v): return them(f"DIRECTION('',({','.join(f6(c) for c in v)}))")


ctx_app = them("APPLICATION_CONTEXT('core data for automotive mechanical design processes')")
them(f"APPLICATION_PROTOCOL_DEFINITION('international standard','automotive_design',2000,#{ctx_app})")
ctx_sp = them(f"PRODUCT_CONTEXT('',#{ctx_app},'mechanical')")
sp = them(f"PRODUCT('nap_hop','nap_hop','',(#{ctx_sp}))")
ctx_dn = them(f"PRODUCT_DEFINITION_CONTEXT('part definition',#{ctx_app},'design')")
pdf = them(f"PRODUCT_DEFINITION_FORMATION('','',#{sp})")
pd = them(f"PRODUCT_DEFINITION('design','',#{pdf},#{ctx_dn})")
pds = them(f"PRODUCT_DEFINITION_SHAPE('','',#{pd})")
u_dai = them("(LENGTH_UNIT()NAMED_UNIT(*)SI_UNIT(.MILLI.,.METRE.))")
u_goc = them("(NAMED_UNIT(*)PLANE_ANGLE_UNIT()SI_UNIT($,.RADIAN.))")
u_khoi = them("(NAMED_UNIT(*)SI_UNIT($,.STERADIAN.)SOLID_ANGLE_UNIT())")
sai_so = them(f"UNCERTAINTY_MEASURE_WITH_UNIT(LENGTH_MEASURE(1.E-05),#{u_dai},'distance_accuracy_value','')")
ctx_hh = them(f"(GEOMETRIC_REPRESENTATION_CONTEXT(3)GLOBAL_UNCERTAINTY_ASSIGNED_CONTEXT((#{sai_so}))"
              f"GLOBAL_UNIT_ASSIGNED_CONTEXT((#{u_dai},#{u_goc},#{u_khoi}))REPRESENTATION_CONTEXT('',''))")
goc = them(f"AXIS2_PLACEMENT_3D('',#{diem((0, 0, 0))},#{huong((0, 0, 1))},#{huong((1, 0, 0))})")

dinh = {}


def dinh_cua(p):
    if p not in dinh: dinh[p] = them(f"VERTEX_POINT('',#{diem(p)})")
    return dinh[p]


cung = {}


def cung_cua(a, b):
    k = (a, b) if (a, b) in cung or (b, a) not in cung else (b, a)
    if k not in cung:
        d = tru(b, a)
        dai = math.sqrt(sum(c * c for c in d))
        vec = them(f"VECTOR('',#{huong(chuan(d))},{f6(dai)})")
        line = them(f"LINE('',#{diem(a)},#{vec})")
        cung[k] = them(f"EDGE_CURVE('',#{dinh_cua(a)},#{dinh_cua(b)},#{line},.T.)")
    return cung[k], k == (a, b)


mat_step = []
for n, vongs in MAT:
    bounds = []
    for i, v in enumerate(vongs):
        oe = []
        for j in range(len(v)):
            e, cung_chieu = cung_cua(v[j], v[(j + 1) % len(v)])
            oe.append(them(f"ORIENTED_EDGE('',*,*,#{e},{'.T.' if cung_chieu else '.F.'})"))
        loop = them(f"EDGE_LOOP('',({','.join(f'#{x}' for x in oe)}))")
        bounds.append(them(f"{'FACE_OUTER_BOUND' if i == 0 else 'FACE_BOUND'}('',#{loop},.T.)"))
    ref = (1, 0, 0) if abs(n[0]) < 0.9 else (0, 1, 0)
    ref = chuan(tru(ref, tuple(n[k] * sum(ref[m] * n[m] for m in range(3)) for k in range(3))))
    ax = them(f"AXIS2_PLACEMENT_3D('',#{diem(vongs[0][0])},#{huong(n)},#{huong(ref)})")
    mp = them(f"PLANE('',#{ax})")
    mat_step.append(them(f"ADVANCED_FACE('',({','.join(f'#{b}' for b in bounds)}),#{mp},.T.)"))
vo = them(f"CLOSED_SHELL('',({','.join(f'#{x}' for x in mat_step)}))")
khoi = them(f"MANIFOLD_SOLID_BREP('nap_hop',#{vo})")
bd = them(f"ADVANCED_BREP_SHAPE_REPRESENTATION('nap_hop',(#{goc},#{khoi}),#{ctx_hh})")
them(f"SHAPE_DEFINITION_REPRESENTATION(#{pds},#{bd})")

with open(os.path.join(THU_MUC, "nap_hop.step"), "w", encoding="ascii", newline="\n") as f:
    f.write("ISO-10303-21;\nHEADER;\nFILE_DESCRIPTION(('Nap hop AIoT'),'2;1');\n"
            "FILE_NAME('nap_hop.step','2026-11-26T12:00:00',(''),(''),'tao_nap_hop.py','','');\n"
            "FILE_SCHEMA(('AUTOMOTIVE_DESIGN { 1 0 10303 214 1 1 1 1 }'));\nENDSEC;\nDATA;\n")
    f.write("\n".join(dong))
    f.write("\nENDSEC;\nEND-ISO-10303-21;\n")

print(f"Nắp: {NGOAI_X[1] - NGOAI_X[0]:.1f} × {NGOAI_Y[1] - NGOAI_Y[0]:.1f} mm, dày {DAY_NAP} mm; gờ "
      f"{go_ngoai_x[1] - go_ngoai_x[0]:.1f} × {go_ngoai_y[1] - go_ngoai_y[0]:.1f} mm, cao {CAO_GO} mm, khe {KHE} mm/bên")
print(f"{len(MAT)} mặt, {len(tgs)} tam giác, thể tích {the_tich / 1000:.1f} cm³ ≈ {the_tich / 1000 * 1.24:.0f} g nhựa PLA (đặc 100 %)")
