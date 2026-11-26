// nhip_tim.h — nhịp tim, SpO2 và chỉ số tưới máu tính ngay trên vòng tay từ PPG 100 mẫu/giây.
// Đây là số ước lượng để hiển thị nhanh; máy chủ vẫn nhận sóng PPG đầy đủ cho mô hình AI.
//
// Lọc nhiễu (bản nâng cấp):
//   1. Khử gai: bước nhảy giữa 2 mẫu lớn bất thường (lỗi đọc I2C, va chạm) bị cắt bớt trước khi vào bộ lọc,
//      nên 1 gai không làm bộ lọc "rung" cả giây.
//   2. Lọc thông dải Butterworth bậc 4 (0,5–4 Hz) bằng 2 tầng biquad: bỏ trôi đường nền (thở, cử động chậm)
//      và nhiễu nhanh, giữ nhịp tim 30–240 lần/phút.
//   3. Phát hiện cử động: tín hiệu vượt 4 lần biên độ mạch bình thường → tạm bỏ qua 1,5 giây.
//   4. Đỉnh mạch: cực đại địa phương, cao hơn đáy ngay trước nó ít nhất 40 % biên độ trung bình (bỏ đỉnh phụ
//      của sóng phản hồi), cách nhịp trước ≥ 0,3 s. Nhịp tim = trung vị 8 khoảng nhịp gần nhất.
// SpO2 tính theo từng nhịp: (đỉnh − đáy)/DC của kênh đỏ chia cho kênh hồng ngoại, lấy trung vị 8 nhịp.
#pragma once
#include <Arduino.h>
#include <math.h>

namespace nhip_tim {

constexpr float FS = 100.0f;

// Bộ lọc bậc 2 (biquad, dạng chuẩn tắc II chuyển vị). Hệ số theo công thức RBJ, Q = 0,7071 → Butterworth.
struct Biquad {
  float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;

  void thietKe(bool thongThap, float fc, float fs = FS, float q = 0.70710678f) {
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
    a1 = -2 * c;
    a2 = 1 - al;
    b0 /= a0;
    b1 /= a0;
    b2 /= a0;
    a1 /= a0;
    a2 /= a0;
  }

  void datLai() { z1 = z2 = 0; }

  float loc(float x) {
    float y = b0 * x + z1;
    z1 = b1 * x - a1 * y + z2;
    z2 = b2 * x - a2 * y;
    return y;
  }
};

// Khử gai + thông dải 0,5–4 Hz cho 1 kênh PPG.
class BoLoc {
 public:
  BoLoc() {
    thongCao.thietKe(false, 0.5f);
    thongThap.thietKe(true, 4.0f);
  }

  void datLai() {
    daChay = false;
    thongCao.datLai();
    thongThap.datLai();
    tbBuoc = 0;
  }

  float loc(float x) {
    if (!daChay) {
      goc = truoc = x;
      daChay = true;
    }
    float d = x - truoc;
    if (tbBuoc > 0 && fabsf(d) > 8.0f * tbBuoc + 50.0f) d = (d > 0 ? 3.0f : -3.0f) * tbBuoc;  // gai nhọn
    tbBuoc += 0.01f * (fabsf(d) - tbBuoc);
    truoc += d;
    return thongThap.loc(thongCao.loc(truoc - goc));  // trừ mốc đầu để bộ lọc không phải "nuốt" mức DC rất lớn
  }

 private:
  Biquad thongCao, thongThap;
  float goc = 0, truoc = 0, tbBuoc = 0;
  bool daChay = false;
};

// Đếm nhịp tim trên kênh chính (hồng ngoại của MAX30102/MAX30100 hoặc kênh duy nhất của AS7038RB).
class BoDem {
 public:
  void datLai() {
    boLoc.datLai();
    soKhoang = viTri = 0;
    mocNhip = 0;
    bienDoTB = y1 = y2 = day = dc = 0;
    mau = 0;
    hetCuDong = 0;
  }

