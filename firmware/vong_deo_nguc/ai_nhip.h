// ai_nhip.h — AI chạy ngay trên vòng ngực (TinyML), không cần máy chủ.
//   1. Tìm đỉnh R từng mẫu theo Pan–Tompkins: lọc 5–15 Hz → đạo hàm 5 điểm → bình phương → tích phân 150 ms
//      → ngưỡng thích nghi, thời gian trơ 200 ms, tìm lại nhịp bị sót.
//   2. 11 đặc trưng nhịp của 10 giây gần nhất (8 đặc trưng HRV + 3 tỉ số RR kiểu ngoại tâm thu).
//   3. Mạng nơ-ron 11 → 16 → 16 → 3 (trọng số trong mo_hinh_tinyml.h, do notebook/kịch bản huấn luyện sinh ra).
// Bản Python giống hệt từng bước: StreamingRDetector + tinyml_features trong may_chu/aiot_infer.py
// (đã kiểm chứng trên MIT-BIH: tìm đúng 99,6 % nhịp).
#pragma once
#include <Arduino.h>
#include <math.h>

#include "mo_hinh_tinyml.h"

namespace ai_nhip {

struct Biquad {
  float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
  void thietKe(bool thongThap, float fc, float fs, float q = 0.70710678f) {
    float w = 2.0f * (float)M_PI * fc / fs, c = cosf(w), al = sinf(w) / (2.0f * q), a0 = 1.0f + al;
    if (thongThap) {
      b0 = (1 - c) / 2;
      b1 = 1 - c;
      b2 = (1 - c) / 2;
    } else {
      b0 = (1 + c) / 2;
      b1 = -(1 + c);
      b2 = (1 + c) / 2;
    }
    a1 = -2 * c / a0;
    a2 = (1 - al) / a0;
    b0 /= a0;
    b1 /= a0;
    b2 /= a0;
    z1 = z2 = 0;
  }
  float loc(float x) {
    float y = b0 * x + z1;
    z1 = b1 * x - a1 * y + z2;
    z2 = b2 * x - a2 * y;
    return y;
  }
};

constexpr int MAX_NHIP = 40, MAX_W = 128, SO_DT = 11;

// 11 đặc trưng từ thời điểm các đỉnh R (giây), giống tinyml_features() trong Python.
inline void tinhDacTrung(const float* t, int m, float* f) {
  float rr[MAX_NHIP];
  int k = 0;
  for (int i = 1; i < m; i++) {
    float d = t[i] - t[i - 1];
    if (d >= 0.25f && d <= 2.5f) rr[k++] = d;
  }
  for (int i = 0; i < SO_DT; i++) f[i] = 0;
  f[8] = 1;
  f[9] = 1;
  if (k < 3) return;
  float tb = 0;
  for (int i = 0; i < k; i++) tb += rr[i];
  tb /= k;
  float ss = 0, sd2 = 0, n50 = 0, mn = rr[0], mx = rr[0];
  for (int i = 0; i < k; i++) {
    ss += (rr[i] - tb) * (rr[i] - tb);
    mn = min(mn, rr[i]);
    mx = max(mx, rr[i]);
  }
  for (int i = 1; i < k; i++) {
    float d = rr[i] - rr[i - 1];
    sd2 += d * d;
    if (fabsf(d) > 0.05f) n50 += 1;
  }
  float rmssd = sqrtf(sd2 / (k - 1));
  int ngoat = 0;
  for (int i = 1; i < k - 1; i++)
    if ((rr[i] - rr[i - 1]) * (rr[i] - rr[i + 1]) > 0) ngoat++;
  float lo = mn, hi = mx;
  if (hi - lo < 1e-9f) {
    lo -= 0.5f;
    hi += 0.5f;
  }
  int dem[8] = {0};
  for (int i = 0; i < k; i++) {
    int b = (int)((rr[i] - lo) / (hi - lo) * 8.0f);
    dem[b < 0 ? 0 : (b > 7 ? 7 : b)]++;
  }
  float h = 0;
  for (int b = 0; b < 8; b++)
    if (dem[b]) {
      float p = (float)dem[b] / k;
      h -= p * logf(p);
    }
  f[0] = 60.0f / tb;
  f[1] = 1000.0f * sqrtf(ss / k);
  f[2] = 1000.0f * rmssd;
  f[3] = rmssd / tb;
  f[4] = n50 / (k - 1);
  f[5] = (float)ngoat / max(k - 2, 1);
  f[6] = h / logf(8.0f);
  f[7] = k + 1;
  float s[MAX_NHIP];
  for (int i = 0; i < k; i++) s[i] = rr[i];
  for (int i = 1; i < k; i++)  // sắp xếp chèn (≤ 40 phần tử) để lấy trung vị
    for (int j = i; j > 0 && s[j] < s[j - 1]; j--) {
      float x = s[j];
      s[j] = s[j - 1];
      s[j - 1] = x;
    }
  float med = (k % 2) ? s[k / 2] : 0.5f * (s[k / 2 - 1] + s[k / 2]);
  int som = 0;
  for (int i = 0; i < k - 1; i++)
    if (rr[i] < 0.85f * med && rr[i + 1] > 1.1f * med) som++;
  f[8] = mn / med;
  f[9] = mx / med;
  f[10] = (float)som / k;
}

// Mạng nơ-ron: chuẩn hoá → 16 (ReLU) → 16 (ReLU) → 3 (softmax).
inline void duDoan(const float* f, float* p) {
  // Gọi rõ tinyml:: (không "using namespace"): Arduino có sẵn hằng số nhị phân B1, B10... dễ trùng tên.
  constexpr int SO_VAO = tinyml::SO_VAO, AN1 = tinyml::AN1, AN2 = tinyml::AN2, SO_LOP = tinyml::SO_LOP;
  float x[SO_VAO], h1[AN1], h2[AN2], z[SO_LOP];
  for (int i = 0; i < SO_VAO; i++) x[i] = (f[i] - tinyml::TB[i]) / tinyml::DO_LECH[i];
  for (int j = 0; j < AN1; j++) {
    float s = tinyml::LECH1[j];
    for (int i = 0; i < SO_VAO; i++) s += x[i] * tinyml::TS1[i * AN1 + j];
    h1[j] = s > 0 ? s : 0;
  }
  for (int j = 0; j < AN2; j++) {
    float s = tinyml::LECH2[j];
    for (int i = 0; i < AN1; i++) s += h1[i] * tinyml::TS2[i * AN2 + j];
    h2[j] = s > 0 ? s : 0;
  }
  float zmax = -1e30f;
  for (int j = 0; j < SO_LOP; j++) {
    float s = tinyml::LECH3[j];
    for (int i = 0; i < AN2; i++) s += h2[i] * tinyml::TS3[i * SO_LOP + j];
    z[j] = s;
    zmax = max(zmax, s);
  }
  float tong = 0;
  for (int j = 0; j < SO_LOP; j++) {
    p[j] = expf(z[j] - zmax);
    tong += p[j];
  }
  for (int j = 0; j < SO_LOP; j++) p[j] /= tong;
}

// Tìm đỉnh R từng mẫu (Pan–Tompkins), giữ thời điểm 40 nhịp gần nhất.
class BoTimR {
 public:
  explicit BoTimR(float fs = 500) : fs(fs) { datLai(); }

