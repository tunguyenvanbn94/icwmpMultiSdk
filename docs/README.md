# Tài liệu icwmp multi-SDK

Repo này chứa source dùng chung cho Broadcom BDK và MTK OpenWrt. `userspace/` là
source áp vào SDK, `feeds/` là hai package MTK, `apply` kiểm bundle và cài vào
SDK đích. Lệnh sử dụng và build ở [README repo](../README.md).

## Kiến trúc và flow

- [Thiết kế đa platform và hai data model](mtk/icwmp_multiplatform_tr098_design.md)
- [Flow app, domain, SDK và TR-098](mtk/icwmp_multiplatform_tr098_flow.md)
- [Thiết kế tích hợp BDK](bdk/icwmp_bdk_port_design.md), [runtime BDK](bdk/icwmp_bdk_runtime_flow.md)
- [TR-098 trên TR-181](bdk/tr098_facade_on_tr181_design.md), [ma trận hai model](bdk/icwmp_datamodel_tr098_tr181_matrix.md)
- [Hướng dẫn phát triển parameter](bdk/icwmp_parameter_development_guide.md)

## Trạng thái và việc tiếp theo

- [Kế hoạch phase port C](issue/tr098_c_port_phases.md)
- [Trạng thái có cấu trúc](issue/implementation-status.json) và [công cụ xem tiến độ](issue/progress.py)
- [Phân tích và bằng chứng](issue/analysis.md), [ma trận coverage](issue/tr098_coverage_matrix.tsv)
- [Lệnh build](issue/build-commands.md), [lệnh debug](issue/debug-commands.md)

Các file trong `issue/`, `mtk/` và `bdk/` là bản sao từ workspace
ngày 2026-09-29. Chúng giữ mốc commit và đường dẫn gốc để truy vết, vì vậy vài
liên kết tương đối và trạng thái trong file cũ không phản ánh repo này. Mốc mới
nhất trong source là patch 0077. Trạng thái phase có cấu trúc cập nhật lần cuối
ngày 2026-10-04: 458/783 parameter TR-098 bằng C, P1–P5 đã port, P6–P8 còn kế
hoạch. Các patch 0062–0077 sửa init, debug, apply, lỗi runtime và tải của shell
data model, không mở rộng coverage. Bản SDK đã build được xác minh tới P4c–P4f;
chưa có kết quả build/board-test đầy đủ cho HEAD repo này. 0067–0077 đã kiểm
bằng agent thật chạy trên host: [tests/host](../tests/host/README.md).

Xem lịch sử từng patch trong repo:

```sh
git log --reverse --oneline main
git show --stat <commit>
```
