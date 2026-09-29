# Tổng quan kiến trúc đa nền tảng iCWMP & libtr098

Tài liệu này mô tả kiến trúc tổng quan, luồng hoạt động (flow), và cơ chế trừu tượng hoá (abstraction) giúp `icwmp` cùng data model `libtr098` có thể chạy trên nhiều nền tảng SDK khác nhau (hiện tại là Broadcom BDK và MTK OpenWrt).

---

## 1. Kiến trúc tổng thể các thành phần (Overall Architecture)

Kiến trúc chia hệ thống thành 3 tầng chính: **Core Engine** (xử lý giao thức), **Data Model Engine** (cây cấu trúc TR-098/TR-181) và **Platform Backends** (lớp giao tiếp với hệ điều hành/SDK bên dưới).

```mermaid
graph TD
    subgraph ACS
        GenieACS["GenieACS / ITMS"]
    end

    subgraph CPE
        subgraph Core
            SessionMgr["Session Management"]
            RPCParser["RPC Parser and Builder"]
            HTTPClient["HTTP/HTTPS Transport"]
        end

        subgraph DM
            TreeManager["Data Model Tree TR-098/TR-181"]
            Dispatcher["Request Dispatcher"]
            subgraph HAL
                API["HAL APIs Get/Set/Add/Del"]
            end
        end

        subgraph Backends
            BDK["Broadcom BDK<br/>(CMS MDM/libbcm_generic_hal)"]
            OpenWrt["MTK OpenWrt<br/>(UCI/Ubus/easycwmp scripts)"]
            Future["Future Vendor SDK<br/>(Vendor Specific API)"]
        end

        ACS <-->|"SOAP over HTTP/HTTPS"| Core
        Core <-->|"Function Calls"| DM
        DM --> HAL
        HAL --> BDK
        HAL --> OpenWrt
        HAL -.-> Future
    end

    classDef core fill:#e1f5fe,stroke:#01579b,stroke-width:2px
    classDef dm fill:#e8f5e9,stroke:#1b5e20,stroke-width:2px
    classDef sdk fill:#fff3e0,stroke:#e65100,stroke-width:2px

    class Core core
    class DM dm
    class Backends sdk
```

**Vai trò các tầng:**
1. **iCWMP Core Engine:** Quản lý vòng đời tiến trình, tạo session định kỳ (Periodic Inform) hoặc khi có sự kiện (Value Change, Connection Request). Quản lý chuỗi xác thực Digest/Basic với ACS và phân giải bản tin SOAP XML.
2. **libtr098 (Data Model):** Định nghĩa cấu trúc cây thư mục (ví dụ: `InternetGatewayDevice.DeviceInfo.`). Dịch các đường dẫn path từ SOAP thành các nút (node) cụ thể để xử lý.
3. **Platform Backends & HAL:** Trừu tượng hóa việc đọc/ghi cấu hình thực tế. Core sẽ không bao giờ gọi trực tiếp `uci` hay `cms_mdm`. Mọi thao tác đều qua HAL, và tuỳ vào flag lúc biên dịch (`ICWMP_BDK` hoặc `ICWMP_OPENWRT`) mã nguồn tương ứng của backend sẽ được gọi.

---

## 2. Luồng xử lý tổng quan (Session Flow)

Luồng thời gian từ lúc CPE khởi động, thiết lập kết nối đến lúc thực thi các cấu hình lệnh từ máy chủ ACS.

```mermaid
sequenceDiagram
    participant Boot as System Boot
    participant Core as iCWMP Engine
    participant DM as libtr098
    participant SDK as Platform SDK (OpenWrt/BDK)
    participant ACS as ACS Server

    Boot->>Core: Khởi chạy tiến trình icwmpd
    Core->>SDK: Khởi tạo kết nối hệ thống (MDM shmId hoặc Ubus)
    Core->>SDK: Đọc cấu hình ACS (URL, User/Pass)
    Core->>DM: Init Data Model (Chọn TR-098 hoặc TR-181)
    DM->>SDK: Nạp dữ liệu tĩnh/động để dựng cây param
    Core->>Core: Đợi Event (Boot, Periodic, ValueChange, Connection Request)

    note over Core,ACS: Bắt đầu phiên CWMP (Session)
    Core->>ACS: HTTP POST (Inform Request)
    ACS-->>Core: 401 Unauthorized / Challenge (nếu ACS yêu cầu Auth)
    Core->>ACS: HTTP POST (Inform kèm Digest Auth)
    ACS-->>Core: HTTP 200 OK (InformResponse)

    note over Core,ACS: Thực thi các RPC từ ACS
    ACS->>Core: GetParameterValues / SetParameterValues (SOAP)
    Core->>DM: Parse cấu trúc XML & Gọi API libtr098
    DM->>SDK: Dispatcher -> HAL -> Đọc/Ghi thực tế ở HĐH
    SDK-->>DM: Trả kết quả thành công (hoặc mã lỗi CWMP Fault)
    DM-->>Core: Build lại list kết quả thành chuỗi XML
    Core->>ACS: XML Response (Get/Set Response)

    note over Core,ACS: Kết thúc phiên
    Core->>ACS: Empty POST (Báo hiệu hết RPC)
    ACS-->>Core: HTTP 204 No Content
    Core->>SDK: Kích hoạt lưu Flash/Reboot (nếu có apply cấu hình)
```


## 3. Luồng chi tiết giao tiếp Data Model đa nền tảng (Get/Set)