  void datLai() {
    hp.thietKe(false, 5.0f, fs);
    lp.thietKe(true, 15.0f, fs);
    for (int i = 0; i < 5; i++) yb[i] = 0;
    W = max(1, min(MAX_W, (int)lroundf(0.15f * fs)));
    for (int i = 0; i < W; i++) buf[i] = 0;
    k = 0;
    tong = p1 = p2 = maxHoc = tongHoc = spk = npk = 0;
    n = 0;
    hoc = (int64_t)(2 * fs);
    tro = (int64_t)(0.2f * fs);
    cuoi = nhieuT = -1;
    cuoiV = nhieuV = 0;
    soRR = soNhip = 0;
  }

  void them(float x) {
    float y = lp.loc(hp.loc(x));
    yb[0] = yb[1];
    yb[1] = yb[2];
    yb[2] = yb[3];
    yb[3] = yb[4];
    yb[4] = y;
    float d = (2 * yb[4] + yb[3] - yb[1] - 2 * yb[0]) * (fs / 8.0f);
    float e = d * d;
    tong += e - buf[k];
    buf[k] = e;
    if (++k == W) {  // tính lại tổng cho khỏi trôi số
      k = 0;
      tong = 0;
      for (int i = 0; i < W; i++) tong += buf[i];
    }
    float m = tong / W;
    int64_t nn = n++;
    if (nn < hoc) {  // 2 giây đầu: học mức tín hiệu và nhiễu
      if (m > maxHoc) maxHoc = m;
      tongHoc += m;
      if (nn == hoc - 1) {
        spk = 0.33f * maxHoc;
        npk = 0.5f * tongHoc / hoc;
      }
    } else {
      int64_t moc = cuoi >= 0 ? cuoi : hoc;
      if (nn - moc > (int64_t)(2.5f * fs) && spk > npk) {
        float giam = 1.0f - 1.5f / fs;
        if (giam < 0.9f) giam = 0.9f;
        spk = npk + giam * (spk - npk);  // quá 2,5 s không thấy nhịp (kể cả sau gai nhiễu lúc mới đeo): hạ ngưỡng ~4,5x/s
      }
      if (p1 > p2 && p1 >= m && p1 > 0) {
        dinh(nn - 1, p1);
      }
    }
    p2 = p1;
    p1 = m;
  }

