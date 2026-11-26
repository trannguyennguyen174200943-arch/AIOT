// xu_ly_radar.h — xử lý tín hiệu radar ngay trên thiết bị, dùng chung cho MR60BHA1 và bộ kit MR60BHA2.
//   ChuanHoa    đưa pha của MR60BHA2 về sóng 0–255 quanh 128 như MR60BHA1, kèm tỉ lệ để nhận ra cử động người
//   LocNhip     trung vị 5 số nhịp tim / nhịp thở radar gửi: bỏ số nhảy vọt, đánh giá độ ổn định
//   BoNgungTho  phát hiện khoảng ngưng cử động thở ≥ 10 giây lúc người nằm yên, đếm số lần trong 1 giờ
//   anh_sang    cảm biến ánh sáng BH1750 có sẵn trên bộ kit MR60BHA2 (I2C D4, D5): phòng tối thì giảm độ sáng đèn
#pragma once
#include <Arduino.h>
#include <Wire.h>
#include <math.h>

namespace xu_ly {

constexpr int FS = 5;  // điểm sóng mỗi giây

// ---- Chuẩn hoá 1 kênh pha (radian) về 0–255 quanh 128 ----
// Bỏ mức nền trôi chậm (~10 s) rồi chia cho độ lệch trung bình. Độ lệch trung bình tăng nhanh (~4 s) khi tín hiệu lớn
// lên để sóng không bị cắt ngọn lâu, giảm chậm (~50 s) để lúc ngưng thở đường sóng phẳng thật chứ không phóng to nhiễu.
// Đang cử động thì giữ nguyên độ lệch trung bình: một cú xoay mình không làm hệ số co lại cả phút.
struct ChuanHoa {
  float nen = 0, bienDo = 0;  // bienDo (radian) in ra Serial để biết biên độ thật
  float tiLe = 0;             // độ lệch hiện tại / độ lệch trung bình: lớn bất thường là đang cử động
  uint32_t n = 0;

  int ra(float x, bool giuBienDo) {
    if (n == 0) nen = x;
    nen += (n < 50 ? 1.0f / (n + 1) : 0.02f) * (x - nen);
    float lech = fabsf(x - nen);
    tiLe = n < 50 ? 0 : lech / fmaxf(bienDo, 1e-3f);
    if (n < 100) bienDo += (lech - bienDo) / (n + 1);
    else if (!giuBienDo) bienDo += (lech > bienDo ? 0.05f : 0.004f) * (lech - bienDo);
    n++;
    int y = 128 + (int)lroundf(40.0f * (x - nen) / fmaxf(bienDo, 1e-3f));
    return y < 0 ? 0 : (y > 255 ? 255 : y);
  }
};

// ---- Lọc nhịp tim / nhịp thở do radar tính ----
// Radar gửi từng số một, đôi khi nhảy vọt (người trở mình, nhiễu). Lấy trung vị 5 số gần nhất; 20 giây không có số
// mới thì báo "chưa có" (-1) thay vì gửi số cũ.
class LocNhip {
 public:
  void them(int v, uint32_t ms) {
    if (v <= 0) return;  // 0 hoặc -1: radar chưa đo được
    a[vt] = v;
    vt = (vt + 1) % N;
    if (n < N) n++;
    moc = ms;
  }
  void datLai() { n = vt = 0; moc = 0; }

  int giaTri(uint32_t ms) const {  // -1 nếu chưa đủ 3 số hoặc quá cũ
    if (n < 3 || !moc || ms - moc > 20000) return -1;
    int s[N];
    sapXep(s);
    return s[n / 2];
  }
  bool onDinh(uint32_t ms) const {  // các số trong cửa sổ lệch nhau không quá 20 % trung vị
    int m = giaTri(ms);
    if (m <= 0) return false;
    int s[N];
    sapXep(s);
    return s[n - 1] - s[0] <= m / 5;
  }

 private:
  static constexpr int N = 5;
  int a[N] = {0}, n = 0, vt = 0;
  uint32_t moc = 0;
  void sapXep(int* s) const {
    for (int i = 0; i < n; i++) s[i] = a[i];
    for (int i = 1; i < n; i++)
      for (int j = i; j > 0 && s[j] < s[j - 1]; j--) {
        int t = s[j];
        s[j] = s[j - 1];
        s[j - 1] = t;
      }
  }
};

// ---- Ngưng cử động thở ----
// So biên độ thở ngắn hạn (~3 s) với biên độ thở bình thường của chính người đó (học trong 20 giây nằm yên đầu tiên,
// sau đó cập nhật chậm khi thở đều). Dưới 25 % lúc người nằm yên là đang ngưng; trên 50 % liền 3 giây là đã thở lại.
// Biên độ ngắn hạn cần khoảng 4 giây để tụt xuống nên số giây ngưng được cộng thêm 4. Cử động (thường là lúc tỉnh dậy
// cuối cơn ngưng thở) cũng chốt khoảng ngưng. Giống cảm biến ở dây đeo ngực, radar chỉ thấy ngưng cử động thở (ngưng
// thở trung ương, giảm thở rõ); ngưng thở tắc nghẽn vẫn còn cử động gắng thở nên cần thêm SpO2 vòng tay.
class BoNgungTho {
 public:
  enum SuKien : uint8_t { KHONG, BAO, THO_LAI };
  int giayNgung = 0;  // khoảng ngưng đang diễn ra (giây), 0 = đang thở
  int giayVua = 0;    // độ dài khoảng ngưng vừa kết thúc