Đây là cách iCWMP xử lý rẽ nhánh linh hoạt giữa cấu hình Broadcom CMS và MTK OpenWrt (UCI/easycwmp).

```mermaid
graph TD
    subgraph iCWMP
        REQ["Yêu cầu từ ACS Get/Set Value"]
    end

    subgraph libtr098
        Router{"Cấu hình Model?"}
        TR098["InternetGatewayDevice."]
        TR181["Device."]

        Router -->|"TR-098"| TR098
        Router -->|"TR-181"| TR181

        TR098 --> ParamMap["Tra cứu Node trên cây"]
        TR181 --> ParamMap

        ParamMap --> IsStatic{"Phạm vi dữ liệu?"}
        IsStatic -->|"Thuộc quản trị nội bộ"| MemVar["Biến quản lý riêng<br/>(vd ACS URL)"]
        IsStatic -->|"Phụ thuộc Firmware"| HAL_Interface["Gọi C interface HAL"]
    end

    subgraph SDK_Layer
        HAL_Interface --> PlatformCheck{"Backend Target?"}

        PlatformCheck -->|"ICWMP_BDK"| BDK_Impl["Broadcom Impl"]
        BDK_Impl --> CMS["CMS MDM / Batch bcmGeneric_ SPV API"]

        PlatformCheck -->|"ICWMP_OPENWRT"| MTK_Impl["MTK OpenWrt Impl"]
        MTK_Impl --> Scripts["Gọi shell scripts<br/>(cwmpclient/easycwmp)"]
        MTK_Impl --> UCI["API libuci / libubus"]

        PlatformCheck -->|"VENDOR_X"| Vendor_Impl["Vendor X Impl"]
        Vendor_Impl --> VendorAPI["Vendor Specific SDK API"]
    end

    REQ --> Router
    MemVar --> Response["Trả XML Envelope về ACS"]
    CMS --> Response
    Scripts --> Response
    UCI --> Response
    VendorAPI --> Response

    style HAL_Interface fill:#f9f,stroke:#333,stroke-width:2px
```

### Chi tiết cách hoạt động của Data Model:
- **Router (Chọn Data Model):** Có thể chạy 1 lúc cả tham số `InternetGatewayDevice.` (TR-098) và `Device.` (TR-181) nhờ vào proxy alias hoặc dựng cây tách rời, điều khiển bằng tham số cấu hình.
- **Phân loại tham số (Static vs Platform-dependent):** Những thông số liên quan đến chính iCWMP (như `ManagementServer.URL`) sẽ do iCWMP tự quản lý và ghi config riêng (thường lưu `/data/icwmp/` hoặc `/etc/config/cwmp`). Những thông số thiết bị (như WiFi SSID, IP WAN) sẽ đi qua HAL.
- **MTK OpenWrt Layer:** Sử dụng lại toàn bộ gia tài snapshot hơn 1000 param từ `easycwmp` bằng cách map từ C xuống layer Shell scripts, giúp giảm thiểu rủi ro code lỗi logic và đồng bộ tương thích với phiên bản hiện tại.

---

## 4. Khả năng mở rộng cho SDK tương lai (Future Extensibility)

Nhờ thiết kế Abstract HAL, việc thêm một nền tảng thứ 3 (ví dụ: Realtek SDK) hoàn toàn không ảnh hưởng tới core engine hay cây logic TR-098. Các bước thực hiện chỉ bao gồm:
1. Tạo một thư mục backend mới: `libtr098/platform/vendor_x/`
2. Cài đặt các hàm giao tiếp bắt buộc chuẩn của HAL: `hal_get_value()`, `hal_set_value()`, `hal_add_object()`, `hal_del_object()`, `hal_apply_config()`.
3. Định nghĩa mapping table giữa đường dẫn CWMP chuẩn và đường dẫn riêng biệt của SDK đó.
4. Thêm `CFLAGS=-DICWMP_VENDOR_X` vào Makefile khi biên dịch trên nền tảng đó. Hệ thống sẽ tự động bypass Broadcom/MTK.

---

## Ghi chú đối chiếu với bản đã hiện thực (Claude Code, 2026-09-23)

Tài liệu này là **tổng quan khái niệm**, viết trước khi code. Bản đã hiện thực
(overlay commit `99f4988`, patch `0032`) theo đúng hướng ở mục 3 — dùng lại thư viện hàm
shell của `easycwmp` thay vì viết lại data model — nhưng khác ở phần đặt tên và một số chi tiết:

| Trong tài liệu này | Trong code thực tế |
|---|---|
| cờ build `ICWMP_OPENWRT` | `./configure --with-platform=mtk` → `DM_PLATFORM_MTK` (libtr098) / `ICWMP_MTK` (icwmpd) |
| "Abstract HAL" `hal_get_value()`… | seam có sẵn `platform/dmplatform.h` (`dm_platform_param_method`, `commit`, `revert`, …) + `inc/icwmp_platform.h` cho icwmpd |
| "gọi shell scripts" | **một** shell con thường trú (`platform/script/dmscript.c` + `scripts/mtk/icwmp_dm.sh`), không fork mỗi RPC |
| "hơn 1000 param" | đếm được **824 param + 204 object** (`easycwmp_tr098_inventory.txt`) |
| TR-098 và TR-181 chạy song song | MTK **chỉ TR-098** ở lượt này (TR-181 chỉ có ở platform `bdk`) |

Chi tiết và bằng chứng: [../analysis.md](../analysis.md),
[../../../docs/icwmp_multiplatform_tr098_design.md](../../../docs/icwmp_multiplatform_tr098_design.md).