  // Số nhịp và 11 đặc trưng trong `giay` giây gần nhất.
  int dacTrung(float* f, float giay = 10.0f) const {
    float t[MAX_NHIP];
    int m = 0;
    int64_t tu = n - (int64_t)(giay * fs);
    for (int i = 0; i < soNhip; i++)
      if (nhip[i] >= tu) t[m++] = nhip[i] / fs;
    tinhDacTrung(t, m, f);
    return m;
  }

 private:
  float fs;
  Biquad hp, lp;
  float yb[5], buf[MAX_W], tong, p1, p2, maxHoc, tongHoc, spk, npk, cuoiV, nhieuV;
  int W, k, soRR, soNhip;
  int64_t n, hoc, tro, cuoi, nhieuT, rr[8], nhip[MAX_NHIP];

  void nhan(int64_t t, float v) {
    if (cuoi >= 0) {
      if (soRR == 8) {
        for (int i = 1; i < 8; i++) rr[i - 1] = rr[i];
        soRR--;
      }
      rr[soRR++] = t - cuoi;
    }
    cuoi = t;
    cuoiV = v;
    if (soNhip == MAX_NHIP) {
      for (int i = 1; i < MAX_NHIP; i++) nhip[i - 1] = nhip[i];
      soNhip--;
    }
    nhip[soNhip++] = t;
    nhieuT = -1;
    nhieuV = 0;
  }

  void dinh(int64_t t, float v) {
    float thr = npk + 0.25f * (spk - npk);
    if (v > thr) {
      if (cuoi >= 0 && t - cuoi < tro) {  // cùng 1 QRS: dời tới đỉnh cao hơn
        if (v > cuoiV) {
          if (soRR) rr[soRR - 1] += t - cuoi;
          if (soNhip) nhip[soNhip - 1] = t;
          cuoi = t;
          cuoiV = v;
        }
        return;
      }
      float rrTb = 0;
      for (int i = 0; i < soRR; i++) rrTb += rr[i];
      if (soRR) rrTb /= soRR;
      if (cuoi >= 0 && rrTb > 0 && t - cuoi > 1.66f * rrTb && nhieuT >= 0 && nhieuV > 0.5f * thr && t - nhieuT >= tro) {
        float nv = nhieuV;
        nhan(nhieuT, nv);  // nhịp bị sót
        spk = 0.25f * nv + 0.75f * spk;
      }
      float vCap = (spk > 0 && v > 3.0f * spk) ? 3.0f * spk : v;  // chặn 1 gai nhiễu làm vọt ngưỡng
      nhan(t, v);
      spk = 0.125f * vCap + 0.875f * spk;
    } else {
      npk = 0.125f * v + 0.875f * npk;
      if (v > nhieuV && (cuoi < 0 || t - cuoi >= tro)) {
        nhieuT = t;
        nhieuV = v;
      }
    }
  }
};

}  // namespace ai_nhip