  // x: 1 điểm sóng thở (5 điểm/giây, đơn vị tuỳ loại radar). Không có người thì quên hết, có người lại thì học lại.
  SuKien them(float x, bool coNguoi, bool cuDong, uint32_t ms) {
    if (!coNguoi) {
      datLai();
      return KHONG;
    }
    if (n == 0) nen = x;
    nen += (n < 50 ? 1.0f / (n + 1) : 0.02f) * (x - nen);                          // mức nền trôi chậm (~10 s)
    bdNgan += (n < 15 ? 1.0f / (n + 1) : 1.0f / 15) * (fabsf(x - nen) - bdNgan);  // biên độ ngắn hạn (~3 s)
    n++;
    if (bdNen <= 0) {  // chưa học xong: cần 20 giây nằm yên
      if (!cuDong && n > 3 * FS) {
        tongHoc += bdNgan;
        if (++demHoc >= 20 * FS) bdNen = fmaxf(tongHoc / demHoc, 1e-6f);
      }
      return KHONG;
    }
    if (cuDong) return ketThuc(ms);
    if (!mauNgung && bdNgan > 0.5f * bdNen && bdNgan < 3.0f * bdNen) bdNen += 0.002f * (bdNgan - bdNen);  // ~100 s
    if (bdNgan < 0.25f * bdNen) {
      mauNgung++;
      mauTho = 0;
      giayNgung = (int)(mauNgung / FS) + 4;
      if (giayNgung >= 20 && !daBao) {
        daBao = true;
        return BAO;
      }
    } else if (mauNgung) {
      if (bdNgan <= 0.5f * bdNen) mauTho = 0;
      else if (++mauTho >= 3 * FS) return ketThuc(ms);  // thở lại đều 3 giây
    }
    return KHONG;
  }

  int lanTrong1Gio(uint32_t ms) const {  // số khoảng ngưng ≥ 10 giây trong 60 phút qua
    int d = 0;
    for (int i = 0; i < soLan; i++)
      if (ms - lan[i] < 3600000UL) d++;
    return d;
  }

  void datLai() {
    n = mauNgung = mauTho = demHoc = 0;
    nen = bdNgan = bdNen = tongHoc = 0;
    giayNgung = 0;
    daBao = false;
  }

 private:
  static constexpr int MAX_LAN = 60;
  uint32_t n = 0, mauNgung = 0, mauTho = 0, demHoc = 0;
  float nen = 0, bdNgan = 0, bdNen = 0, tongHoc = 0;
  bool daBao = false;
  uint32_t lan[MAX_LAN] = {0};
  int soLan = 0, viTri = 0;

  SuKien ketThuc(uint32_t ms) {
    SuKien sk = KHONG;
    if (mauNgung && giayNgung >= 10) {
      giayVua = giayNgung;
      lan[viTri] = ms;
      viTri = (viTri + 1) % MAX_LAN;
      if (soLan < MAX_LAN) soLan++;
      if (daBao) sk = THO_LAI;
    }
    mauNgung = mauTho = 0;
    giayNgung = 0;
    daBao = false;
    return sk;
  }
};

}  // namespace xu_ly

// ---- Cảm biến ánh sáng BH1750 trên bộ kit MR60BHA2 (địa chỉ 0x23; mạch khác có thể là 0x5C) ----
namespace anh_sang {

inline uint8_t dc = 0;  // 0 = không có cảm biến (mạch tự lắp theo Hình C)

inline bool batDau() {
  Wire.begin();  // chân I2C mặc định của XIAO: D4 (SDA), D5 (SCL)
  Wire.setTimeOut(20);
  const uint8_t dsDiaChi[2] = {0x23, 0x5C};
  for (uint8_t a : dsDiaChi) {
    Wire.beginTransmission(a);
    Wire.write(0x01);  // bật nguồn
    if (Wire.endTransmission() != 0) continue;
    Wire.beginTransmission(a);
    Wire.write(0x10);  // đo liên tục, độ phân giải 1 lux, 120 ms một lần
    if (Wire.endTransmission() == 0) {
      dc = a;
      return true;
    }
  }
  return false;
}

inline float docLux() {  // -1 nếu không có cảm biến hoặc đọc lỗi
  if (!dc || Wire.requestFrom((uint16_t)dc, (size_t)2, true) != 2) return -1;
  uint16_t v = (uint16_t)Wire.read() << 8;
  v |= (uint16_t)Wire.read();
  return v / 1.2f;
}

}  // namespace anh_sang
