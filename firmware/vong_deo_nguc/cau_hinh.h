// cau_hinh.h — sửa các dòng dưới đây trước khi nạp code.
#pragma once

// ===== Mạng (giống nhau trên cả 3 thiết bị) =====
#define WIFI_TEN        "Ten_WiFi"
#define WIFI_MAT_KHAU   "mat_khau_wifi"
#define MQTT_MAY_CHU    "172.17.180.109"  // IP máy tính chạy Mosquitto (gõ ipconfig trên máy đó để xem)
#define MQTT_CONG       1883
#define MQTT_TAI_KHOAN  ""              // để trống nếu broker không đặt mật khẩu
#define MQTT_MAT_KHAU   ""
#define MA_NGUOI_DUNG   "bn01"          // mã người đeo, giống nhau trên 3 thiết bị của cùng một người
#define MUI_GIO_GIAY    (7 * 3600)      // giờ Việt Nam (UTC+7), dùng ghi giờ trong tin nhắn

// ===== Riêng vòng đeo ngực =====
#define SDT_MAC_DINH    "+84900000000"  // số người nhà, dùng khi platform chưa gửi số khác qua MQTT
#define SIM_TOC_DO      115200          // module 4G A7670C (đang dùng) và A7680C: 115200. SIM800L: 9600
#define SIM_XUNG_RESET_MS 2500         // giữ chân RESET của module ở mức thấp (A7670C khoảng 2,5 s; SIM800L 200 ms)
#define XEM_DO_THI      0               // 1 = in ECG và sóng thở ra Serial Plotter để kiểm tra điện cực, cảm biến thở
#define CAM_BIEN_THO_PVDF 1             // cảm biến thở ở dây đeo ngực (GPIO39): 1 = màng áp điện PVDF (đang dùng),
                                        // 0 = cảm biến lực FSR402
