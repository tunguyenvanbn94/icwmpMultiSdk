# Tài liệu icwmp multi-SDK

Repo này chứa source dùng chung cho Broadcom BDK và MTK OpenWrt. `userspace/` là
source áp vào SDK, `feeds/` là hai package MTK, `apply` kiểm bundle và cài vào
SDK đích. Lệnh sử dụng và build ở [README repo](../README.md).

## Bắt đầu từ đây (đào tạo, chuyển giao)

1. [Kiến trúc, cách chia code và flow xử lý](handover/icwmp_architecture_guide.md): vì sao làm icwmp riêng,
   ba chiều SDK/model/product, chung và riêng trong source, đặt code ở đâu, flow khởi động/phiên/GPV-SPV,
   tích hợp SDK, quy ước tên.
2. [Đã làm gì, đang ở đâu, còn gì](handover/icwmp_progress_matrix.md): bảng tiến độ, gate board PH0, known
   issue, việc để đóng PH0, lộ trình PH1–PH8.

Từ 2026-10-06, mọi tài liệu về kế hoạch, tiến độ và trạng thái được cập nhật **trong `docs/` của repo này**:
bằng chứng ở [issue/analysis.md](issue/analysis.md) (đã đồng bộ tới §52), trạng thái ở
[issue/implementation-status.json](issue/implementation-status.json), gate board ở
[plan/ph0_gate_runbook.md](plan/ph0_gate_runbook.md).

## Kế hoạch hiện hành

- [Đồng bộ main/dev và thiết kế v2](plan/sync-main-dev.md): kết luận, khoảng hở so với code, known
  issue K1–K9, lộ trình PH0–PH8, quy ước branch, commit, cổng PR và nguồn sự thật
- Kiến trúc đích v2 (rà soát 2026-09-30 trên `main`/0066, đọc kèm §2 của file trên):
  [multi-SDK](plan/v2/icwmp_multisdk_design_v2.md), [MTK/OpenWrt](plan/v2/icwmp_mtk_openwrt_design_v2.md),
  [các phase](plan/v2/icwmp_next_phases_plan_v2.md), [audit code/thiết kế](plan/v2/icwmp_code_design_audit_status_2026-09-30.md)

## Kiến trúc và flow (lịch sử, 2026-09-29)

- [Thiết kế đa platform và hai data model](mtk/icwmp_multiplatform_tr098_design.md)
- [Flow app, domain, SDK và TR-098](mtk/icwmp_multiplatform_tr098_flow.md)
- [Thiết kế tích hợp BDK](bdk/icwmp_bdk_port_design.md), [runtime BDK](bdk/icwmp_bdk_runtime_flow.md)
- [TR-098 trên TR-181](bdk/tr098_facade_on_tr181_design.md), [ma trận hai model](bdk/icwmp_datamodel_tr098_tr181_matrix.md)
- [Hướng dẫn phát triển parameter](bdk/icwmp_parameter_development_guide.md)

## Trạng thái và việc tiếp theo

- [Trạng thái có cấu trúc](issue/implementation-status.json) (nguồn duy nhất: phase, lộ trình,
  known issue, validation) và [công cụ xem tiến độ](issue/progress.py)
- [Kế hoạch phase port C](issue/tr098_c_port_phases.md) (lịch sử; trạng thái phase xem JSON)
- [Phân tích và bằng chứng](issue/analysis.md), [ma trận coverage](issue/tr098_coverage_matrix.tsv)
- [Lệnh build](issue/build-commands.md), [lệnh debug](issue/debug-commands.md)

Các file trong `mtk/` và `bdk/`, cùng phần đầu của `issue/`, là bản sao từ workspace ngày 2026-09-29.
Chúng giữ mốc commit và đường dẫn gốc để truy vết, nên vài liên kết tương đối và trạng thái trong file cũ
không phản ánh repo này; link tới `../../src/2025q3/...` trong `analysis.md` là cây vendor của workspace.
Trạng thái hiện hành luôn xem ở [handover/icwmp_progress_matrix.md](handover/icwmp_progress_matrix.md) và JSON:
mốc code mới nhất `0083`, 458/783 parameter TR-098 bằng C (P1–P5), MTK SDK build đạt tới 0083,
board MTK đạt G1–G3, G5, K10, K13, K14, K17 (image 0083).

Các script kiểm tĩnh trong `issue/` (`check-c-sanity.py`, `verify-dm-paths.py`,
`check-automake-conds.py`, `check-pkg-deps.py`) giờ kiểm cây `userspace/` của chính repo này.
Đặt `ICWMP_USERSPACE` để kiểm một cây khác. Danh sách lệnh của cổng PR nằm ở
[sync-main-dev.md §6.3](plan/sync-main-dev.md#63-cổng-của-mỗi-pr-vào-dev).

Xem lịch sử từng patch trong repo:

```sh
git log --reverse --oneline dev
git show --stat <commit>
```
