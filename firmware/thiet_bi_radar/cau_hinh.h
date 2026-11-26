// cau_hinh.h — sửa các dòng dưới đây trước khi nạp code.
#pragma once

// ===== Mạng (giống nhau trên cả 3 thiết bị) =====
#define WIFI_TEN        "GIAOTHONG"
#define WIFI_MAT_KHAU   "12345678"
#define MQTT_MAY_CHU    "172.17.180.109"  // IP máy tính chạy Mosquitto (gõ ipconfig trên máy đó để xem)
#define MQTT_CONG       1883
#define MQTT_TAI_KHOAN  ""              // để trống nếu broker không đặt mật khẩu
#define MQTT_MAT_KHAU   ""
#define MA_NGUOI_DUNG   "bn01"          // mã người đeo, giống nhau trên 3 thiết bị của cùng một người
#define MUI_GIO_GIAY    (7 * 3600)      // giờ Việt Nam (UTC+7)

// ===== Riêng thiết bị radar =====
#define DO_SANG_DEN     40              // độ sáng đèn WS2812B (0–255); không lắp đèn thì để nguyên
#define CHAN_DEN_RGB    D1              // đèn RGB báo trạng thái: bộ kit MR60BHA2 có sẵn đèn ở D1.
                                        // Mạch tự lắp theo Hình C (radar MR60BHA1 + đèn WS2812B rời) thì đặt D0
#define XEM_DO_THI      0               // 1 = in sóng thở/sóng tim ra Serial Plotter
