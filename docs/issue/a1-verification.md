# A1 verification — 2026-09-23

Base cd93685595d4a52302d5b14adc9cbf38ac82553c, source overlay làm việc có patch 0034 chưa commit.
**Static/integration PASS. Compiler, SDK build/link, board test NOT RUN.**

| Kiểm | Kết quả |
|---|---|
| C/header parity với baseline | PASS, 228 file, chỉ đổi include namespace và 2 include relative trong sdk/sdk.h |
| App/library sdk-scan --check | PASS |
| Shell syntax | PASS installer BDK/MTK, header installer, SDK prune |
| BDK/MTK/UCI prune | PASS source references trong các fragment còn tồn tại, scanner khớp |
| Header canonical + legacy | PASS 3 SDK, app include targets tồn tại, chuyển BDK → MTK dọn header SDK cũ |
| SDK header không tồn tại | Reject trước khi tạo output |
| MTK install | PASS dry-run không ghi, legacy archive, cài/reinstall, chỉ MTK, path có khoảng trắng và dấu nháy |
| BDK install | PASS profile không khớp bị reject trước mutation, legacy archive, fixture đúng preimage cài/reinstall, generated Makefile cũ bị loại |
| Patch 0034 | PASS git apply --check/apply và GNU patch -p1 --dry-run/apply --fuzz=0 |
| Patch replay | 352 file khớp overlay byte-for-byte và executable bit, group-write mode không phải Git-tracked bit |
| Bundle A1 | PASS overlay khớp, standalone installers MTK/BDK chạy trên fixture |

Lỗi gặp và đã xử lý: preflight BDK trước đây chạy sau khi chép source. Profile vendor hiện có
không khớp patch 0001 làm lộ đường fail này; A1 chuyển preflight lên trước mọi mutation.
Đường success của test dùng ba input file SDK và phục hồi profile preimage **chỉ trong fixture**.
Không suy thành full integration PASS trên vendor tree hiện tại.

Patch dùng delete/add cho các move. `git apply` có thể cảnh báo whitespace vốn có trong source
vendor được di chuyển; không mass-format để che cảnh báo. Replay bytes vẫn đúng baseline ngoài
các thay đổi có chủ ý. Dùng clean build để loại generated object/header cũ.

Verifier: `verify-a1.py --scratch <empty-task-owned-directory>`. Host không có gcc/cc/make/
autoreconf, không cài thêm toolchain. Không sửa vendor src, không chạy installer trên vendor src.

Các artifact chính thức: `0034-icwmp-dm-source-layout.patch`, `icwmp_a1_port.tar.gz`,
`a1-implementation.md`, `implementation-status.json`, `progress.py`, installer/feed cập nhật.


Cleanup: đã xóa đúng `/tmp/icwmp_a1_JkaQPYG4` gồm fixtures, temp Git index, replay trees và
bundle extraction. Repo overlay index thật không đổi, chưa commit. check-docs PASS (9 warning
chatlog cũ ở issue khác), scoped diff check PASS, bundle export deterministic, hash trong SHA256SUMS-a1.