  // tho: số đọc thô; ms: thời điểm của mẫu. Trả true nếu vừa đi qua 1 đỉnh nhịp.
  bool them(uint32_t tho, uint32_t ms) {
    float raw = (float)tho;
    dc = (mau == 0) ? raw : dc + 0.005f * (raw - dc);  // mức DC, dùng tính chỉ số tưới máu
    float y = boLoc.loc(-raw);                           // máu dồn tới → phản xạ giảm: đảo dấu để đỉnh hướng lên
    bool laNhip = false;
    if (++mau > 200) {                                   // 2 giây đầu: bộ lọc đang ổn định
      if (bienDoTB > 0 && fabsf(y) > 4.0f * bienDoTB) hetCuDong = ms + 1500;
      if (dangCuDong(ms)) day = y;                       // tránh lưu đáy sâu từ gai cử động
      else if (y1 < day) day = y1;
      bool dinh = y1 > y2 && y1 >= y && y1 > 0;
      bool duCao = bienDoTB <= 0 || (y1 - day) > 0.4f * bienDoTB;
      if (dinh && duCao && !dangCuDong(ms) && (mocNhip == 0 || ms - mocNhip > 300)) {
        float bienDo = y1 - day;
        if (bienDoTB <= 0) bienDoTB = bienDo;
        else if (bienDo < 3.0f * bienDoTB) bienDoTB += 0.2f * (bienDo - bienDoTB);
        if (mocNhip != 0) {
          uint32_t kc = ms - mocNhip;
          if (kc <= 2000) {
            khoang[viTri] = kc;
            viTri = (viTri + 1) % SO_KHOANG;
            if (soKhoang < SO_KHOANG) soKhoang++;
          } else {
            soKhoang = viTri = 0;  // mất nhịp quá lâu: đếm lại từ đầu
          }
        }
        mocNhip = ms;
        day = y1;
        laNhip = true;
      } else if (mocNhip != 0 && ms - mocNhip > 2000) {
        bienDoTB *= 0.995f;  // lâu không thấy nhịp (tín hiệu yếu đi): hạ dần ngưỡng
      }
    }
    y2 = y1;
    y1 = y;
    return laNhip;
  }

  bool dangCuDong(uint32_t ms) const { return hetCuDong != 0 && (int32_t)(hetCuDong - ms) > 0; }

  // Nhịp tim (lần/phút); -1 nếu chưa đủ 4 khoảng nhịp hoặc đã quá 3 giây không thấy nhịp.
  int nhipTim(uint32_t ms) const {
    if (soKhoang < 4 || ms - mocNhip > 3000) return -1;
    uint32_t a[SO_KHOANG];
    for (int i = 0; i < soKhoang; i++) a[i] = khoang[i];
    for (int i = 1; i < soKhoang; i++)  // sắp xếp chèn, tối đa 8 phần tử
      for (int j = i; j > 0 && a[j] < a[j - 1]; j--) {
        uint32_t t = a[j];
        a[j] = a[j - 1];
        a[j - 1] = t;
      }
    uint32_t giua = (soKhoang % 2) ? a[soKhoang / 2] : (a[soKhoang / 2 - 1] + a[soKhoang / 2]) / 2;
    int hr = (int)lroundf(60000.0f / giua);
    return (hr >= 30 && hr <= 220) ? hr : -1;
  }

  // Chất lượng tín hiệu: 100 tốt, 50 có cử động trong 5 giây qua, 0 chưa bắt được nhịp.
  int chatLuong(uint32_t ms) const {
    if (nhipTim(ms) < 0) return 0;
    return (hetCuDong != 0 && (int32_t)(hetCuDong + 3500 - ms) > 0) ? 50 : 100;
  }

  // Chỉ số tưới máu PI (%): biên độ sóng mạch / mức DC. Cổ tay thường 0,1–2 %; quá thấp là áp chưa sát da.
  float chiSoTuoiMau() const { return dc > 1 ? 100.0f * bienDoTB / dc : 0; }

 private:
  static constexpr int SO_KHOANG = 8;
  BoLoc boLoc;
  uint32_t khoang[SO_KHOANG] = {0};
  int soKhoang = 0, viTri = 0;
  uint32_t mocNhip = 0, mau = 0, hetCuDong = 0;
  float bienDoTB = 0, y1 = 0, y2 = 0, day = 0, dc = 0;
};

// SpO2 theo "tỉ số của tỉ số" tính cho từng nhịp: R = (AC đỏ / DC đỏ) / (AC hồng ngoại / DC hồng ngoại).
// Chưa hiệu chuẩn với máy đo chuẩn, sai số có thể vài %: chỉ dùng để tham khảo. SPO2_BU trong cau_hinh.h bù độ lệch
// sau khi so với máy kẹp ngón (cách làm trong HUONG_DAN.md).
// Bỏ nhịp có cử động ở bất kỳ lúc nào trong nhịp (không chỉ lúc kết thúc nhịp) và nhịp dài quá 2 giây (đã lỡ nhịp ở
// giữa, đỉnh − đáy không còn là của 1 nhịp). 15 giây không có nhịp hợp lệ thì xoá số cũ, báo "chưa có".
#ifndef SPO2_BU
#define SPO2_BU 0
#endif
class BoSpO2 {
 public:
  void datLai() {
    locHn.datLai();
    locDo.datLai();
    daChay = false;
    n = viTri = 0;
    giaTri = rTrungVi = -1;
    mauTuLanCuoi = 0;
    datLaiDinhDay();
  }

