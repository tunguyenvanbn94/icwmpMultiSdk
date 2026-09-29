# Thay đổi so với upstream uci `5781664d`

| File | Thay đổi | Lý do |
|---|---|---|
| `uci/uci.h` | `UCI_CONFDIR` `/etc/config` → `/data/icwmp/config`, `UCI_SAVEDIR` `/tmp/.uci` → `/tmp/icwmp/.uci` | Rootfs BDK là squashfs read-only, `/data` là partition ghi được và bền qua reboot (`system-config.sh` cũng dùng `/data`). `/tmp` là tmpfs |
| (bỏ) `lua/`, `tests/`, `sh/`, `.github/` | không copy | Không cần cho libuci + CLI |

Mọi consumer (`icwmpd`, `libtr098`, CLI `uci`) tự thấy thư mục mới vì dùng default của
`uci_alloc_context()`. Không cần `uci_set_confdir()` ở từng chỗ.
