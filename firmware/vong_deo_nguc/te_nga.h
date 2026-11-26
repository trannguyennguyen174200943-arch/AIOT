// te_nga.h — MPU6050 (GY-521, I2C 0x68): mức vận động và phát hiện té ngã, đọc 50 lần/giây.
// Té ngã = (rơi tự do rồi va chạm, hoặc va chạm rất mạnh) + sau đó thân người nghiêng hẳn đi và nằm yên.
// Không cần biết vòng đeo được gắn theo hướng nào: so tư thế sau ngã với tư thế ngay trước ngã.
#pragma once
#include <Wire.h>
#include <math.h>

namespace te_nga {

constexpr uint8_t DC = 0x68;
constexpr int TAN_SO = 50;

// Ngưỡng tính theo g (gia tốc trọng trường). Nên thử ngã lên đệm dày rồi chỉnh lại.
constexpr float ROI_TU_DO_G = 0.45f;      // |a| nhỏ hơn: cơ thể đang rơi
constexpr float VA_CHAM_G = 2.2f;         // |a| lớn hơn, trong 1 s sau khi rơi: chạm đất
constexpr float VA_CHAM_MANH_G = 3.0f;    // va chạm rất mạnh dù không thấy pha rơi (ngã khuỵu)
constexpr float GOC_NGA_DO = 50.0f;       // thân người lệch hơn góc này so với trước khi ngã
constexpr float GOC_DUNG_DAY_DO = 30.0f;  // trở lại gần tư thế cũ 3 s liền: đã đứng dậy

enum class SuKien : uint8_t { KHONG, NGA, DUNG_DAY };

struct Mau {
  float ax, ay, az;  // g
  float gx, gy, gz;  // độ/giây
};

inline bool ghi(uint8_t r, uint8_t v) {
  Wire.beginTransmission(DC);
  Wire.write(r);
  Wire.write(v);
  return Wire.endTransmission() == 0;
}

// Trả về giá trị WHO_AM_I (0x68 với MPU6050 thật; bản nhái có thể là 0x70, 0x72, 0x98), -1 nếu không thấy.
inline int batDau() {
  Wire.beginTransmission(DC);
  Wire.write(0x75);
  if (Wire.endTransmission(false) != 0 || Wire.requestFrom((uint16_t)DC, (size_t)1, true) != 1) return -1;
  int who = Wire.read();
  ghi(0x6B, 0x01);  // thức dậy, lấy xung nhịp từ con quay trục X
  ghi(0x1A, 0x03);  // lọc thông thấp ≈ 44 Hz
  ghi(0x1B, 0x08);  // con quay ±500 °/s (65,5 LSB mỗi °/s)
  ghi(0x1C, 0x10);  // gia tốc ±8 g (4096 LSB mỗi g)
  return who;
}

// Đọc 1 mẫu. Trả false nếu đọc I2C lỗi, hoặc 7 số đo giống hệt nhau suốt 1 giây: MPU6050 bị sụt áp sẽ tự khởi động
// lại ở chế độ ngủ, thanh ghi dữ liệu đứng yên mà đọc vẫn "thành công" (cảm biến đang chạy luôn nhiễu vài đơn vị).
inline bool doc(Mau& m) {
  static int16_t truoc[7];
  static int giongNhau = 0;
  Wire.beginTransmission(DC);
  Wire.write(0x3B);
  if (Wire.endTransmission(false) != 0 || Wire.requestFrom((uint16_t)DC, (size_t)14, true) != 14) return false;
  int16_t v[7];
  for (int i = 0; i < 7; i++) {
    int cao = Wire.read();
    int thap = Wire.read();
    v[i] = (int16_t)((cao << 8) | thap);
  }
  giongNhau = memcmp(v, truoc, sizeof v) == 0 ? giongNhau + 1 : 0;
  memcpy(truoc, v, sizeof v);
  if (giongNhau >= TAN_SO) return false;
  m.ax = v[0] / 4096.0f;
  m.ay = v[1] / 4096.0f;
  m.az = v[2] / 4096.0f;
  m.gx = v[4] / 65.5f;  // v[3] là nhiệt độ
  m.gy = v[5] / 65.5f;
  m.gz = v[6] / 65.5f;
  return true;
}

inline float gocGiua(const float* a, const float* b) {
  float d = a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
  float na = sqrtf(a[0] * a[0] + a[1] * a[1] + a[2] * a[2]);
  float nb = sqrtf(b[0] * b[0] + b[1] * b[1] + b[2] * b[2]);
  if (na < 1e-3f || nb < 1e-3f) return 0;
  return acosf(fminf(fmaxf(d / (na * nb), -1.0f), 1.0f)) * 57.29578f;
}

class BoPhatHien {
 public:
  // Số liệu mỗi giây cho máy chủ: máy chủ bỏ qua đoạn nhịp thở lúc người đang vận động.
  float vanDong = 0;   // độ lệch chuẩn của |a| trong 1 s (g)
  float conQuay = 0;   // tốc độ xoay trung bình (°/s)
  int muc = 0;         // 0 nằm/ngồi yên, 1 cử động nhẹ, 2 đi lại/vận động
  bool coSoLieuMoi = false;

