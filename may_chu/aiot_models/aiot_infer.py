"""Tiền xử lý + suy luận, dùng chung cho Colab (huấn luyện) và máy chủ platform (chạy thật).

Trên máy chủ (cần: pip install tensorflow scikit-learn scipy joblib, cùng phiên bản scikit-learn với Colab):
    from aiot_infer import RiskModel, BreathDetector
    risk = RiskModel("cnn_gru_fusion.keras", "model_config.json")
    nhan, xac_suat = risk.predict(ecg, 500, ppg, 100, mmw, 5)   # mảng numpy + tần số lấy mẫu thật của thiết bị
    risk.last_info                                               # chất lượng tín hiệu, số đỉnh R, thời gian suy luận
    breath = BreathDetector("breath_iforest.joblib")
    breath.calibrate(fsr_60_giay_dau)                            # 60 giây đầu khi vừa đeo, thở bình thường
    bat_thuong, diem = breath.check(fsr_30_giay_gan_nhat)

Lọc nhiễu (phiên bản 3, 17/11/2026; mô hình huấn luyện với bản 2 vẫn được lọc đúng như bản 2):
    ECG  : chắn điện lưới 50 Hz + 100 Hz (notch), bỏ trôi đường nền bằng 2 bộ lọc trung vị 200 ms rồi 600 ms
           (de Chazal 2004), lọc thông dải 0.5–40 Hz bỏ phần trôi còn lại và nhiễu cơ.
    PPG  : lọc Hampel bỏ gai nhọn do cử động, rồi lọc thông dải 0.5–8 Hz.
    Radar: kênh thở 0.08–0.8 Hz, kênh tim 0.7–2.4 Hz.
    Mọi bộ lọc là Butterworth chạy 2 chiều (zero-phase) nên không làm lệch thời điểm đỉnh R hay sóng mạch.
Đỉnh R: 2 bộ tìm độc lập, Pan–Tompkins (1985) và Elgendi (2013), được ghép lại (xem _ghep_dinh_r).
Chất lượng tín hiệu (0–1): ECG theo độ giống nhau giữa các nhịp (có mẫu thứ 2 cho ngoại tâm thu cùng ổ), độ nhọn và
mức khớp của 2 bộ tìm đỉnh R (bSQI, Li và cộng sự 2008); PPG theo độ giống nhau giữa các sóng mạch + độ lệch
(skewness). Máy chủ bỏ qua đoạn có chất lượng thấp thay vì đưa nhiễu vào mô hình.
Nhịp không đều (nghi rung nhĩ): COSEn của chuỗi khoảng RR (Lake và Moorman 2011).
Kết quả so bản 2 và bản 3 trên MIT-BIH + nhiễu thật NSTDB + AFDB: xem HUONG_DAN.md, mục "Lọc nhiễu và thuật toán bản 3".
"""
import json
import time
from fractions import Fraction

import numpy as np
from scipy import ndimage as ndi
from scipy import signal as sps
from scipy import stats

PREPROCESS_VERSION = 3       # 3: bỏ trôi đường nền bằng lọc trung vị, 2 bộ tìm đỉnh R + bSQI, COSEn (mô hình bản 2 vẫn chạy đúng)
POWERLINE_HZ = 50.0          # điện lưới Việt Nam
_SOS_CACHE = {}
_NOTCH_CACHE = {}
_HINH_CACHE = {}


# ---------------------------------------------------------------- bộ lọc cơ bản
def _filtfilt(sos, x):
    """sosfiltfilt an toàn với đoạn ngắn (tự giảm độ dài đệm)."""
    x = np.asarray(x, dtype=np.float64)
    if x.shape[0] < 4:
        return x
    pad = min(3 * (2 * len(sos) + 1), x.shape[0] - 1)
    return sps.sosfiltfilt(sos, x, axis=0, padlen=pad)


def resample(x, fs_in, fs_out):
    """Đổi tần số lấy mẫu, vd. ECG 500 Hz của AD8232 → 250 Hz.
    padtype="line": nối dài 2 đầu theo xu hướng của tín hiệu thay vì đệm số 0. Số đọc thô của cảm biến
    có mức nền rất lớn (ADC ≈ 1900, PPG ≈ 110000); đệm 0 sẽ tạo bậc nhảy ở cuối đoạn và làm hỏng đầu vào mô hình."""
    x = np.asarray(x, dtype=np.float64)
    if abs(fs_in - fs_out) < 1e-9:
        return x
    fr = Fraction(fs_out / fs_in).limit_denominator(1000)
    return sps.resample_poly(x, fr.numerator, fr.denominator, axis=0, padtype="line")


def bandpass(x, fs, lo, hi, order=3):
    key = ("band", float(fs), float(lo), float(hi), int(order))
    sos = _SOS_CACHE.get(key)
    if sos is None:
        sos = sps.butter(order, [lo, min(hi, 0.45 * fs)], btype="band", fs=fs, output="sos")
        _SOS_CACHE[key] = sos
    return _filtfilt(sos, x)


def lowpass(x, fs, hi, order=2):
    key = ("low", float(fs), float(hi), int(order))
    sos = _SOS_CACHE.get(key)
    if sos is None:
        sos = sps.butter(order, min(hi, 0.45 * fs), btype="low", fs=fs, output="sos")
        _SOS_CACHE[key] = sos
    return _filtfilt(sos, x)


def notch(x, fs, f0=POWERLINE_HZ, q=30.0):
    """Lọc chắn dải hẹp ở tần số điện lưới và hài bậc 2 (nếu còn dưới tần số Nyquist)."""
    x = np.asarray(x, dtype=np.float64)
    for f in (f0, 2 * f0):
        if f < 0.45 * fs and x.shape[0] > 12:
            key = (float(fs), float(f), float(q))
            ba = _NOTCH_CACHE.get(key)
            if ba is None:
                ba = sps.iirnotch(f, q, fs=fs)
                _NOTCH_CACHE[key] = ba
            b, a = ba
            x = sps.filtfilt(b, a, x, axis=0, padlen=min(9, x.shape[0] - 1))
    return x


