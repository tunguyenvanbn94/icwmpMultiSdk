# Uplink — design system cho công cụ icwmp

Repo không có UI. Uplink lấy ngôn ngữ hình ảnh từ những gì repo đã dùng:
`docs/issue/implementation-status.json`, `docs/issue/progress.py`, output của
`apply.py`, `tr098_coverage_matrix.tsv`, mức `CWMP_LOG`, CWMP event và
`FAULT_9000–9019`. Dùng cho dashboard tiến độ, output apply, xem coverage và log.
Token nằm trong [`tokens.json`](tokens.json).

## Nguyên tắc

1. **Bằng chứng có nấc.** `PLANNED → PARTIAL → STATIC_VERIFIED → BUILD_PASS → BOARD_PASS`.
   Độ đậm của fill tăng theo mức kiểm chứng; chỉ `BOARD_PASS` là nền đặc.
   `STATIC_VERIFIED` chưa có nghĩa BUILD/BOARD PASS nên chỉ có viền.
2. **Path là tên.** Path TR-098 dùng mono, phần đầu màu muted, lá in đậm. Khi
   thiếu chỗ thì cắt phần đầu (`…DeviceInfo.MemoryStatus.Free`), không cắt lá.
3. **Một source, hai SDK.** BDK (teal) và MTK (copper) nằm ngoài trục màu
   trạng thái, luôn kèm nhãn chữ.
4. **Văn xuôi tiếng Việt, identifier tiếng Anh.** IBM Plex Sans Condensed / Sans / Mono
   hỗ trợ đủ dấu tiếng Việt.

## Quy tắc ngắn

- Màu `signal` (#1F4FBF) chỉ dùng cho action, link, focus và NOTICE, không dùng cho trạng thái.
- Pill (radius 999) chỉ dành cho CWMP event. Các chỗ khác dùng radius 2 / 4 / 8.
- Control cao tối thiểu 44px. Focus ring 2px `signal`, offset 2px.
- Console (#121417) là bề mặt tối duy nhất. Cột level rộng cố định 76px; dòng ERROR có dải nền màu.
- Giá trị lúc runtime trong ví dụ để dạng `[ACS URL]`, không bịa số liệu.