  SuKien them(const Mau& m, uint32_t ms) {
    float a = sqrtf(m.ax * m.ax + m.ay * m.ay + m.az * m.az);
    float w = sqrtf(m.gx * m.gx + m.gy * m.gy + m.gz * m.gz);
    const float acc[3] = {m.ax, m.ay, m.az};
    if (!daKhoiDong && a > 0.2f) {
      for (int i = 0; i < 3; i++) g[i] = gCham[i] = acc[i];
      daKhoiDong = true;
    } else {
      for (int i = 0; i < 3; i++) g[i] += 0.05f * (acc[i] - g[i]);  // hướng trọng lực, lọc ≈ 0,4 s
    }

    s1 += a;
    s2 += a * a;
    sw += w;
    if (++n1 == TAN_SO) {
      float tb = s1 / n1;
      vanDong = sqrtf(fmaxf(0, s2 / n1 - tb * tb));
      conQuay = sw / n1;
      muc = (vanDong < 0.03f && conQuay < 15) ? 0 : (vanDong < 0.12f && conQuay < 60) ? 1 : 2;
      coSoLieuMoi = true;
      s1 = s2 = sw = 0;
      n1 = 0;
    }

    switch (tt) {
      case BINH_THUONG:
        for (int i = 0; i < 3; i++) gCham[i] += 0.007f * (g[i] - gCham[i]);  // tư thế trung bình ≈ 3 s
        if (a < ROI_TU_DO_G) batDauNga(ms, DANG_ROI);
        else if (a > VA_CHAM_MANH_G) batDauNga(ms, DA_VA_CHAM);
        break;
      case DANG_ROI:
        if (a > VA_CHAM_G) {
          tt = DA_VA_CHAM;
          moc = ms;
        } else if (ms - moc > 1000) {
          tt = BINH_THUONG;
        }
        break;
      case DA_VA_CHAM:  // chờ 1,5 s cho người nằm yên
        if (ms - moc > 1500) {
          tt = KIEM_TRA;
          moc = ms;
          ta = ta2 = 0;
          nkt = 0;
        }
        break;
      case KIEM_TRA:  // 2 s: nghiêng hẳn so với trước và không còn vận động mạnh
        ta += a;
        ta2 += a * a;
        nkt++;
        if (ms - moc > 2000) {
          float tb = ta / nkt, lech = sqrtf(fmaxf(0, ta2 / nkt - tb * tb));
          if (gocGiua(g, gTruoc) > GOC_NGA_DO && lech < 0.25f) {
            tt = DA_NGA;
            mocDungDay = 0;
            return SuKien::NGA;
          }
          tt = BINH_THUONG;
        }
        break;
      case DA_NGA:
        if (gocGiua(g, gTruoc) < GOC_DUNG_DAY_DO) {
          if (mocDungDay == 0) mocDungDay = ms | 1;
          else if (ms - mocDungDay > 3000) {
            tt = BINH_THUONG;
            return SuKien::DUNG_DAY;
          }
        } else {
          mocDungDay = 0;
        }
        break;
    }
    return SuKien::KHONG;
  }

  void datLai() { tt = BINH_THUONG; }  // người nhà đã huỷ báo động: theo dõi lại từ đầu

 private:
  enum TrangThai { BINH_THUONG, DANG_ROI, DA_VA_CHAM, KIEM_TRA, DA_NGA } tt = BINH_THUONG;
  bool daKhoiDong = false;
  float g[3] = {0, 0, 1}, gCham[3] = {0, 0, 1}, gTruoc[3] = {0, 0, 1};
  uint32_t moc = 0, mocDungDay = 0;
  float ta = 0, ta2 = 0, s1 = 0, s2 = 0, sw = 0;
  int nkt = 0, n1 = 0;

  void batDauNga(uint32_t ms, TrangThai tiep) {
    memcpy(gTruoc, gCham, sizeof gTruoc);
    moc = ms;
    tt = tiep;
  }
};

}  // namespace te_nga