  // laNhip: BoDem vừa thấy 1 nhịp; tot: không có cử động.
  void them(uint32_t hongNgoai, uint32_t kenhDo, bool laNhip, bool tot) {
    float a = hongNgoai, b = kenhDo;
    if (!daChay) {
      dcHn = a;
      dcDo = b;
      daChay = true;
    }
    dcHn += 0.01f * (a - dcHn);  // mức DC, tần số cắt ≈ 0,16 Hz
    dcDo += 0.01f * (b - dcDo);
    float yh = locHn.loc(a), yd = locDo.loc(b);
    if (yh > maxHn) maxHn = yh;
    if (yh < minHn) minHn = yh;
    if (yd > maxDo) maxDo = yd;
    if (yd < minDo) minDo = yd;
    if (!tot) coCuDong = true;   // cử động lúc nào trong nhịp cũng làm sai đỉnh − đáy của cả nhịp
    soMauNhip++;
    if (++mauTuLanCuoi > HET_HAN_MAU) {  // lâu không có nhịp hợp lệ: số cũ không còn đúng, học lại từ đầu
      giaTri = rTrungVi = -1;
      n = viTri = 0;
    }
    if (!laNhip) return;
    float acHn = maxHn - minHn, acDo = maxDo - minDo;  // đỉnh − đáy trong đúng 1 chu kỳ mạch
    bool boQua = coCuDong || soMauNhip > NHIP_DAI_NHAT;
    datLaiDinhDay();
    if (boQua || dcHn < 1 || dcDo < 1 || acHn / dcHn < 0.001f || acHn / dcHn > 0.25f || acDo / dcDo > 0.25f) return;  // PI 0,1–25 %: lọc nhiễu nền & va đập mạnh
    float R = (acDo / dcDo) / (acHn / dcHn);
    if (R < 0.3f || R > 1.5f) return;
    mauTuLanCuoi = 0;
    r[viTri] = R;
    viTri = (viTri + 1) % SO_NHIP;
    if (n < SO_NHIP) n++;
    if (n < 4) return;
    float s[SO_NHIP];
    for (int i = 0; i < n; i++) s[i] = r[i];
    for (int i = 1; i < n; i++)
      for (int j = i; j > 0 && s[j] < s[j - 1]; j--) {
        float t = s[j];
        s[j] = s[j - 1];
        s[j - 1] = t;
      }
    float Rg = (n % 2) ? s[n / 2] : 0.5f * (s[n / 2 - 1] + s[n / 2]);
    rTrungVi = Rg;
    // Đường cong thực nghiệm trong thiết kế mẫu của Maxim (MAX30102). MAX30100 cùng hãng, cùng cách đo nên dùng chung;
    // độ lệch riêng của từng vòng bù bằng SPO2_BU.
    float moi = -45.060f * Rg * Rg + 30.354f * Rg + 94.845f + SPO2_BU;
    if (moi < 70) return;
    if (moi > 100) moi = 100;
    giaTri = giaTri < 0 ? moi : 0.7f * giaTri + 0.3f * moi;
  }

  int phanTram() const { return giaTri < 0 ? -1 : (int)lroundf(giaTri); }  // -1 = chưa có
  float tiSoR() const { return rTrungVi; }  // trung vị R của các nhịp gần nhất (-1 = chưa có), dùng khi hiệu chỉnh

 private:
  static constexpr int SO_NHIP = 8;
  static constexpr uint32_t NHIP_DAI_NHAT = 200;   // 2 s ở 100 mẫu/giây (nhịp tim 30 lần/phút)
  static constexpr uint32_t HET_HAN_MAU = 1500;    // 15 s
  BoLoc locHn, locDo;
  float dcHn = 0, dcDo = 0, maxHn = 0, minHn = 0, maxDo = 0, minDo = 0;
  float r[SO_NHIP] = {0};
  float giaTri = -1, rTrungVi = -1;
  int n = 0, viTri = 0;
  uint32_t soMauNhip = 0, mauTuLanCuoi = 0;
  bool daChay = false, coCuDong = false;

  void datLaiDinhDay() {  // bắt đầu 1 nhịp mới
    maxHn = maxDo = -1e30f;
    minHn = minDo = 1e30f;
    coCuDong = false;
    soMauNhip = 0;
  }
};

}  // namespace nhip_tim