def despike(x, fs, win_sec=0.17, k=4.0):
    """Lọc Hampel chuẩn theo biên độ AC: bỏ gai nhọn do cử động hoặc lỗi I2C mà giữ nguyên đỉnh sóng mạch."""
    x = np.asarray(x, dtype=np.float64).copy()
    w = max(3, int(win_sec * fs) | 1)
    if x.ndim != 1 or len(x) < w or not np.all(np.isfinite(x)):
        return x
    med = ndi.median_filter(x, size=w, mode="nearest")
    res = np.abs(x - med)
    stride = max(1, int(fs // 25))
    if stride > 1 and len(x) >= 6 * stride:
        ws = max(3, int(round(1.0 * fs / stride)) | 1)
        idx_s, idx_f = np.arange(0, len(x), stride), np.arange(len(x))
        mad = 1.4826 * np.interp(idx_f, idx_s, ndi.median_filter(res[::stride], size=ws, mode="nearest")) + 1e-9
        ac = x - np.interp(idx_f, idx_s, ndi.median_filter(x[::stride], size=ws, mode="nearest"))
    else:
        w_mad = max(w, int(1.0 * fs) | 1)
        mad = 1.4826 * ndi.median_filter(res, size=w_mad, mode="nearest") + 1e-9
        ac = x - ndi.median_filter(x, size=w_mad, mode="nearest")
    ac_span = float(np.percentile(ac, 90) - np.percentile(ac, 10))
    bad = res > np.maximum(k * mad, 0.85 * ac_span)
    x[bad] = med[bad]
    return x


def remove_baseline(x, fs, w1=0.2, w2=0.6):
    """Bỏ trôi đường nền ECG bằng 2 bộ lọc trung vị nối tiếp 200 ms rồi 600 ms (de Chazal và cộng sự, 2004).
    Bộ thứ nhất xoá phức bộ QRS và sóng P, bộ thứ hai xoá sóng T; phần còn lại là đường nền do thở, cử động hay điện
    cực bị kéo, được trừ đi. Khác lọc thông cao, bộ lọc trung vị theo kịp cả bước nhảy đột ngột của đường nền mà không
    làm méo đoạn ST. Tính trên tín hiệu thưa khoảng 50 Hz cho nhanh rồi nội suy lại (đường nền thay đổi chậm)."""
    x = np.asarray(x, dtype=np.float64)
    if x.ndim != 1 or len(x) < int(w2 * fs) + 3 or not np.all(np.isfinite(x)):
        return x
    k = max(1, int(fs // 50))
    xs = x[::k]
    fs_s = fs / k
    nen = ndi.median_filter(xs, size=max(3, int(round(w1 * fs_s)) | 1), mode="nearest")
    nen = ndi.median_filter(nen, size=max(3, int(round(w2 * fs_s)) | 1), mode="nearest")
    if k > 1:
        nen = np.interp(np.arange(len(x)), np.arange(len(xs)) * k, nen)
    return x - nen


def zscore(x, eps=1e-6, clip=None):
    x = np.asarray(x, dtype=np.float64)
    x = x - x.mean(axis=0, keepdims=True)
    x = x / (x.std(axis=0, keepdims=True) + eps)
    return np.clip(x, -clip, clip) if clip else x


def fit_len(x, n):
    """Cắt hoặc đệm cho đủ n điểm."""
    x = np.asarray(x)
    if len(x) >= n:
        return x[:n]
    pad = [(0, n - len(x))] + [(0, 0)] * (x.ndim - 1)
    return np.pad(x, pad, mode="edge")


# ---- lọc từng loại tín hiệu: áp cho cả đoạn dài TRƯỚC khi cắt cửa sổ ----
def prep_ecg(x, fs, version=None):
    """ECG từ AD8232: khử offset DC trung vị, chắn 50/100 Hz điện lưới, (bản 3: bỏ trôi đường nền bằng lọc trung vị),
    lọc 0.5–40 Hz bỏ trôi đường nền còn lại và nhiễu cơ. version: theo mô hình (model_config "preprocess_version")."""
    v = PREPROCESS_VERSION if version is None else int(version)
    arr = np.asarray(x, dtype=np.float64)
    if arr.size > 0 and np.all(np.isfinite(arr)):
        arr = arr - np.median(arr, axis=-1, keepdims=True)
    arr = notch(arr, fs)
    if v >= 3 and arr.ndim == 1:
        arr = remove_baseline(arr, fs)
    return bandpass(arr, fs, 0.5, 40.0)


def prep_ppg(x, fs):
    """PPG từ AS7038RB/MAX30102: khử offset DC trung vị, bỏ gai nhọn (Hampel) rồi lọc 0.5–8 Hz giữ dạng sóng mạch."""
    arr = np.asarray(x, dtype=np.float64)
    if arr.size > 0 and np.all(np.isfinite(arr)):
        arr = arr - np.median(arr, axis=-1, keepdims=True)
    return bandpass(despike(arr, fs), fs, 0.5, 8.0)


def prep_mmw(x, fs=None):
    """Sóng thở + sóng tim của radar MR60BHA1 (0–255, trục giữa 128) → mảng (n, 2).
    Có fs thì lọc riêng từng kênh: thở 0.08–0.8 Hz (5–48 lần/phút), tim 0.7–2.4 Hz (42–144 lần/phút)."""
    x = np.asarray(x, dtype=np.float64) - 128.0
    x = x if x.ndim == 2 else x[:, None]
    if fs is not None and len(x) > 20:
        x = x.copy()
        x[:, 0] = bandpass(x[:, 0], fs, 0.08, 0.8, order=2)
        if x.shape[1] > 1:
            x[:, 1] = bandpass(x[:, 1], fs, 0.7, 2.4, order=2)
    return x


def window_inputs(ecg, ppg, mmw, ecg_len, ppg_len, mmw_len):
    """1 cửa sổ đã lọc → 3 mảng đầu vào của mô hình, chuẩn hoá z-score theo từng cửa sổ (không phụ thuộc độ khuếch
    đại riêng của từng cảm biến), cắt ngọn ở ±12 (ECG) / ±6 (PPG, radar) để 1 gai nhiễu không đè cả cửa sổ."""
    e = zscore(fit_len(ecg, ecg_len), clip=12).reshape(ecg_len, 1)
    p = zscore(fit_len(ppg, ppg_len), clip=6).reshape(ppg_len, 1)
    m = zscore(fit_len(mmw, mmw_len), clip=6).reshape(mmw_len, -1)
    return e.astype(np.float32), p.astype(np.float32), m.astype(np.float32)


# ---------------------------------------------------------------- đỉnh R, sóng mạch, chất lượng tín hiệu
def _pan_tompkins(ecg_f, fs):
    """Bộ tìm đỉnh R thứ nhất, theo Pan–Tompkins (1985), rút gọn:
    lọc 5–15 Hz → đạo hàm → bình phương → tích phân cửa sổ 150 ms → ngưỡng thích nghi (SPK/NPK),
    thời gian trơ 200 ms, tìm lại nhịp bị sót khi khoảng RR dài hơn 1.66 lần trung bình.
    Trả về chỉ số mẫu của từng đỉnh R (tự nhận chiều sóng R lên hay xuống)."""
    x = np.asarray(ecg_f, dtype=np.float64)
    n = len(x)
    if n < 2 * fs or not np.all(np.isfinite(x)) or x.std() < 1e-9:
        return np.array([], dtype=int)
    y = bandpass(x, fs, 5.0, 15.0, order=2)
    e = np.gradient(y) ** 2
    w = max(1, int(0.15 * fs))
    mwi = np.convolve(e, np.ones(w) / w, mode="same")
    cand, _ = sps.find_peaks(mwi, distance=max(1, int(0.2 * fs)))
    if len(cand) == 0:
        return np.array([], dtype=int)
    head = mwi[: int(2 * fs)]
    spk, npk = min(0.33 * head.max(), float(np.percentile(head, 95))), float(np.median(head))
    thr = npk + 0.25 * (spk - npk)
    tro = int(0.2 * fs)
    t_wave_win = int(0.36 * fs)
    peaks, rr_avg = [], None
    for c in cand:
        v = mwi[c]
        last_t = peaks[-1] if peaks else 0
        if c - last_t > int(2.5 * fs) and spk > npk:
            spk = npk + 0.5 * (spk - npk)                        # quá 2,5 s chưa thấy nhịp (sau gai nhiễu): hạ ngưỡng
            thr = npk + 0.25 * (spk - npk)
        if v > thr:
            if peaks and c - peaks[-1] < tro:            # trong thời gian trơ: giữ đỉnh lớn hơn
                if v > mwi[peaks[-1]]:
                    peaks[-1] = c
                continue
            ref_amp = min(mwi[peaks[-1]], 2.0 * spk) if peaks and spk > 0 else (mwi[peaks[-1]] if peaks else v)
            if peaks and c - peaks[-1] < t_wave_win and v < 0.35 * ref_amp:
                npk = 0.125 * v + 0.875 * npk            # sóng T cao sớm trong 360 ms: coi là nhiễu, không đếm đúp
                thr = npk + 0.25 * (spk - npk)
                continue
            if peaks and rr_avg and c - peaks[-1] > 1.66 * rr_avg:   # có thể đã sót 1 nhịp: tìm lại, ngưỡng 1/2
                lo_, hi_ = np.searchsorted(cand, peaks[-1] + tro, "right"), np.searchsorted(cand, c - tro, "left")
                sot = [q for q in cand[lo_:hi_] if mwi[q] > 0.5 * thr]
                if sot:
                    q = max(sot, key=lambda k: mwi[k])
                    peaks.append(q)
                    spk = 0.25 * mwi[q] + 0.75 * spk
            peaks.append(c)
            v_cap = min(v, 3.0 * spk) if spk > 0 else v
            spk = 0.125 * v_cap + 0.875 * spk
            if len(peaks) >= 3:
                rr_avg = float(np.median(np.diff(peaks[-9:])))
        else:
            npk = 0.125 * v + 0.875 * npk
        thr = npk + 0.25 * (spk - npk)
    if not peaks:
        return np.array([], dtype=int)
    return _chinh_dinh_r(x, peaks, fs)


def _chinh_dinh_r(x, peaks, fs):
    """Dời mỗi vị trí tìm được về đúng đỉnh sóng R trên ECG (trong ±80 ms), tự nhận chiều sóng R lên hay xuống."""
    n, tro = len(x), int(0.2 * fs)
    half = max(1, int(0.08 * fs))
    doan = [(max(0, int(p) - half), min(n, int(p) + half)) for p in peaks]
    cao_len = np.median([x[a:b].max() for a, b in doan])
    xuong = np.median([-x[a:b].min() for a, b in doan])
    s = 1.0 if cao_len >= xuong else -1.0
    r = np.unique([a + int(np.argmax(s * x[a:b])) for a, b in doan])
    if len(r) > 1:
        r = r[np.concatenate([[True], np.diff(r) >= tro])]
    return r.astype(int)


def detect_r_peaks_elgendi(ecg_f, fs):
    """Bộ tìm đỉnh R thứ hai, độc lập với Pan–Tompkins: 2 đường trung bình trượt (Elgendi, 2013).
    Lọc 8–20 Hz rồi bình phương; so trung bình trượt 97 ms (cỡ 1 phức bộ QRS) với trung bình trượt 611 ms (cỡ 1 nhịp)
    cộng ngưỡng 0,08 lần năng lượng trung bình. Đoạn nào vượt ngưỡng và dài ít nhất 97 ms là 1 phức bộ QRS;
    đỉnh R là điểm năng lượng lớn nhất trong đoạn. Không có ngưỡng thích nghi nên sai theo kiểu khác Pan–Tompkins."""
    x = np.asarray(ecg_f, dtype=np.float64)
    if len(x) < 2 * fs or not np.all(np.isfinite(x)) or x.std() < 1e-9:
        return np.array([], dtype=int)
    y = bandpass(x, fs, 8.0, 20.0, order=3)
    y2 = y * y
    w1, w2 = max(1, int(round(0.097 * fs))), max(1, int(round(0.611 * fs)))
    ma_qrs = ndi.uniform_filter1d(y2, w1, mode="nearest")
    ma_nhip = ndi.uniform_filter1d(y2, w2, mode="nearest")
    khoi = (ma_qrs > ma_nhip + 0.08 * float(y2.mean())).astype(np.int8)
    d = np.diff(np.concatenate([[0], khoi, [0]]))
    tro, peaks = int(0.2 * fs), []
    for a, b in zip(np.flatnonzero(d == 1), np.flatnonzero(d == -1)):
        if b - a < w1:
            continue
        p = a + int(np.argmax(y2[a:b]))
        if peaks and p - peaks[-1] < tro:
            if y2[p] > y2[peaks[-1]]:
                peaks[-1] = p
            continue
        peaks.append(p)
    if not peaks:
        return np.array([], dtype=int)
    return _chinh_dinh_r(x, peaks, fs)


def bsqi(r1, r2, fs, tol=0.15):
    """Mức khớp của 2 bộ tìm đỉnh R (bSQI, Li, Mark và Clifford 2008): số nhịp cả 2 cùng thấy (lệch ≤ 150 ms) chia
    cho tổng số nhịp khác nhau mà 2 bộ tìm được. ECG sạch gần bằng 1; nhiễu làm 2 bộ thấy khác nhau nên giảm mạnh."""
    r1, r2 = np.sort(np.asarray(r1, dtype=np.int64)), np.sort(np.asarray(r2, dtype=np.int64))
    if len(r1) == 0 or len(r2) == 0:
        return 0.0
    j = np.searchsorted(r2, r1)
    gan = np.minimum(np.abs(r1 - r2[np.clip(j - 1, 0, len(r2) - 1)]), np.abs(r2[np.clip(j, 0, len(r2) - 1)] - r1))
    khop = int(np.sum(gan <= tol * fs))
    return float(khop / max(len(r1) + len(r2) - khop, 1))


_R_CACHE = [None, None]


def _hai_bo_tim(x, fs):
    """Đỉnh R của cả 2 bộ tìm trên cùng 1 đoạn; nhớ đoạn vừa tính để ecg_quality không phải tính lại."""
    n = len(x)
    key = (n, float(fs), float(x[0]), float(x[n // 3]), float(x[(2 * n) // 3]), float(x[-1]), float(x.sum()))
    if _R_CACHE[0] != key:
        _R_CACHE[0], _R_CACHE[1] = key, (_pan_tompkins(x, fs), detect_r_peaks_elgendi(x, fs))
    return _R_CACHE[1]


def _ghep_dinh_r(pt, el, fs, k=0.75):
    """Ghép 2 bộ tìm đỉnh R.
    - 2 bộ khớp nhau ≥ 90 % nhịp (ECG sạch): dùng kết quả của Elgendi, bộ chính xác hơn trên ECG sạch.
    - Có nhiễu: giữ nhịp cả 2 bộ cùng thấy; nhịp chỉ 1 bộ thấy chỉ được thêm nếu cách mọi nhịp đã giữ ít nhất
      0,75 lần RR trung vị. Đỉnh giả do nhiễu thường nằm sát một nhịp thật nên bị loại."""
    if len(el) == 0 or len(pt) == 0:
        return el if len(el) else pt
    if bsqi(pt, el, fs) >= 0.9:
        return el
    tol = 0.15 * fs
    j = np.searchsorted(pt, el)
    gan = np.minimum(np.abs(el - pt[np.clip(j - 1, 0, len(pt) - 1)]), np.abs(pt[np.clip(j, 0, len(pt) - 1)] - el))
    khop = el[gan <= tol]
    if len(khop) < 3:
        return el
    rr = float(np.median(np.diff(khop)))
    giu = [int(q) for q in khop]
    le = [int(q) for q in el[gan > tol]] + [int(p) for p in pt if np.min(np.abs(khop - p)) > tol]
    for q in sorted(le):
        if np.min(np.abs(np.array(giu) - q)) >= k * rr:
            giu.append(q)
    return np.sort(np.array(giu, dtype=int))


def detect_r_peaks(ecg_f, fs, version=None):
    """Đỉnh R trên ECG ĐÃ LỌC (chỉ số mẫu). Bản 2: Pan–Tompkins. Bản 3: ghép Pan–Tompkins với bộ của Elgendi
    (xem _ghep_dinh_r). version: theo mô hình (model_config "preprocess_version"), mặc định bản mới nhất."""
    v = PREPROCESS_VERSION if version is None else int(version)
    x = np.asarray(ecg_f, dtype=np.float64)
    if v < 3:
        return _pan_tompkins(x, fs)
    if len(x) < 2 * fs or not np.all(np.isfinite(x)) or x.std() < 1e-9:
        return np.array([], dtype=int)
    pt, el = _hai_bo_tim(x, fs)
    return _ghep_dinh_r(pt, el, fs)


def _sqi_penalties(x, fs, gstd, sqi, c_lo, c_hi, nyq_cap, hf_thr, hf_slope, ent_hi=None):
    """Phạt chung cho ECG/PPG: block 1 s bị phẳng mất tiếp xúc + tỷ lệ nhiễu HF (+ entropy phổ nếu có)."""
    step = max(1, int(fs))
    nb = len(x) // step
    if nb >= 4:
        blocks = x[: nb * step].reshape(nb, step)
        flat_frac = float(np.mean(blocks.std(axis=1) < 0.03 * gstd))
        if flat_frac > 0.45:
            sqi *= float(np.clip((0.75 - flat_frac) / 0.30, 0.0, 1.0))
    freqs = np.fft.rfftfreq(len(x), 1.0 / fs)
    pwr = np.abs(np.fft.rfft(x - x.mean())) ** 2
    p_card = float(pwr[(freqs >= c_lo) & (freqs < c_hi)].sum())
    nyq_hi = min(nyq_cap, 0.49 * fs)
    if p_card > 1e-12 and nyq_hi > c_hi:
        hf_ratio = float(pwr[(freqs >= c_hi) & (freqs <= nyq_hi)].sum()) / p_card
        if hf_ratio > hf_thr:
            sqi *= float(np.clip(1.0 + hf_slope * (hf_thr - hf_ratio), 0.2, 1.0))
    if ent_hi is not None:
        p_band = pwr[(freqs >= c_lo) & (freqs <= min(ent_hi, nyq_hi))]
        if len(p_band) >= 4 and float(p_band.sum()) > 1e-12:
            pb = p_band / float(p_band.sum())
            ent = float(-(pb * np.log(pb + 1e-12)).sum() / np.log(len(pb)))
            if ent > 0.78:
                sqi *= float(np.clip((0.95 - ent) / 0.17, 0.15, 1.0))
    return round(float(sqi), 3)


def ecg_quality(ecg_f, fs, r=None, version=None):
    """Chất lượng ECG 0–1: hình thái nhịp, độ nhọn QRS, tỷ lệ nhiễu cơ 25–40 Hz và mức mất tiếp xúc điện cực.
    Bản 3 thêm: (1) mức khớp của 2 bộ tìm đỉnh R (bSQI); (2) nhịp lạ nhưng giống nhau (ngoại tâm thu cùng một ổ)
    được so với mẫu nhịp thứ hai nên không bị coi là nhiễu, tránh bỏ qua đúng những đoạn có rối loạn nhịp."""
    v = PREPROCESS_VERSION if version is None else int(version)
    x = np.asarray(ecg_f, dtype=np.float64)
    gstd = float(x.std()) if len(x) > 0 and np.all(np.isfinite(x)) else 0.0
    if len(x) < 2 * fs or not np.all(np.isfinite(x)) or gstd < 1e-9:
        return 0.0
    if r is None:
        r = detect_r_peaks(x, fs, version=v)
    a, b = int(0.2 * fs), int(0.4 * fs)
    beats = np.array([x[i - a:i + b] for i in r if i - a >= 0 and i + b <= len(x)])
    if len(beats) < 3:
        return 0.0
    hr = 60.0 / np.median(np.diff(r) / fs)
    if not 30 <= hr <= 220:
        return 0.0
    tpl = np.median(beats, axis=0)
    tc = tpl - tpl.mean()
    bc = beats - beats.mean(axis=1, keepdims=True)
    corr = (bc @ tc) / (np.linalg.norm(bc, axis=1) * np.linalg.norm(tc) + 1e-12)
    if v >= 3:
        lech = corr < 0.8
        if lech.sum() >= 2:
            t2 = np.median(beats[lech], axis=0)
            t2c = t2 - t2.mean()
            corr2 = (bc @ t2c) / (np.linalg.norm(bc, axis=1) * np.linalg.norm(t2c) + 1e-12)
            if float(np.median(corr2[lech])) >= 0.8:          # các nhịp lạ giống nhau: ngoại tâm thu thật, không phải nhiễu
                corr = np.maximum(corr, corr2)
    k = float(stats.kurtosis(x, fisher=False))
    sqi = 0.75 * float(np.mean(np.clip(corr, 0, 1))) + 0.25 * float(np.clip((k - 3.0) / 5.0, 0, 1))
    if v >= 3:
        pt, el = _hai_bo_tim(x, fs)
        sqi *= float(np.clip((bsqi(pt, el, fs) - BSQI_DUOI) / (BSQI_DU - BSQI_DUOI), 0.0, 1.0))
    return _sqi_penalties(x, fs, gstd, sqi, 2.0, 25.0, 40.0, 0.4, 0.9)


BSQI_DUOI, BSQI_DU = 0.5, 0.9      # 2 bộ khớp ≥ 90 % nhịp: giữ nguyên chất lượng; ≤ 50 %: coi là nhiễu hoàn toàn


def detect_ppg_peaks(ppg_f, fs):
    """Đỉnh sóng mạch trên PPG đã lọc (đỉnh hướng lên), cách nhau ≥ 0.33 s (dùng MAD chống gai nhiễu)."""
    x = np.asarray(ppg_f, dtype=np.float64)
    if len(x) < 2 * fs or not np.all(np.isfinite(x)) or x.std() < 1e-9:
        return np.array([], dtype=int)
    mad_std = 1.4826 * float(np.median(np.abs(x - np.median(x))))
    scale = mad_std if mad_std > 1e-9 else float(x.std())
    scale = min(scale, float(x.std()))
    pk, _ = sps.find_peaks(x, distance=max(1, int(0.33 * fs)), prominence=max(1e-9, 0.35 * scale))
    return pk


def subpixel_peak(arr, idx):
    """Nội suy parabol 3 điểm quanh chỉ số đỉnh để giảm sai số lượng tử hoá thời gian."""
    idx = int(idx)
    if 0 < idx < len(arr) - 1:
        denom = arr[idx - 1] - 2.0 * arr[idx] + arr[idx + 1]
        if denom < -1e-12:
            return float(idx) + float(np.clip(0.5 * (arr[idx - 1] - arr[idx + 1]) / denom, -0.5, 0.5))
    return float(idx)


def ppg_upstrokes(ppg_f, fs, peaks=None, subpixel=False):
    """Điểm sườn lên dốc nhất của từng sóng mạch: mốc thời gian 'mạch đến' ổn định hơn chân sóng."""
    x = np.asarray(ppg_f, dtype=np.float64)
    if peaks is None:
        peaks = detect_ppg_peaks(x, fs)
    d = np.gradient(x)
    out = []
    for p in peaks:
        a = max(0, int(p) - int(0.3 * fs))
        if int(p) - a >= 2:
            idx = a + int(np.argmax(d[a:int(p)]))
            out.append(subpixel_peak(d, idx) if subpixel else idx)
    return np.array(out, dtype=np.float64 if subpixel else int)


def ppg_quality(ppg_f, fs, peaks=None):
    """Chất lượng PPG 0–1: độ giống nhau sóng mạch (80%), skewness (20%), phạt đoạn phẳng & rung cổ tay HF."""
    x = np.asarray(ppg_f, dtype=np.float64)
    gstd = float(x.std()) if len(x) > 0 and np.all(np.isfinite(x)) else 0.0
    if len(x) < 2 * fs or not np.all(np.isfinite(x)) or gstd < 1e-9:
        return 0.0
    if peaks is None:
        peaks = detect_ppg_peaks(x, fs)
    if len(peaks) < 3:
        return 0.0
    hr = 60.0 / np.median(np.diff(peaks) / fs)
    if not 30 <= hr <= 220:
        return 0.0
    song = [np.interp(np.linspace(0, b - a - 1, 40), np.arange(b - a), x[a:b])
            for a, b in zip(peaks[:-1], peaks[1:]) if 0.3 * fs <= b - a <= 2.0 * fs]
    if len(song) < 2:
        return 0.0
    P = np.array(song)
    tpl = np.median(P, axis=0)
    tc, pc = tpl - tpl.mean(), P - P.mean(axis=1, keepdims=True)
    corr = (pc @ tc) / (np.linalg.norm(pc, axis=1) * np.linalg.norm(tc) + 1e-12)
    s = float(stats.skew(x))
    sqi = 0.8 * float(np.mean(np.clip(corr, 0, 1))) + 0.2 * float(np.clip((s + 0.2) / 0.8, 0, 1))
    return _sqi_penalties(x, fs, gstd, sqi, 0.5, 4.2, 15.0, 0.35, 0.8, ent_hi=8.0)


# ---------------------------------------------------------------- biến thiên nhịp tim (HRV), nhịp không đều
HRV_FEATURES = ["hr", "sdnn_ms", "rmssd_ms", "nrmssd", "pnn50", "tpr", "shannon", "n_beats"]
# Bản 3: thêm 6 đặc trưng nhắm vào lớp "Bất thường nhẹ" (ngoại tâm thu, block nhánh):
#   rr_min_ratio / rr_max_ratio: khoảng RR ngắn nhất / dài nhất so với trung vị (nhịp đến sớm, khoảng nghỉ bù)
#   premature_frac: tỉ lệ nhịp "đến sớm rồi nghỉ bù" (kiểu ngoại tâm thu)
#   qrs_ms: độ rộng phức bộ QRS của nhịp trung vị (block nhánh, nhịp thất thường > 120 ms)
#   corr_min, corr_low_frac: nhịp khác hình dạng nhất, tỉ lệ nhịp khác hình dạng (ngoại tâm thu thất)
MORPH_FEATURES = ["rr_min_ratio", "rr_max_ratio", "premature_frac", "qrs_ms", "corr_min", "corr_low_frac"]
HRV_FEATURES_V3 = HRV_FEATURES + MORPH_FEATURES


def rr_intervals(r, fs):
    rr = np.diff(np.asarray(r, dtype=np.float64)) / fs
    return rr[(rr >= 0.25) & (rr <= 2.5)]


def morph_features(x, fs, r):
    """6 đặc trưng hình thái nhịp + RR ngoại tâm thu (xem MORPH_FEATURES) của 1 cửa sổ ECG đã lọc."""
    rr = rr_intervals(r, fs)
    if len(rr) >= 3:
        med = float(np.median(rr))
        f = [rr.min() / med, rr.max() / med, float(np.sum((rr[:-1] < 0.85 * med) & (rr[1:] > 1.1 * med)) / len(rr))]
    else:
        f = [1.0, 1.0, 0.0]
    x = np.asarray(x, dtype=np.float64)
    a, b = int(0.2 * fs), int(0.4 * fs)
    B = np.array([x[i - a:i + b] for i in r if i - a >= 0 and i + b <= len(x)])
    if len(B) < 3:
        return np.array(f + [0.0, 0.0, 0.0], dtype=np.float32)
    T = np.median(B, axis=0)
    tc, bc = T - T.mean(), B - B.mean(axis=1, keepdims=True)
    corr = (bc @ tc) / (np.linalg.norm(bc, axis=1) * np.linalg.norm(tc) + 1e-12)
    e = np.abs(np.gradient(T))
    lo, hi = max(0, a - int(0.15 * fs)), min(len(T), a + int(0.15 * fs))
    on = np.flatnonzero(e[lo:hi] > 0.15 * e[lo:hi].max())
    qrs = (on[-1] - on[0] + 1) / fs * 1000 if len(on) else 0.0
    return np.array(f + [qrs, float(corr.min()), float(np.mean(corr < 0.8))], dtype=np.float32)


def hrv_features(r, fs, x=None, names=None):
    """Đặc trưng nhịp của 1 cửa sổ. Mặc định 8 đặc trưng HRV: nhịp tim, SDNN, RMSSD, RMSSD/RR trung bình, pNN50,
    tỉ lệ điểm ngoặt (TPR), entropy Shannon của RR, số nhịp (rung nhĩ làm RMSSD, TPR, entropy tăng rõ).
    names = HRV_FEATURES_V3 và x = ECG đã lọc: thêm 6 đặc trưng hình thái nhịp."""
    if names is not None and list(names) != HRV_FEATURES:
        d = dict(zip(HRV_FEATURES, hrv_features(r, fs)))
        d.update(zip(MORPH_FEATURES, morph_features(x, fs, r) if x is not None else [1, 1, 0, 0, 0, 0]))
        return np.array([d[n] for n in names], dtype=np.float32)
    rr = rr_intervals(r, fs)
    if len(rr) < 3:
        return np.zeros(len(HRV_FEATURES), dtype=np.float32)
    d = np.diff(rr)
    mean = float(rr.mean())
    rmssd = float(np.sqrt(np.mean(d ** 2)))
    tpr = float(np.sum((rr[1:-1] - rr[:-2]) * (rr[1:-1] - rr[2:]) > 0) / max(len(rr) - 2, 1))
    hist, _ = np.histogram(rr, bins=8)
    p = hist[hist > 0] / hist.sum()
    sh = float(-(p * np.log(p)).sum() / np.log(8))
    return np.array([60.0 / mean, 1000 * rr.std(), 1000 * rmssd, rmssd / mean, float(np.mean(np.abs(d) > 0.05)),
                     tpr, sh, len(rr) + 1], dtype=np.float32)


def cosen(rr, r=0.03):
    """Hệ số entropy mẫu COSEn (Lake và Moorman, 2011) của chuỗi khoảng RR (giây), chiều nhúng m = 1, dung sai
    r = 30 ms: COSEn = SampEn + ln(2r) − ln(RR trung bình). SampEn đo mức khó đoán của chuỗi; trừ ln(RR trung bình)
    để không phụ thuộc nhịp nhanh hay chậm. Rung nhĩ làm khoảng RR ngẫu nhiên nên COSEn cao. Trả None nếu < 12 RR."""
    rr = np.asarray(rr, dtype=np.float64)
    rr = rr[(rr >= 0.25) & (rr <= 2.5)]
    if len(rr) < 12:
        return None
    a, b = rr[:-1], rr[1:]
    da = np.abs(a[:, None] - a[None, :]) <= r
    iu = np.triu_indices(len(a), 1)
    B = int(da[iu].sum())
    if B == 0:
        return None
    A = int((da & (np.abs(b[:, None] - b[None, :]) <= r))[iu].sum())
    samp = -np.log(max(A, 1) / B)             # A = 0 (chuỗi rất ngẫu nhiên): lấy cận trên
    return float(samp + np.log(2 * r) - np.log(rr.mean()))


def rr_irregularity(rr, version=None):
    """Đánh giá nhịp không đều trên ≥ 40 khoảng RR (giây), theo Dash và cộng sự (2009):
    RMSSD/RR trung bình > 0.1, entropy Shannon (16 ô) > 0.7 và tỉ lệ điểm ngoặt (TPR) nằm trong khoảng của một chuỗi
    ngẫu nhiên (2/3 ± 2.5 độ lệch chuẩn, tính theo số nhịp) → nghi rung nhĩ.
    Bỏ 5 % khoảng RR dài nhất và ngắn nhất trước khi tính để ngoại tâm thu lẻ tẻ không gây báo nhầm.
    Bản 3: quyết định bằng COSEn (Lake và Moorman, 2011), xem _quyet_dinh_rung_nhi; kết quả luật Dash vẫn trả về
    ở khoá "dash" để so sánh."""
    v = PREPROCESS_VERSION if version is None else int(version)
    rr = np.asarray(rr, dtype=np.float64)
    rr = rr[(rr >= 0.25) & (rr <= 2.5)]
    if len(rr) < 40:
        return None
    tpr = float(np.sum((rr[1:-1] - rr[:-2]) * (rr[1:-1] - rr[2:]) > 0) / (len(rr) - 2))
    k = max(1, int(0.05 * len(rr)))
    g = np.sort(rr)[k:-k]
    d = np.diff(rr)
    d = d[np.abs(d) <= np.percentile(np.abs(d), 95)]
    nrmssd = float(np.sqrt(np.mean(d ** 2)) / g.mean())
    hist, _ = np.histogram(g, bins=16)
    p = hist[hist > 0] / hist.sum()
    sh = float(-(p * np.log(p)).sum() / np.log(16))
    n = len(rr)
    lech = 2.5 * np.sqrt((16 * n - 29) / 90.0) / (n - 2)       # độ lệch chuẩn của TPR với chuỗi ngẫu nhiên
    dash = nrmssd > 0.1 and sh > 0.7 and abs(tpr - 2.0 / 3.0) <= lech
    out = {"nrmssd": round(nrmssd, 3), "shannon": round(sh, 3), "tpr": round(tpr, 3),
           "sdnn_ms": round(1000 * float(rr.std()), 1), "rmssd_ms": round(1000 * float(np.sqrt(np.mean(np.diff(rr) ** 2))), 1),
           "khong_deu": bool(dash), "so_nhip": int(len(rr) + 1)}
    if v >= 3:
        ce = cosen(rr)
        out["cosen"] = None if ce is None else round(ce, 3)
        out["dash"] = bool(dash)
        out["khong_deu"] = bool(_quyet_dinh_rung_nhi(nrmssd, ce))
    return out


def _quyet_dinh_rung_nhi(nrmssd, ce):
    """Bản 3: nghi rung nhĩ khi COSEn > -1,3 (khoảng RR ngẫu nhiên, khó đoán) và RMSSD/RR trung bình > 0,1
    (RR dao động đủ lớn). Trên đoạn 90 giây của AFDB + MIT-BIH: độ nhạy 94,0 %, độ đặc hiệu 97,5 %, so với 88,8 % và
    92,7 % của luật Dash; ngưỡng chọn trên lưới -1,4 … -1,0 và mọi ngưỡng từ -1,3 đến -1,2 đều tốt hơn Dash."""
    return ce is not None and ce > COSEN_NGUONG and nrmssd > 0.1


COSEN_NGUONG = -1.3                # Lake và Moorman (2011) dùng -1,4 cho đoạn 12 nhịp; đoạn 90 giây dùng -1,3


def pulse_arrival_time(r_times, up_times, lo=0.08, hi=0.45):
    """Thời gian mạch đến (giây): từ đỉnh R (ECG ở ngực) tới sườn lên của sóng mạch cổ tay ngay sau đó.
    Huyết áp tăng thì thành mạch căng, sóng mạch đi nhanh hơn, thời gian này ngắn lại. Trả về trung vị hoặc None."""
    r_times, up_times = np.asarray(r_times, dtype=np.float64), np.asarray(up_times, dtype=np.float64)
    if len(r_times) < 3 or len(up_times) < 3:
        return None
    j = np.searchsorted(up_times, r_times + lo)
    ok = j < len(up_times)
    d = up_times[j[ok]] - r_times[ok]
    d = d[(d >= lo) & (d <= hi)]
    if len(d) < 3:
        return None
    if len(d) >= 4 and float(np.percentile(d, 75) - np.percentile(d, 25)) > 0.08:
        return None
    return float(np.median(d))


# ---------------------------------------------------------------- mel-spectrogram của ECG
MEL_CFG = {"n_fft": 512, "hop": 64, "n_mels": 32, "fmin": 0.5, "fmax": 40.0}
_FB_CACHE = {}


def mel_filterbank(fs, n_fft, n_mels, fmin, fmax):
    key = (fs, n_fft, n_mels, fmin, fmax)
    if key not in _FB_CACHE:
        def hz2mel(f):
            return 2595.0 * np.log10(1.0 + np.asarray(f) / 700.0)

        def mel2hz(m):
            return 700.0 * (10 ** (np.asarray(m) / 2595.0) - 1.0)

        freqs = np.linspace(0, fs / 2, n_fft // 2 + 1)
        pts = mel2hz(np.linspace(hz2mel(fmin), hz2mel(min(fmax, fs / 2)), n_mels + 2))
        fb = np.zeros((n_mels, len(freqs)))
        for i in range(n_mels):
            lo, c, hi = pts[i], pts[i + 1], pts[i + 2]
            fb[i] = np.clip(np.minimum((freqs - lo) / (c - lo + 1e-12), (hi - freqs) / (hi - c + 1e-12)), 0, None)
        fb /= np.maximum(fb.sum(axis=1, keepdims=True), 1e-12)
        _FB_CACHE[key] = fb
    return _FB_CACHE[key]


def mel_shape(ecg_len, cfg=None):
    c = cfg or MEL_CFG
    return c["n_mels"], 1 + (ecg_len - c["n_fft"]) // c["hop"]


def log_mel(x, fs, cfg=None):
    """Chuyển 1 cửa sổ ECG đã lọc sang miền tần số: mel-spectrogram (log), chuẩn hoá z-score → ảnh (n_mels, số khung).
    Cửa sổ Hann 512 điểm (~2 s ở 250 Hz), bước 64 điểm; 32 dải mel trong 0.5–40 Hz."""
    c = cfg or MEL_CFG
    x = np.asarray(x, dtype=np.float64)
    _, _, Z = sps.stft(x, fs=fs, window="hann", nperseg=c["n_fft"], noverlap=c["n_fft"] - c["hop"],
                       boundary=None, padded=False)
    S = np.log(mel_filterbank(fs, c["n_fft"], c["n_mels"], c["fmin"], c["fmax"]) @ (np.abs(Z) ** 2) + 1e-10)
    return ((S - S.mean()) / (S.std() + 1e-6)).astype(np.float32)


def extra_inputs(ecg_win, fs, use_mel=True, use_hrv=True, mel_cfg=None, hrv_names=None, r=None):
    """Đầu vào phụ tính từ cửa sổ ECG đã lọc: ảnh mel-spectrogram (cho nhánh 2D-CNN) và đặc trưng nhịp (HRV)."""
    out = {}
    if use_mel:
        out["ecg_mel"] = log_mel(ecg_win, fs, mel_cfg)[..., None]
    if use_hrv:
        if r is None:
            r = detect_r_peaks(ecg_win, fs)
        out["hrv"] = hrv_features(r, fs, ecg_win, hrv_names)
    return out


# ---------------------------------------------------------------- phân loại từng nhịp: N / S / V
BEAT_CLASSES = ["N", "S", "V"]      # N bình thường (cả block nhánh), S ngoại tâm thu trên thất, V ngoại tâm thu thất
BEAT_FEATURES = (["pre_rr", "post_rr", "pre_tb10", "post_tb10", "pre_dai", "post_dai", "pre_post", "qrs_ms", "corr_mau",
                  "lech_mau", "bien_do_r", "dien_tich_qrs", "song_p", "bien_do_t", "da_hoc"]
                 + [f"hinh_{i}" for i in range(16)])
WINDOW_BEAT_FEATURES = ["so_s", "so_v", "max_p_s", "max_p_v", "tb_p_s", "tb_p_v", "so_nhip"]


class BeatTracker:
    """Đặc trưng từng nhịp, chạy liên tục cho MỘT người (de Chazal 2004 + hình dạng nhịp):
    khoảng RR trước/sau, so với 10 nhịp trước và với RR dài hạn (~5 phút), độ rộng QRS, sóng P, sóng T,
    và độ giống với MẪU NHỊP RIÊNG của người đó (trung bình trượt, tự học, đa số là nhịp bình thường).
    Chỉ dùng thông tin quá khứ (nhịp sau chỉ cần để biết RR sau), nên máy chủ chạy được theo thời gian thực;
    notebook dùng đúng lớp này để tạo dữ liệu huấn luyện."""

    def __init__(self, fs):
        self.fs = fs
        self.a, self.b = int(0.25 * fs), int(0.45 * fs)
        self.tpl, self.rr_dai, self.rr_tb, self.dem = None, None, [], 0
        self.r_truoc = None

    def _dac_trung(self, seg, pre, post):
        fs, A_ = self.fs, self.a
        tb10 = float(np.mean(self.rr_tb[-10:])) if self.rr_tb else pre
        self.rr_dai = pre if self.rr_dai is None else 0.995 * self.rr_dai + 0.005 * pre
        if self.tpl is None:
            self.tpl = seg.copy()
        tpl = self.tpl
        ra = max(abs(tpl[A_]), 1e-6)
        tc, sc = tpl - tpl.mean(), seg - seg.mean()
        corr = float(sc @ tc / (np.linalg.norm(sc) * np.linalg.norm(tc) + 1e-12))
        e = np.abs(np.gradient(seg))
        lo, hi = A_ - int(0.15 * fs), A_ + int(0.15 * fs)
        on = np.flatnonzero(e[lo:hi] > 0.15 * e[lo:hi].max())
        qrs = (on[-1] - on[0] + 1) / fs * 1000 if len(on) else 0.0
        pw = slice(A_ - int(0.22 * fs), A_ - int(0.06 * fs))
        p_nl = np.sum(seg[pw] ** 2) / (np.sum(tpl[pw] ** 2) + 1e-9)
        tw = seg[A_ + int(0.25 * fs):]
        n_seg = len(seg)
        lut = _HINH_CACHE.get(n_seg)
        if lut is None:
            lut = (np.linspace(0, n_seg - 1, 16), np.arange(n_seg, dtype=np.float64))
            _HINH_CACHE[n_seg] = lut
        hinh = np.interp(lut[0], lut[1], seg) / ra
        f = [pre, post, pre / tb10, post / tb10, pre / self.rr_dai, post / self.rr_dai, pre / post, qrs, corr,
             float(np.sqrt(np.mean((seg - tpl) ** 2)) / ra), (seg[A_] if tpl[A_] >= 0 else -seg[A_]) / ra,
             float(np.sum(np.abs(seg[lo:hi])) / (np.sum(np.abs(tpl[lo:hi])) + 1e-9)), float(np.log(p_nl + 1e-6)),
             float(tw.max() - tw.min()) / ra, min(self.dem, 200) / 200.0, *hinh]
        if self.dem < 10 or corr >= 0.55:       # tránh ngoại tâm thu thất / nhiễu làm hỏng mẫu nhịp bình thường
            self.tpl = 0.98 * tpl + 0.02 * seg
            self.rr_tb = (self.rr_tb + [pre])[-10:]
        self.dem += 1
        return f

    def process(self, x, r, t0=0.0):
        """x: ECG đã lọc; r: chỉ số đỉnh R trong x; t0: thời điểm (giây) của mẫu đầu.
        Xử lý các nhịp chưa xử lý ở lần gọi trước (cần có nhịp kế tiếp). Trả (thời điểm giây, ma trận đặc trưng)."""
        x = np.asarray(x, dtype=np.float64)
        if self.r_truoc is not None and (t0 + len(x) / self.fs < self.r_truoc - 1.0 or t0 > self.r_truoc + 120.0):
            self.__init__(self.fs)                      # đồng hồ thiết bị chạy lùi hoặc nghỉ quá lâu: học lại từ đầu
        times, feats = [], []
        for i in range(1, len(r) - 1):
            t = t0 + r[i] / self.fs
            if self.r_truoc is not None and t <= self.r_truoc + 0.15:
                continue
            a, b = r[i] - self.a, r[i] + self.b
            if a < 0 or b > len(x):
                continue
            pre, post = (r[i] - r[i - 1]) / self.fs, (r[i + 1] - r[i]) / self.fs
            self.r_truoc = t
            if not (0.15 < pre < 3 and 0.15 < post < 3):
                continue
            seg = x[a:b] - np.median(x[a:b])
            feats.append(self._dac_trung(seg, pre, post))
            times.append(t)
        F = np.array(feats, dtype=np.float32) if feats else np.zeros((0, len(BEAT_FEATURES)), dtype=np.float32)
        return np.array(times), F


def window_beat_features(p):
    """Gộp xác suất từng nhịp (n × 3: N, S, V) trong 1 cửa sổ thành 7 đặc trưng."""
    p = np.asarray(p, dtype=np.float64).reshape(-1, 3)
    if len(p) == 0:
        return np.zeros(len(WINDOW_BEAT_FEATURES), dtype=np.float32)
    return np.array([np.sum(p[:, 1] > 0.5), np.sum(p[:, 2] > 0.5), p[:, 1].max(), p[:, 2].max(), p[:, 1].mean(),
                     p[:, 2].mean(), len(p)], dtype=np.float32)


# ---------------------------------------------------------------- AI trên vòng ngực (TinyML): bản Python tham chiếu
# Firmware vòng ngực (ai_nhip.h) làm ĐÚNG các bước dưới đây bằng C++; bản Python dùng để huấn luyện và kiểm chứng.
TINYML_FEATURES = HRV_FEATURES + MORPH_FEATURES[:3]     # 11 đặc trưng chỉ cần thời điểm các đỉnh R


class _Biquad:
    """Giống struct Biquad trong firmware: hệ số RBJ, Q = 0,7071 (Butterworth), dạng chuẩn tắc II chuyển vị."""

    def __init__(self, thong_thap, fc, fs, q=0.70710678):
        w = 2 * np.pi * fc / fs
        c, al = np.cos(w), np.sin(w) / (2 * q)
        a0 = 1 + al
        b = [(1 - c) / 2, 1 - c, (1 - c) / 2] if thong_thap else [(1 + c) / 2, -(1 + c), (1 + c) / 2]
        self.b0, self.b1, self.b2 = b[0] / a0, b[1] / a0, b[2] / a0
        self.a1, self.a2 = -2 * c / a0, (1 - al) / a0
        self.z1 = self.z2 = 0.0

    def loc(self, x):
        y = self.b0 * x + self.z1
        self.z1 = self.b1 * x - self.a1 * y + self.z2
        self.z2 = self.b2 * x - self.a2 * y
        return y


class StreamingRDetector:
    """Pan–Tompkins chạy TỪNG MẪU như trên vi điều khiển: lọc 5–15 Hz → đạo hàm 5 điểm → bình phương → tích phân
    150 ms → cực đại địa phương → ngưỡng thích nghi (SPK/NPK), thời gian trơ 200 ms, tìm lại nhịp sót khi RR dài
    hơn 1,66 lần trung bình. Giữ thời điểm 40 nhịp gần nhất (đơn vị mẫu)."""

    def __init__(self, fs=500, ghi_het=False):
        self.fs = fs
        self.tat_ca = [] if ghi_het else None          # ghi mọi nhịp (để kiểm chứng), firmware không cần
        self.hp, self.lp = _Biquad(False, 5.0, fs), _Biquad(True, 15.0, fs)
        self.yb = [0.0] * 5
        self.W = max(1, int(round(0.15 * fs)))
        self.buf, self.k, self.tong = [0.0] * self.W, 0, 0.0
        self.p1 = self.p2 = 0.0
        self.n = 0
        self.max_hoc = self.tong_hoc = 0.0
        self.spk = self.npk = 0.0
        self.tro = int(0.2 * fs)
        self.cuoi, self.cuoi_v = -1, 0.0
        self.nhieu_t, self.nhieu_v = -1, 0.0
        self.rr, self.beats = [], []

    def them(self, x):
        y = self.lp.loc(self.hp.loc(float(x)))
        yb = self.yb
        yb.pop(0)
        yb.append(y)                                   # yb[4] = y[n] ... yb[0] = y[n-4]
        d = (2 * yb[4] + yb[3] - yb[1] - 2 * yb[0]) * (self.fs / 8.0)
        e = d * d
        self.tong += e - self.buf[self.k]
        self.buf[self.k] = e
        self.k += 1
        if self.k == self.W:                           # tính lại tổng cho khỏi trôi số
            self.k = 0
            self.tong = float(sum(self.buf))
        m = self.tong / self.W
        n = self.n
        self.n += 1
        if n < 2 * self.fs:                            # 2 giây đầu: học mức tín hiệu và nhiễu
            self.max_hoc = max(self.max_hoc, m)
            self.tong_hoc += m
            if n == 2 * self.fs - 1:
                self.spk, self.npk = 0.33 * self.max_hoc, 0.5 * self.tong_hoc / (2 * self.fs)
        else:
            ref = self.cuoi if self.cuoi >= 0 else 2 * self.fs
            if n - ref > int(2.5 * self.fs) and self.spk > self.npk:
                self.spk = self.npk + max(0.9, 1.0 - 1.5 / self.fs) * (self.spk - self.npk)
            if self.p1 > self.p2 and self.p1 >= m and self.p1 > 0:
                self._dinh(n - 1, self.p1)
        self.p2, self.p1 = self.p1, m

    def _nhan(self, t, v):
        if self.cuoi >= 0:
            self.rr = (self.rr + [t - self.cuoi])[-8:]
        self.cuoi, self.cuoi_v = t, v
        self.beats = (self.beats + [t])[-40:]
        if self.tat_ca is not None:
            self.tat_ca.append(t)
        self.nhieu_t, self.nhieu_v = -1, 0.0

    def _dinh(self, t, v):
        thr = self.npk + 0.25 * (self.spk - self.npk)
        if v > thr:
            if self.cuoi >= 0 and t - self.cuoi < self.tro:       # cùng 1 QRS: dời tới đỉnh cao hơn
                if v > self.cuoi_v:
                    if self.rr:
                        self.rr[-1] += t - self.cuoi
                    self.beats[-1] = t
                    if self.tat_ca:
                        self.tat_ca[-1] = t
                    self.cuoi, self.cuoi_v = t, v
                return
            rr_tb = sum(self.rr) / len(self.rr) if self.rr else 0.0
            if (self.cuoi >= 0 and rr_tb > 0 and t - self.cuoi > 1.66 * rr_tb and self.nhieu_t >= 0
                    and self.nhieu_v > 0.5 * thr and t - self.nhieu_t >= self.tro):
                nv = self.nhieu_v
                self._nhan(self.nhieu_t, nv)                     # nhịp bị sót
                self.spk = 0.25 * nv + 0.75 * self.spk
            v_cap = min(v, 3.0 * self.spk) if self.spk > 0 else v
            self._nhan(t, v)
            self.spk = 0.125 * v_cap + 0.875 * self.spk
        else:
            self.npk = 0.125 * v + 0.875 * self.npk
            if v > self.nhieu_v and (self.cuoi < 0 or t - self.cuoi >= self.tro):
                self.nhieu_t, self.nhieu_v = t, v

    def dac_trung(self, cua_so=10.0):
        """11 đặc trưng TinyML của các nhịp trong `cua_so` giây gần nhất."""
        bay = self.n / self.fs
        return tinyml_features(np.array([b / self.fs for b in self.beats if b / self.fs >= bay - cua_so]))


def tinyml_features(t_beats):
    """11 đặc trưng chỉ từ thời điểm các đỉnh R (giây) trong cửa sổ: 8 HRV + 3 tỉ số RR (= 11 cột đầu của HRV_FEATURES_V3)."""
    t = np.asarray(t_beats, dtype=np.float64)
    rr = rr_intervals(t, 1.0)
    if len(rr) >= 3:
        med = float(np.median(rr))
        m3 = [rr.min() / med, rr.max() / med, float(np.sum((rr[:-1] < 0.85 * med) & (rr[1:] > 1.1 * med)) / len(rr))]
    else:
        m3 = [1.0, 1.0, 0.0]
    return np.concatenate([hrv_features(t, 1.0), np.array(m3)]).astype(np.float32)


def apply_temperature(prob, temperature=1.0):
    """Hiệu chỉnh xác suất (temperature scaling) để 0.8 thật sự nghĩa là đúng khoảng 80 %."""
    T = float(temperature or 1.0)
    prob = np.asarray(prob, dtype=np.float64)
    if abs(T - 1.0) < 1e-6:
        return prob
    z = np.log(np.clip(prob, 1e-8, 1.0)) / T
    z = np.exp(z - z.max(axis=-1, keepdims=True))
    return z / z.sum(axis=-1, keepdims=True)


def _bo_khoa(obj, khoa):
    if isinstance(obj, dict):
        return {k: _bo_khoa(v, khoa) for k, v in obj.items() if k not in khoa}
    if isinstance(obj, list):
        return [_bo_khoa(v, khoa) for v in obj]
    return obj


def load_keras_model(path):
    """Nạp mô hình .keras. Colab thường dùng Keras mới hơn máy tính (vd. Keras 3.13 cần Python ≥ 3.11, máy có
    Python 3.10 chỉ cài được tới Keras 3.12). Nếu Keras trên máy không hiểu vài tham số mới trong cấu hình
    (vd. quantization_config), bỏ đúng các tham số đó, dựng lại mô hình từ cấu hình rồi nạp trọng số."""
    import os
    import re
    import shutil
    import tempfile
    import zipfile

    import keras

    try:
        return keras.models.load_model(path)
    except Exception as e:  # noqa: BLE001 - mọi lỗi phiên bản đều thử cách dự phòng bên dưới
        loi_dau, thong_bao = e, str(e)
    with zipfile.ZipFile(path) as z:
        cfg = json.loads(z.read("config.json"))
        tam = tempfile.mkdtemp()
        z.extract("model.weights.h5", tam)
    cfg.pop("compile_config", None)          # chỉ cần dự đoán, không cần trình tối ưu
    bo = set()
    try:
        for _ in range(8):
            moi = set(re.findall(r"'(\w+)'\s*:", " ".join(
                re.findall(r"Unrecognized keyword arguments passed to \w+: (\{[^}]*\})", thong_bao))))
            if bo and not moi - bo:
                break
            bo |= moi
            try:
                model = keras.saving.deserialize_keras_object(_bo_khoa(cfg, bo))
                model.load_weights(os.path.join(tam, "model.weights.h5"))
                print(f"Đã nạp mô hình bằng cách tương thích (Keras {keras.__version__} trên máy cũ hơn bản lúc "
                      f"huấn luyện; bỏ tham số {sorted(bo) or 'không'})")
                return model
            except Exception as e:  # noqa: BLE001
                thong_bao = str(e)
    finally:
        shutil.rmtree(tam, ignore_errors=True)
    raise loi_dau


def load_joblib(path):
    """joblib.load, kèm đường vòng khi file được lưu bằng numpy 2 (Colab) mà máy này còn numpy 1:
    bộ sinh số ngẫu nhiên bên trong mô hình cây (chỉ dùng lúc huấn luyện) được dựng lại theo kiểu numpy 1."""
    import joblib

    try:
        return joblib.load(path)
    except (ValueError, TypeError) as e:
        if "BitGenerator" not in str(e) and "state must be" not in str(e):
            raise
    import numpy.random._pickle as npk
    from joblib.numpy_pickle import NumpyUnpickler
    from joblib.numpy_pickle_utils import _read_fileobject

    tao_gen = getattr(npk, "__generator_ctor")
    lop = {}

    def ten(x):
        return x.__name__ if isinstance(x, type) else x

    def bit_gen(bg="MT19937"):
        goc = getattr(np.random, ten(bg))
        if goc not in lop:
            class TuongThich(goc):
                def __setstate__(self, st):          # numpy 2 lưu (state, seed_seq), numpy 1 chỉ nhận state
                    goc.__setstate__(self, st[0] if isinstance(st, tuple) else st)
            TuongThich.__name__ = TuongThich.__qualname__ = goc.__name__   # numpy kiểm tra tên lớp khi gán state
            lop[goc] = TuongThich
        return lop[goc]()

    def gen(bg="MT19937", *_):
        return np.random.Generator(bg) if isinstance(bg, np.random.BitGenerator) else tao_gen(ten(bg))

    class Doc(NumpyUnpickler):
        def find_class(self, module, name):
            if module == "numpy.random._pickle" and name == "__bit_generator_ctor":
                return bit_gen
            if module == "numpy.random._pickle" and name == "__generator_ctor":
                return gen
            return super().find_class(module, name)

    with open(path, "rb") as f:
        with _read_fileobject(f, path, None) as fo:
            return Doc(path, fo, mmap_mode=None).load()


class RiskModel:
    """Mô hình 1D-CNN-GRU-Attention Fusion. Đưa vào đoạn tín hiệu thô gần nhất (nên dài hơn cửa sổ vài giây).
    Thiết bị nào không có dữ liệu thì truyền mảng hằng (PPG = 0, radar = 128): sau tiền xử lý thành 0, đúng như
    lúc huấn luyện mô hình đã được tập 'mất kênh' (modality dropout)."""

    def __init__(self, model_path, config_path):
        import keras

        self._keras = keras
        with open(config_path, encoding="utf-8") as f:
            self.cfg = json.load(f)
        self.model = load_keras_model(model_path)
        self.input_names = [str(getattr(t, "name", "")).split(":")[0] for t in self.model.inputs]
        self.last_info = {}
        self.att_model = None              # trả thêm trọng số chú ý của nhánh ECG (mô hình có attention)
        try:
            lop = self.model.get_layer("ecg_att_trong_so")
            self.att_model = keras.Model(self.model.inputs, [self.model.outputs[0], lop.output])
        except Exception:  # noqa: BLE001 - mô hình không có attention
            self.att_model = None
        import os

        thu_muc = os.path.dirname(os.path.abspath(model_path))
        self.extra_models = [load_keras_model(os.path.join(thu_muc, f)) for f in self.cfg.get("extra_models", [])
                             if os.path.exists(os.path.join(thu_muc, f))]   # thêm mô hình (khác hạt giống): lấy trung bình
        self.feat_model = None             # mô hình cây trên đặc trưng nhịp, cộng xác suất với mô hình học sâu
        duong = os.path.join(thu_muc, "dac_trung_hgb.joblib")
        if os.path.exists(duong):
            self.feat_model = load_joblib(duong)
        self.beat_pipe = None              # mô hình 2 tầng: phân loại từng nhịp → kết luận cả cửa sổ
        self._hrv_cache = None
        duong = os.path.join(thu_muc, "phan_loai_nhip.joblib")
        if os.path.exists(duong):
            self.beat_pipe = load_joblib(duong)
            self.tracker = BeatTracker(self.cfg["ecg_fs"])
            self.beat_hist = []            # (thời điểm giây, xác suất N/S/V) của các nhịp gần đây

    def reset_tracker(self):
        """Đặt lại bộ theo dõi từng nhịp khi mất tín hiệu ECG lâu hoặc đồng hồ thiết bị nhảy."""
        if self.beat_pipe is not None:
            self.tracker = BeatTracker(self.cfg["ecg_fs"])
            self.beat_hist.clear()

    def _hrv_for(self, names):
        key = tuple(names)
        if self._hrv_cache and self._hrv_cache[0] == key:
            return self._hrv_cache[1]
        v = hrv_features(self._r, self.cfg["ecg_fs"], self._e, names)
        self._hrv_cache = (key, v)
        return v

    def make_inputs(self, ecg, ecg_fs, ppg, ppg_fs, mmw, mmw_fs):
        c = self.cfg
        efs, pfs, mfs = c["ecg_fs"], c["ppg_fs"], c["mmw_fs"]
        pv = int(c.get("preprocess_version", 2))       # lọc nhiễu đúng như lúc huấn luyện mô hình này
        e = fit_len(prep_ecg(resample(ecg, ecg_fs, efs), efs, version=pv)[-c["ecg_len"]:], c["ecg_len"])
        p = fit_len(prep_ppg(resample(ppg, ppg_fs, pfs), pfs)[-c["ppg_len"]:], c["ppg_len"])
        m = prep_mmw(resample(np.asarray(mmw, dtype=np.float64), mmw_fs, mfs), mfs if c.get("mmw_filter") else None)
        m = fit_len(m[-c["mmw_len"]:], c["mmw_len"])
        sqi_p = ppg_quality(p, pfs)
        p_net = np.zeros_like(p) if sqi_p < 0.25 else p
        xe, xp, xm = window_inputs(e, p_net, m, c["ecg_len"], c["ppg_len"], c["mmw_len"])
        x = {"ecg": xe[None], "ppg": xp[None], "mmwave": xm[None]}
        r = detect_r_peaks(e, efs, version=pv)
        ex = extra_inputs(e, efs, c.get("use_mel", False), c.get("use_hrv", False), c.get("mel"),
                          c.get("hrv_names"), r=r)
        for k, v in ex.items():
            x[k] = v[None]
        if c.get("use_hrv", False) and "hrv" in ex:
            self._hrv_cache = (tuple(c.get("hrv_names") or HRV_FEATURES), ex["hrv"])
        else:
            self._hrv_cache = None
        info = {"sqi_ecg": ecg_quality(e, efs, r, version=pv), "sqi_ppg": sqi_p, "so_dinh_r": int(len(r))}
        if sqi_p < 0.25 and float(np.std(p)) >= 1e-9:
            info["ppg_masked"] = True
        self._e, self._r = e, r
        return x, info

    def _p_hai_tang(self, t_end):
        """Xác suất theo mô hình 2 tầng cho cửa sổ hiện tại. t_end: giờ (giây) của mẫu cuối; None = không theo dõi liên tục."""
        bp, efs, e, r = self.beat_pipe, self.cfg["ecg_fs"], self._e, self._r
        if t_end is None:
            tr = BeatTracker(efs)
            _, F = tr.process(e, r, 0.0)
            P = bp["beat_model"].predict_proba(F) if len(F) else np.zeros((0, 3))
        else:
            if self.beat_hist and (t_end < self.beat_hist[-1][0] - 1.0 or t_end > self.beat_hist[-1][0] + 120.0):
                self.beat_hist.clear()
            times, F = self.tracker.process(e, r, t_end - len(e) / efs)
            if len(F):
                P = bp["beat_model"].predict_proba(F)
                self.beat_hist.extend(zip(times.tolist(), P))
            giu_tu = t_end - max(self.cfg["window_sec"], 60.0)
            self.beat_hist = [it for it in self.beat_hist if it[0] >= giu_tu][-300:]
            P = np.array([p for t, p in self.beat_hist if t_end - self.cfg["window_sec"] <= t <= t_end]).reshape(-1, 3)
        self._last_n_beats = len(P)
        x2 = np.concatenate([self._hrv_for(bp["hrv_names"]), window_beat_features(P)])[None]
        p2 = np.zeros(len(self.cfg["class_names"]))
        p2[bp["window_model"].classes_] = bp["window_model"].predict_proba(x2)[0]
        return p2, int(np.sum(P[:, 1] > 0.5)) if len(P) else 0, int(np.sum(P[:, 2] > 0.5)) if len(P) else 0

    def predict(self, ecg, ecg_fs, ppg, ppg_fs, mmw, mmw_fs, t_end=None, min_sqi_ecg=None):
        """t_end (tuỳ chọn): giờ của mẫu cuối (giây), để theo dõi từng nhịp liên tục giữa các lần gọi.
        min_sqi_ecg: nếu đặt, bỏ qua suy luận nặng và không cập nhật BeatTracker khi chất lượng ECG thấp hơn ngưỡng."""
        t0 = time.perf_counter()
        x, info = self.make_inputs(ecg, ecg_fs, ppg, ppg_fs, mmw, mmw_fs)
        if min_sqi_ecg is not None and info["sqi_ecg"] < min_sqi_ecg:
            info["bo_qua"] = True
            info["ms"] = round(1000 * (time.perf_counter() - t0), 1)
            self.last_info = info
            return self.cfg["class_names"][0], None
        vao = [x[n] for n in self.input_names] if all(n in x for n in self.input_names) else x
        np_ = self._keras.ops.convert_to_numpy
        if self.att_model is not None:
            out, att = self.att_model(vao, training=False)
            att = np.asarray(np_(att), dtype=np.float64)[0, :, 0]
            att = (att - att.min()) / (att.max() - att.min() + 1e-12)              # 0 = ít chú ý nhất, 1 = nhiều nhất
            info["chu_y"] = np.round(att, 3).tolist()                                # mỗi điểm ≈ 0,13 s
            info["ecg_mau"] = np.round(zscore(resample(self._e, self.cfg["ecg_fs"], 100), clip=6), 2).tolist()
        else:
            out = self.model(vao, training=False)
        prob = np.asarray(np_(out), dtype=np.float64)[0]
        if self.extra_models:              # trung bình các mô hình học sâu (như notebook), rồi mới hiệu chỉnh xác suất
            ps = [prob] + [np.asarray(np_(m(vao, training=False)), dtype=np.float64)[0] for m in self.extra_models]
            prob = np.mean(ps, axis=0)
            info["bat_dong"] = round(float(np.max(np.std(ps, axis=0))), 3)   # các mô hình lệch nhau bao nhiêu
        prob = apply_temperature(prob, self.cfg.get("temperature", 1.0))
        if self.beat_pipe is not None:
            p2, so_s, so_v = self._p_hai_tang(t_end)
            n_b = getattr(self, "_last_n_beats", 4)
            w = float(self.beat_pipe.get("weight", 0.5)) * min(1.0, n_b / 4.0)
            info["xac_suat_hoc_sau"], info["xac_suat_tung_nhip"] = np.round(prob, 3).tolist(), np.round(p2, 3).tolist()
            info["ngoai_tam_thu_nhi"], info["ngoai_tam_thu_that"] = so_s, so_v
            prob = (1 - w) * prob + w * p2
        fm = self.feat_model
        if fm is not None and len(getattr(fm["model"], "classes_", [])) == len(prob):
            f = self._hrv_for(fm["names"])
            p2 = np.asarray(fm["model"].predict_proba(f[None])[0], dtype=np.float64)
            w = float(fm.get("weight", 0.5))
            info["xac_suat_hoc_sau"], info["xac_suat_dac_trung"] = np.round(prob, 3).tolist(), np.round(p2, 3).tolist()
            prob = (1 - w) * prob + w * p2
        prob = np.clip(prob, 0.0, 1.0)
        s = float(prob.sum())
        if s > 0:
            prob = prob / s
        h = -float(np.sum(prob * np.log(np.clip(prob, 1e-9, 1.0))))
        info["do_tin_cay"] = round(1.0 - h / np.log(len(prob)), 3)      # 1 = chắc chắn, 0 = phân vân đều các lớp
        info["ms"] = round(1000 * (time.perf_counter() - t0), 1)
        self.last_info = info
        return self.cfg["class_names"][int(np.argmax(prob))], prob


# ---------------------------------------------------------------- nhịp thở từ cảm biến lực FSR (Isolation Forest)
BREATH_FEATURES = ["mean", "sd", "peak", "rate_bpm", "max_gap_s", "amp_cv",
                   "flat_frac", "band_ratio", "spec_entropy", "acf_peak"]
APNEA_GAP_S = 20.0          # không có nhịp thở nào trong 20 s → ngưng thở, dù điểm Isolation Forest chưa vượt ngưỡng


def breath_calibration(x, fs, calib_sec=60):
    """Mốc riêng cho từng người và từng lần đeo, lấy từ 60 giây đầu (trung vị theo khối 10 s chống nhiễu ho/cử động)."""
    raw = np.asarray(x, dtype=np.float64)[: int(calib_sec * fs)]
    seg = lowpass(raw - np.median(raw), fs, 1.5)
    center = float(np.median(raw))
    ac = bandpass(seg, fs, 0.08, 1.0, order=2)
    blk = max(1, int(10 * fs))
    nb = len(ac) // blk
    scale = float(np.median(ac[: nb * blk].reshape(nb, blk).std(axis=1))) if nb >= 3 else float(np.std(ac))
    return center, scale + 1e-6


def breath_feature_dict(win, fs, center, scale):
    """Mọi đặc trưng của 1 cửa sổ:
    mean, sd, peak (Hình 2 của báo cáo); rate_bpm nhịp thở; max_gap_s khoảng không thở dài nhất; amp_cv độ đều biên độ;
    flat_frac tỉ lệ đoạn 2 s gần như phẳng; band_ratio năng lượng trong dải thở 0.1–0.7 Hz; spec_entropy độ 'loạn'
    của phổ; acf_peak độ tuần hoàn (tự tương quan lớn nhất ở trễ 1.5–10 s)."""
    z = (np.asarray(win, dtype=np.float64) - center) / scale
    mean, sd = float(z.mean()), float(z.std())
    zz = z - mean
    f = {"mean": mean, "sd": sd, "peak": float(np.max(np.abs(zz)))}
    fr, P = sps.periodogram(zz * np.hanning(len(zz)), fs=fs, nfft=max(4096, len(zz)))
    band = (fr >= 0.1) & (fr <= 1.0)
    f["rate_bpm"] = float(60.0 * fr[band][np.argmax(P[band])]) if P[band].max() > 0 else 0.0
    pk, prop = sps.find_peaks(zz, distance=max(1, int(1.2 * fs)), prominence=0.5)
    edges = np.concatenate([[0.0], pk / fs, [len(zz) / fs]])
    amps = prop["prominences"]
    f["max_gap_s"] = float(np.max(np.diff(edges)))
    f["amp_cv"] = float(np.std(amps) / (np.mean(amps) + 1e-6)) if len(pk) > 1 else 1.0
    seg = int(2 * fs)
    k = len(zz) // seg
    f["flat_frac"] = float(np.mean(zz[: k * seg].reshape(k, seg).std(axis=1) < 0.15)) if k else 0.0
    f["band_ratio"] = float(P[(fr >= 0.1) & (fr <= 0.7)].sum() / (P[fr > 0.02].sum() + 1e-12))
    pb = P[band] / (P[band].sum() + 1e-12)
    f["spec_entropy"] = float(-(pb * np.log(pb + 1e-12)).sum() / np.log(max(int(band.sum()), 2)))
    ac = np.correlate(zz, zz, mode="full")[len(zz) - 1:]
    ac = ac / (ac[0] + 1e-12)
    lo, hi = int(1.5 * fs), min(int(10 * fs), len(ac) - 1)
    f["acf_peak"] = float(ac[lo:hi].max()) if hi > lo else 0.0
    return f


def breath_features(win, fs, center, scale, extra=True):
    """extra: danh sách tên đặc trưng; True = 6 đặc trưng của phiên bản 1; False = chỉ mean, SD, peak như Hình 2."""
    names = list(extra) if isinstance(extra, (list, tuple)) else (BREATH_FEATURES[:6] if extra else BREATH_FEATURES[:3])
    d = breath_feature_dict(win, fs, center, scale)
    return np.array([d[n] for n in names])


def breath_baseline(model, x_cal, fs, center, scale, names, window_sec=30, stride_sec=10):
    """Điểm bất thường nền của riêng một người: điểm lớn nhất của các cửa sổ 30 s (trượt 10 s) nằm trong 60 giây hiệu
    chuẩn lúc mới đeo (thở bình thường). Người có kiểu thở vốn kém đều thì điểm nền cao, nên ngưỡng của người đó cũng
    được nâng lên, tránh báo nhầm liên tục. Dùng chung cho notebook và máy chủ."""
    x = lowpass(np.asarray(x_cal, dtype=np.float64), fs, 1.5)
    w, st = int(window_sec * fs), max(1, int(stride_sec * fs))
    F = [breath_features(x[i:i + w], fs, center, scale, extra=names) for i in range(0, len(x) - w + 1, st)]
    return float(np.max(-model.score_samples(np.array(F)))) if F else None


class BreathDetector:
    def __init__(self, path):
        d = load_joblib(path)
        self.model, self.threshold = d["model"], d["threshold"]
        self.fs, self.calib_sec = d["fs"], d["calib_sec"]
        self.names = list(d.get("features") or (BREATH_FEATURES[:6] if d.get("extra", True) else BREATH_FEATURES[:3]))
        self.apnea_gap = d.get("apnea_gap_s", APNEA_GAP_S)
        self.window_sec = d.get("window_sec", 30)
        self.margin = d.get("margin")          # có: ngưỡng riêng = max(ngưỡng chung, điểm nền + margin); None: như bản cũ
        self.center = self.scale = self.nen = None
        self.last_feats, self.last_reason = {}, ""

    def calibrate(self, fsr_first_60s):
        self.center, self.scale = breath_calibration(fsr_first_60s, self.fs, self.calib_sec)
        self.nen = None
        if self.margin is not None:
            self.nen = breath_baseline(self.model, fsr_first_60s, self.fs, self.center, self.scale, self.names,
                                       self.window_sec)

    def nguong(self):
        """Ngưỡng đang dùng cho người đang đeo."""
        if self.margin is None or self.nen is None:
            return self.threshold
        return max(self.threshold, self.nen + self.margin)

    def _dac_trung_luat(self, raw):
        """Đặc trưng của 1 cửa sổ (sau lọc thông thấp 1,5 Hz) và luật "≥ 20 s không có nhịp thở nào"."""
        x = lowpass(raw, self.fs, 1.5)
        d = breath_feature_dict(x, self.fs, self.center, self.scale)
        luat = False
        if self.apnea_gap:
            # Xác nhận thêm bằng thành phần AC (0.08–0.8 Hz) để loại trôi đường nền khi đổi tư thế
            ac_z = bandpass(x - np.median(x), self.fs, 0.08, 0.8, order=2) / self.scale
            ac_pk, _ = sps.find_peaks(ac_z, distance=max(1, int(1.2 * self.fs)), prominence=0.5)
            ac_edges = np.concatenate([[0.0], ac_pk / self.fs, [len(ac_z) / self.fs]])
            ac_gap = float(np.max(np.diff(ac_edges)))
            seg = int(2 * self.fs)
            k = len(ac_z) // seg
            ac_flat = float(np.mean(ac_z[: k * seg].reshape(k, seg).std(axis=1) < 0.15)) if k else 0.0
            luat = bool((ac_gap >= self.apnea_gap and ac_flat >= 0.3) or (
                d["max_gap_s"] >= self.apnea_gap and d["flat_frac"] >= 0.3 and ac_gap >= 0.75 * self.apnea_gap))
        return d, luat

    def check(self, fsr_window):
        """Trả (bất thường?, anomaly score). last_reason: "iforest" | "ngung_tho" (luật ≥ 20 s không thở) | ""."""
        raw = np.asarray(fsr_window, dtype=np.float64)
        if self.center is None or not self.scale or len(raw) < max(4, int(4 * self.fs)) or not np.all(np.isfinite(raw)):
            self.last_feats, self.last_reason = {}, ""
            return False, 0.0
        d, luat = self._dac_trung_luat(raw)
        score = float(-self.model.score_samples(np.array([[d[n] for n in self.names]]))[0])
        flag = score > self.nguong()
        reason = "iforest" if flag else ""
        if luat:
            flag, reason = True, "ngung_tho"
        self.last_feats, self.last_reason = d, reason
        return flag, score

    def check_many(self, windows):
        """Như check() cho nhiều cửa sổ đủ dài, không có NaN (chấm Isolation Forest 1 lần nên nhanh hơn nhiều).
        Trả (cờ bất thường, điểm, cờ luật ≥ 20 s không thở, danh sách đặc trưng) cho từng cửa sổ."""
        D, L = zip(*[self._dac_trung_luat(np.asarray(w, dtype=np.float64)) for w in windows])
        s = -self.model.score_samples(np.array([[d[n] for n in self.names] for d in D]))
        lu = np.array(L, dtype=bool)
        return (s > self.nguong()) | lu, s, lu, list(D)


# ---------------------------------------------------------------- ngưng thở khi ngủ từ SpO2 của vòng tay (18/11/2026)
# Ngưng thở / giảm thở làm SpO2 tụt rồi hồi lại, chậm hơn cơn ngưng thở khoảng 10–40 giây. Thử trên 8 đêm Apnea-ECG có
# SpO2 (nhãn từng phút của chuyên gia), kiểm định chéo bỏ-ra-1-người: SpO2 phát hiện tốt hơn hẳn sóng thở ở ngực, vì
# khi ngưng thở do tắc nghẽn lồng ngực vẫn cố thở. Ghép thêm đặc trưng sóng thở ngực vào mô hình lại làm kết quả kém đi
# (mỗi người một kiểu thở), nên mô hình chỉ dùng SpO2; sóng thở ngực vẫn lo luật "≥ 20 s không thở" và Isolation Forest.
SPO2_DAC_TRUNG = ["tut", "bien_do", "lech_tb"]


def spo2_lam_sach(t, v, nhay_max=4.0):
    """Gán NaN cho giá trị SpO2 hỏng: ngoài 50–100 %, hoặc đổi nhanh hơn 4 %/giây so với giá trị hợp lệ trước đó
    (cử động, cảm biến tuột). t: giây, tăng dần; v: %."""
    t = np.asarray(t, dtype=np.float64)
    v = np.asarray(v, dtype=np.float64).copy()
    v[~np.isfinite(v) | (v < 50) | (v > 100)] = np.nan
    t_tr = v_tr = None
    for i in range(len(v)):
        if np.isnan(v[i]):
            continue
        if v_tr is not None and abs(v[i] - v_tr) > nhay_max * max(t[i] - t_tr, 1.0):
            v[i] = np.nan
            continue
        t_tr, v_tr = t[i], v[i]
    return v


def spo2_dac_trung(t, v, t_cuoi, cua_so=80.0, nen=210.0, chu_ky=2.0):
    """Đặc trưng tụt SpO2 của đoạn [t_cuoi − 80 s, t_cuoi) so với mức nền của chính người đó (phân vị 90 trong 210 giây
    gần nhất, gần với SpO2 lúc thở bình thường): tut = nền − thấp nhất, bien_do = cao nhất − thấp nhất,
    lech_tb = nền − trung bình. Dùng mức tụt chứ không dùng giá trị tuyệt đối, vì SpO2 đo ở cổ tay chưa hiệu chuẩn có
    thể lệch vài %. Tự làm sạch (spo2_lam_sach) từ 30 giây trước đoạn nền. chu_ky: số giây giữa 2 giá trị (vòng tay gửi
    2 giây/lần). Trả None khi đoạn 80 giây có dưới một nửa số giá trị hợp lệ, hoặc đoạn nền dưới 30 %."""
    t = np.asarray(t, dtype=np.float64)
    v = np.asarray(v, dtype=np.float64)
    m = (t >= t_cuoi - nen - 30.0) & (t < t_cuoi)
    t, v = t[m], spo2_lam_sach(t[m], v[m])
    v_l = v[t >= t_cuoi - cua_so]
    v_n = v[t >= t_cuoi - nen]
    if np.isfinite(v_l).sum() < 0.5 * cua_so / chu_ky or np.isfinite(v_n).sum() < 0.3 * nen / chu_ky:
        return None
    muc_nen = float(np.nanpercentile(v_n, 90))
    thap = float(np.nanmin(v_l))
    return {"tut": muc_nen - thap, "bien_do": float(np.nanmax(v_l)) - thap, "lech_tb": muc_nen - float(np.nanmean(v_l)),
            "nen": muc_nen, "thap_nhat": thap}


class NgungThoSpO2:
    """Hồi quy logistic trên 3 đặc trưng tụt SpO2 (file ngung_tho_spo2.json do notebook tạo). xet(t, v, t_cuoi) cho xác
    suất đoạn 80 giây gần nhất có tụt SpO2 kiểu ngưng thở / giảm thở (ứng với nhịp thở của khoảng 30–90 giây trước)."""

    def __init__(self, path_or_cfg):
        cfg = path_or_cfg
        if isinstance(path_or_cfg, str):
            with open(path_or_cfg, encoding="utf-8") as f:
                cfg = json.load(f)
        self.cfg = cfg
        self.ten = list(cfg["features"])
        self.mu, self.sd = np.asarray(cfg["mu"], dtype=float), np.asarray(cfg["sd"], dtype=float)
        self.w, self.b = np.asarray(cfg["coef"], dtype=float), float(cfg["intercept"])
        self.nguong = float(cfg.get("threshold", 0.5))
        self.cua_so, self.nen = float(cfg.get("window_s", 80)), float(cfg.get("baseline_s", 210))
        self.chu_ky = float(cfg.get("period_s", 2))

    def xac_suat_dac_trung(self, d):
        z = (np.array([d[k] for k in self.ten], dtype=float) - self.mu) / self.sd
        return float(1.0 / (1.0 + np.exp(-(float(z @ self.w) + self.b))))

    def xet(self, t, v, t_cuoi):
        """Trả (xác suất, đặc trưng) hoặc (None, None) khi thiếu SpO2. t: giây; v: % (NaN/-1 = không có)."""
        d = spo2_dac_trung(t, v, t_cuoi, self.cua_so, self.nen, self.chu_ky)
        if d is None:
            return None, None
        return self.xac_suat_dac_trung(d), d
