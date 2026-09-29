# Ma trận TR-098 (icwmp/libtr098) ↔ TR-181 (BDK MDM) — phát triển theo phần

Scope: những gì SDK BDK hiện có trong data model TR-181 (cái `tr69c` đang phục vụ cho ACS), đối chiếu
với cây `InternetGatewayDevice.` mà backend BDK của libtr098 (issue
[20260916_tr069_app_use_icwmp](../issues/20260916_tr069_app_use_icwmp/README.md)) đã/chưa có, và
kế hoạch làm từng phần LAN / DHCP / WAN / WiFi / hệ thống. Thiết kế tổng thể ở
[icwmp_bdk_port_design.md](icwmp_bdk_port_design.md).

## Kết luận chính

- **[Verified dump + XML]** MDM TR-181 của SDK có đủ nguồn cho toàn bộ IGD "Baseline + EthernetLAN +
  EthernetWAN + WiFiLAN + Time + IPPing" mà Broadcom từng hỗ trợ trong `cms-dm-tr98.xml`, trừ
  `DHCPConditionalServingPool` (TR-181 `Pool.{i}.Chaddr/ChaddrMask` = `NotSupported`) và
  `DNS.Relay`/`UPnP` (`NotSupported`).
- **[Verified source]** Backend lúc bắt đầu (`tr098/bdk/*.c`, HEAD `088f1ee`) có ~80 leaf: DeviceInfo
  16/18, LANHostConfigManagement 12/12, IPInterface 4/4, LANEthernetInterfaceConfig 5/5,
  WLANConfiguration 22/41, Hosts 8/8, WANCommonInterfaceConfig 10/10, WANEthernetInterfaceConfig 5/7,
  WANIPConnection 14/19. **Sau 19/09 (`0017`..`0022`, HEAD `ca594e1`)**: đủ mọi object chuẩn trong
  `cms-dm-tr98.xml` mà MDM có nguồn — root, `Stats` (LANEth/WANEth/WANIP/WANPPP/WLAN), `DHCPStaticAddress`,
  `DHCPOption`, `PortMapping`, `WANPPPConnection`, `WEPKey`, `WPS`, `Time`, `IPPingDiagnostics`,
  `TraceRouteDiagnostics`, `Layer3Forwarding`, `DeviceInfo.VendorConfigFile/MemoryStatus/ProcessStatus`.
  Chưa: `Layer2Bridging` (view), `X_BROADCOM_COM_*` vendor, `QueueManagement`, `DeviceConfig`.
- **[Verified board 19/09]** Chưa leaf nào được ACS gọi tới (session dừng ở auth GenieACS). Mọi mapping
  dưới đây là **chưa test trên board**; vì vậy phần 0 là công cụ `ubus call tr069 dm` để test GPV/GPN/SPV
  ngay trên board không cần ACS.
- Thứ tự làm: **0 công cụ test → 1 LAN + DHCP → 2 WAN → 3 WiFi → 4 hệ thống (root, DeviceInfo, Time,
  Diagnostics) → 5 Routing / Bridging / vendor → 6 Wi-Fi 7 / MLO / client / mesh (proxy TR-181)**.
  Mỗi phần = một commit trong `sdk-overlay/userspace` + một patch `00NN` + tarball mới.
- **[Verified XML + source]** Wi-Fi 7/MLO/mesh/client chi tiết không có trong TR-098; SDK giữ chúng
  trong `Device.WiFi.DataElements.*` (EasyMesh R5, có APMLD/STAMLD Wi-Fi 7) do `wldataeld` điền —
  chỉ chạy khi nvram `wldataeld_enable=1` (MO77300EB mặc định 0). Cấu hình MLO (`wl_mlo_config`,
  `mld1/2_ifnames`) chỉ ở kernel NVRAM, **không có trong MDM** → object `X_MARUSYS_COM_MloCfg.` do
  icwmp tự phục vụ ở cả hai model (`0025`, phần 7 dưới; không đổi data model SDK, chưa build-test).
- **[Verified source]** Data model TR-181 cho icwmp không cần viết lại: MDM của BDK đã là TR-181,
  tr69c chỉ passthrough qua generic HAL. `0024`+`0025` làm y vậy: `cwmp.cpe.datamodel=tr181` → root
  `Device.`, mọi path qua `dm_platform_param_method()`, attributes/enabled-notify/isPassword theo
  `tr69c/SOAPParser/dmCms.c` (phần 8). Chọn mode bằng config, không rebuild.

## Nguồn bằng chứng

| Nguồn | Là gì | Dùng để |
|---|---|---|
| `logs/20260828_referenceBoard/brcm_ap7_fwrefer_dumpmdm_mod_ethwan.log` | `dumpmdm` trên ref board 96765REF1 (mod ethwan, 28/08), UTF-16, **4 cây `<Device>`** = 4 component MDM (sysmgmt, wifi, devinfo, diag; không có `tr69` vì TR-069 tắt) — 19 596 param, 1 084 object | Instance thật, giá trị thật, object nào thật sự được tạo lúc chạy |
| `src/bcm963xx/data-model/cms-dm-tr181-*.xml`, `cms-dm-bcm-*.xml` (snapshot `9f2a56abd`) | Schema TR-181 của SDK, 774 object / 6 162 param, mỗi param có `supportLevel` (`ReadWrite`/`ReadOnly`/`NotSupported`) và `profile` (`DMP_*` bật theo build) | Cái gì ghi được (RCL handler tồn tại), cái gì bị SDK bỏ |
| `src/bcm963xx/data-model/cms-dm-tr98.xml` | Cây IGD mà **Broadcom từng hỗ trợ** trên CMS Legacy98 (133 object, 1 913 param, có `supportLevel`) | Danh sách đích cho TR-098: đúng tên, đúng kiểu, đúng R/W mà ACS Broadcom quen |
| `issues/20260916_tr069_app_use_icwmp/sdk-overlay/userspace/public/libs/libtr098/libtr098/tr098/bdk/*.c` | Backend đang có | Cột "hiện có" |

Cách đọc lại nhanh (host): `iconv -f UTF-16LE -t UTF-8 <dump> | grep -n '<Device>'` → 4 cây;
object có `instance="N"` là instance thật, `nextInstance=` chỉ là marker bảng.

Giá trị boolean trong dump là `TRUE/FALSE`, qua GPV là `1/0` — backend chuẩn hoá (`BDK_MAP_BOOL`).

## Kế hoạch theo phần

| Phần | Phạm vi IGD | Nguồn TR-181 (instance ref board) | Việc chính | Cỡ |
|---|---|---|---|---|
| **0. Công cụ test** — **xong** `e167400` (`0017`) | — | — | `ubus call tr069 dm '{"cmd":"get|names|set|add|del", "path":…}'` chạy DM của libtr098 trên board, giữ `mutex_session_send` | S |
| **1. LAN + DHCP** — **xong** `4dfc928` (`0018`), chưa board-test | `LANDevice.1.` + `LANHostConfigManagement.` (+`DHCPStaticAddress.{i}`, `DHCPOption.{i}`, `IPInterface.1`) + `LANEthernetInterfaceConfig.{i}` (+`Stats`) + `Hosts` | `DHCPv4.Server.Pool.1` (+`StaticAddress`, `Option`), `IP.Interface.1` (br0), `Ethernet.Interface.{i}` Upstream=FALSE (+`Stats`), `Hosts.Host` (2) | thêm 2 bảng có Add/Delete, `Stats`, sửa `DNSServers` (đang ghi nhầm vào `DNS.Client.Server` = DNS WAN) | S |
| **2. WAN** — **xong** `946b45e` (`0019`), chưa board-test | `WANDevice.1.` + `WANCommonInterfaceConfig` + `WANEthernetInterfaceConfig` (+`Stats`) + `WANConnectionDevice.1.` (`WANEthernetLinkConfig`, `WANIPConnection.{i}` +`PortMapping.{i}` +`Stats`, `WANPPPConnection.{i}`) | `Ethernet.Interface` Upstream=TRUE (+`Stats`), `Ethernet.VLANTermination.1`, `IP.Interface.2` (eth1.1 trên MO77300EB) +`Stats`, `DHCPv4.Client.1`, `NAT.InterfaceSetting.1`, `NAT.PortMapping.{i}` (0), `Routing.Router.1.IPv4Forwarding.1`, `DNS.Client.Server.{i}`, `PPP.Interface.{i}` (0) | `PortMapping` Add/Delete, `Stats`, 5 leaf còn thiếu, `WANPPPConnection` đọc/ghi trên instance PPP có sẵn (không tạo stack) | M |
| **3. WiFi** — **xong** `896290e` (`0020`), chưa board-test | `WLANConfiguration.{i}` (+`WEPKey.{i}`, `PreSharedKey.1`, `AssociatedDevice.{i}`, `Stats`, `WPS`) | `WiFi.SSID.{i}` (48) +`Stats`, `Radio.{r}` (3), `AccessPoint.{i}` +`Security` +`WPS` +`AssociatedDevice.{i}` | 19 leaf std còn thiếu, `Stats` 11 leaf, `WEPKey` ↔ `Security.X_BROADCOM_COM_WlKey1..4`, `WPS`, `AssociatedDevice.LastDataTransmitRate`, bảng chân trị BeaconType ↔ `ModeEnabled` | L |
| **4. Hệ thống** — **xong** `85133c9` (`0021`), chưa board-test | root (`DeviceSummary`, `LANDeviceNumberOfEntries`, `WANDeviceNumberOfEntries`), `DeviceInfo.VendorConfigFile.{i}`, `MemoryStatus`, `ProcessStatus`, `Time.`, `IPPingDiagnostics.`, `TraceRouteDiagnostics.` | `DeviceInfo.VendorConfigFile.1`, `MemoryStatus`, `ProcessStatus`, `Time` (1:1), `IP.Diagnostics.IPPing`/`TraceRoute` (+`RouteHops`), sự kiện `CMS_MSG_PING_STATE_CHANGED`/`TRACERT_STATE_CHANGED` do `tr69_md` forward | `Time` 1:1, diagnostics = map + nối sự kiện CMS → `8 DIAGNOSTICS COMPLETE` trong icwmpd | M |
| **5. Routing / Bridging / vendor** — Layer3Forwarding **xong** `ca594e1` (`0022`); Bridging/vendor chưa | `Layer3Forwarding.Forwarding.{i}`, `Layer2Bridging.*` (read-only), `X_BROADCOM_COM_*` chọn lọc (Syslog, Login, IGMP) | `Routing.Router.1.IPv4Forwarding.{i}`, `Bridging.Bridge.1` +`Port.{i}` (5), `X_BROADCOM_COM_SyslogCfg`, `LoginCfg`, `IGMPCfg` | chỉ khi ACS cần | M |
| **6. Wi-Fi 7 / MLO / client / mesh** — **xong** `67ad62d` (`0023`) proxy `X_MARUSYS_COM_Device.` ↔ `Device.` | toàn bộ TR-181, đặc biệt `WiFi.DataElements.*` (EasyMesh R5: APMLD/STAMLD/AffiliatedSTA/WiFi7Capabilities, Backhaul, STA per BSS), `WbdCfg`, `AccessPoint.AssociatedDevice.Stats` | như cột trái | hook `dm_platform_param_method` → generic HAL, không bảng map; cần `wldataeld_enable=1` | S |
| **7. Cấu hình MLO trong icwmp** — **xong** `b563e2d` (`0025`), chưa build-test | `InternetGatewayDevice.X_MARUSYS_COM_MloCfg.` (TR-098) = `Device.WiFi.X_MARUSYS_COM_MloCfg.` (TR-181) | nvram `wl_mlo_*` của WebUI Marusys (CLI `nvram`), kernel nvram `wl_mlo_config`, link BSS qua `Device.WiFi.SSID.{i}`/`AccessPoint.{i}.Security` (generic HAL) | `tr098/bdk/mlo_bdk.c`: 11 leaf, topology từ MDM (không hard-code), lặp lại apply của `cgi_marusys_mlo.c`; reboot để driver đọc `wl_mlo_config`. Thay cho patch SDK `0002` (bỏ) | M |
| **8. Data model TR-181 chọn bằng config** — **xong** `7272d2c` (`0024`) + `b563e2d` (`0025`) + `7a44032` (`0026`), chưa build-test | `Device.` toàn bộ (như tr69c) + cây tĩnh `Device.WiFi.X_MARUSYS_COM_MloCfg.`, `Device.ManagementServer.` (8 leaf icwmp + `X_MARUSYS_COM_Icwmp.`) | generic HAL + UCI của icwmp | root `Device.`, Inform/GPA/SPA/enabled-notify theo tr69c trong `dmproxy_bdk.c`; `cwmp.cpe.datamodel=tr098|tr181` hoặc ACS set `…Icwmp.DataModel` | M |

Không nằm trong kế hoạch (không map được hoặc không có nguồn): `DHCPConditionalServingPool`
(`Pool.Chaddr/ChaddrMask` NotSupported), `DownloadDiagnostics`/`UploadDiagnostics`
(`Device2_Download/Upload` có trong XML nhưng **không có trong dump** ref board → profile không bật;
MO77300EB: `BUILD_TR143` chưa bật theo issue 20260914), `QueueManagement` (QoS 10 042 param, chỉ khi
ACS yêu cầu), `UPnP`, `LANUSBInterfaceConfig`, `WANDSL*`/`PON`/`MoCA`, `X_BROADCOM_COM_WlanAdapter.*`
(ACS Broadcom cũ dùng, thay bằng `X_MARUSYS_COM_*` khi cần).

## Cách kiểm trên board (phần 0)

```sh
ubus call tr069 dm '{"cmd":"names","path":"InternetGatewayDevice.LANDevice.1.","next_level":false}'
ubus call tr069 dm '{"cmd":"get","path":"InternetGatewayDevice.LANDevice.1.LANHostConfigManagement."}'
ubus call tr069 dm '{"cmd":"set","path":"InternetGatewayDevice.LANDevice.1.LANHostConfigManagement.DHCPLeaseTime","value":"43200"}'
ubus call tr069 dm '{"cmd":"add","path":"InternetGatewayDevice.LANDevice.1.LANHostConfigManagement.DHCPStaticAddress."}'
ubus call tr069 dm '{"cmd":"del","path":"InternetGatewayDevice.LANDevice.1.LANHostConfigManagement.DHCPStaticAddress.1."}'
```

Kết quả `set`/`add`/`del` đi qua đúng đường SPV/AddObject của session ACS (`dm_entry_apply` →
`bcm_generic_setParameterValues` batch → `saveConfigToFlash` khi `icwmpd` kết thúc session; với ubus
thì gọi `icwmp_bdk_save_config()` ngay). Đối chiếu phía MDM bằng `dumpmdm` hoặc WebUI.

## Phần 1 — LAN

`InternetGatewayDevice.LANDevice.1.` — một LANDevice = một bridge `br0` (`Device.Bridging.Bridge.1`,
`IP.Interface.1 Name=br0`, `Ethernet.Link.1`).

| TR-098 | TR-181 (MDM) | RW MDM | Hiện có | Việc |
|---|---|---|---|---|
| `LANDevice.1.LANEthernetInterfaceNumberOfEntries` | đếm `Ethernet.Interface.{i}.Upstream=FALSE` (ref: 1 = eth1) | R | có | — |
| `LANDevice.1.LANWLANConfigurationNumberOfEntries` | `WiFi.SSIDNumberOfEntries` (48) | R | có | — |
| `LANDevice.1.LANUSBInterfaceNumberOfEntries` | không có USB | R | có (`0018`) | — |
| `LANEthernetInterfaceConfig.{i}.` `Enable, Status, MACAddress, Name, MaxBitRate, DuplexMode, MACAddressControlEnabled` | `Ethernet.Interface.{i}.` (instance = TR-181) | Enable RW, còn lại R | có (Status dịch Up/NoLink/Disabled/Error) | — |
| `LANEthernetInterfaceConfig.{i}.Stats.` `BytesSent, BytesReceived, PacketsSent, PacketsReceived` | `Ethernet.Interface.{i}.Stats.` cùng tên | R | có (`0018`) | — |
| `LANHostConfigManagement.IPInterface.1.` `Enable, IPInterfaceIPAddress, IPInterfaceSubnetMask, IPInterfaceAddressingType` | `IP.Interface.1.Enable`, `IPv4Address.1.IPAddress/SubnetMask` (`AddressingType=Static`) | RW | có | — |
| `Hosts.HostNumberOfEntries`, `Hosts.Host.{i}.` `IPAddress, AddressSource, LeaseTimeRemaining, MACAddress, HostName, InterfaceType, Active, Layer2Interface, VendorClassID, ClientID, UserClassID` | `Hosts.Host.{i}.` (`PhysAddress`, `Layer1Interface` → InterfaceType Ethernet/802.11) | R | có | `VendorClassID/ClientID/UserClassID` là `NotSupported` trong MDM → trả rỗng |

## Phần 1 — DHCP (server LAN)

`LANHostConfigManagement.` ↔ `Device.DHCPv4.Server.Pool.1.` (`Interface=Device.IP.Interface.1`).

| TR-098 | TR-181 (MDM) | RW MDM | Hiện có | Việc |
|---|---|---|---|---|
| `DHCPServerConfigurable` | const `1` | — | có | — |
| `DHCPServerEnable` | `Pool.1.Enable` | RW | có | — |
| `DHCPRelay` | `DHCPv4.Relay` có trong XML (`Device2_DHCPv4Relay:1`), không có trong dump | — | const `0` | — |
| `MinAddress, MaxAddress, SubnetMask, DomainName, IPRouters, DHCPLeaseTime` | `Pool.1.MinAddress, MaxAddress, SubnetMask, DomainName, IPRouters, LeaseTime` | RW | có | — |
| `DNSServers` | `Pool.1.DNSServers` (ref: `0.0.0.0,0.0.0.0` = phát địa chỉ router) | RW | có (`0018`): đọc `Pool.1.DNSServers`, `0.0.0.0` → trả `IPRouters`; ghi `Pool.1.DNSServers` (bản trước `0001` ghi nhầm `DNS.Client.Server` = DNS WAN) | — |
| `ReservedAddresses` | `Pool.1.ReservedAddresses` = `NotSupported` | — | const `""` | — |
| `DHCPStaticAddressNumberOfEntries`, `DHCPStaticAddress.{i}.` `Enable, Chaddr, Yiaddr` | `Pool.1.StaticAddressNumberOfEntries`, `Pool.1.StaticAddress.{i}.` cùng tên (ref: 0 instance) | RW, Add/Delete | có (`0018`), Add/Delete qua `bdk_add_object`/`bdk_del_object` | test `add` → `set Chaddr/Yiaddr/Enable` → client nhận IP cố định |
| `DHCPOptionNumberOfEntries`, `DHCPOption.{i}.` `Enable, Tag, Value` | `Pool.1.OptionNumberOfEntries`, `Pool.1.Option.{i}.` cùng tên | RW, Add/Delete | có (`0018`) | test `add` → `set Tag=42 Value=<hex>` |
| `DHCPConditionalServingPool.{i}.` | `Pool.{i}.Chaddr/ChaddrMask/VendorClassID/ClientID/UserClassID` = **NotSupported** | — | — | không làm |
| `LANHostConfigManagement.X_BROADCOM_COM_*` (1 param) | — | — | — | bỏ |

DHCPv6 / RouterAdvertisement không có trong TR-098 chuẩn (Broadcom để ở
`X_BROADCOM_COM_IPv6LANHostConfigManagement`) — chỉ làm khi ACS cần.

## Phần 2 — WAN

`WANDevice.1.` = port Ethernet `Upstream=TRUE` (ref board sau mod ethwan: `Ethernet.Interface.1`
= eth0; **MO77300EB: eth1**, `Device.IP.Interface.2` = `eth1.1` qua `Ethernet.VLANTermination.1`,
`DHCPv4.Client.1`). Một `WANConnectionDevice.1`, mỗi `IP.Interface` không phải `br*`/`lo` là một
`WANIPConnection.{i}` (instance = TR-181).

| TR-098 | TR-181 (MDM) | RW MDM | Hiện có | Việc |
|---|---|---|---|---|
| `WANDevice.1.WANConnectionNumberOfEntries` | đếm IP.Interface WAN | R | có | — |
| `WANCommonInterfaceConfig.` `EnabledForInternet, WANAccessType(Ethernet), Layer1Up/DownstreamMaxBitRate, PhysicalLinkStatus, TotalBytes/Packets Sent/Received, MaximumActiveConnections, NumberOfActiveConnections` | `Ethernet.Interface.{u}.MaxBitRate/Status/Stats.*` | R | có | — |
| `WANEthernetInterfaceConfig.` `Enable, Status, MACAddress, MaxBitRate, DuplexMode` | `Ethernet.Interface.{u}.` | Enable RW | có | — |
| `WANEthernetInterfaceConfig.` `ShapingRate, ShapingBurstSize` | `Ethernet.Interface.{u}.X_BROADCOM_COM_ShapingRate/ShapingBurstSize` | RW | có (`0019`) | — |
| `WANEthernetInterfaceConfig.Stats.` 4 leaf | `Ethernet.Interface.{u}.Stats.` | R | có (`0019`) | — |
| `WANConnectionDevice.1.WANEthernetLinkConfig.EthernetLinkStatus` | `Ethernet.Interface.{u}.Status` → Up/Down | R | có (`0019`) | — |
| `WANIPConnection.{i}.` `Enable, ConnectionStatus, PossibleConnectionTypes, ConnectionType, Name, Uptime, LastConnectionError, RSIPAvailable, NATEnabled, AddressingType, ExternalIPAddress, SubnetMask, DefaultGateway, DNSServers, MACAddress` | `IP.Interface.{i}.` + `DHCPv4.Client` (AddressingType) + `NAT.InterfaceSetting` (NATEnabled) + `Routing.Router.1.IPv4Forwarding` (DefaultGateway) + `DNS.Client.Server` | RW | có (aux0..3) | — |
| `WANIPConnection.{i}.` `DNSEnabled, DNSOverrideAllowed, MaxMTUSize, MACAddressOverride, RouteProtocolRx, PortMappingNumberOfEntries` | `DNS.Client.Enable` (RO), const `0`, `IP.Interface.{i}.MaxMTUSize` (RW), const `0`, const `Off`, đếm `NAT.PortMapping` có `Interface=IP.Interface.{i}` hoặc `AllInterfaces` | RW/R | có (`0019`) | — |
| `WANIPConnection.{i}.PortMapping.{i}.` `PortMappingEnabled, PortMappingLeaseDuration, RemoteHost, ExternalPort, InternalPort, PortMappingProtocol, InternalClient, PortMappingDescription` | `NAT.PortMapping.{i}.` `Enable, LeaseDuration, RemoteHost, ExternalPort, InternalPort, Protocol, InternalClient, Description` (+ `Interface` = IP.Interface WAN khi Add) | RW, Add/Delete | có (`0019`): AddObject tạo `NAT.PortMapping` + set `Interface` ngay, DeleteObject; thêm `ExternalPortEndRange` | test add → set ExternalPort/InternalPort/Protocol/InternalClient/Enabled → `iptables -t nat -S` |
| `WANIPConnection.{i}.Stats.` `EthernetBytesSent/Received, EthernetPacketsSent/Received` | `IP.Interface.{i}.Stats.Bytes*/Packets*` | R | có (`0019`) | — |
| `WANPPPConnection.{i}.` (30 std) | `PPP.Interface.{i}.` (`Username, Password, Enable, Status, ConnectionStatus, LastConnectionError, IdleDisconnectTime, MaxMRUSize, CurrentMRUSize, LCPEcho, LCPEchoRetry, PPPoE.ServiceName/ACName`) + `IP.Interface` có `LowerLayers=PPP.Interface.{i}` (ExternalIPAddress, DNS, NAT) | RW | có (`0019`): 30 leaf chuẩn trên instance PPP **có sẵn** (`IP.Interface` có `LowerLayers=PPP.Interface.{p}`), `ConnectionStatus/LastConnectionError/ConnectionTrigger` dùng thẳng enum TR-181, + `PortMapping`/`Stats` | không tạo stack PPP qua AddObject (cần `IP.Interface` + `LowerLayers`) — chỉ khi ISP dùng PPPoE |
| `WANIPConnection.{i}.X_BROADCOM_COM_*` (53), `X_BROADCOM_COM_PortTriggering`, `FirewallException`, `MacFilter` | `Firewall`, `X_BROADCOM_COM_*` | — | — | chỉ khi ACS cần |

## Phần 3 — WiFi

`WLANConfiguration.{i}` ↔ `WiFi.SSID.{i}` (48 = 3 radio × 16, instance ổn định, `LowerLayers` →
`Radio.{r}`), `AccessPoint.{a}` với `SSIDReference` = SSID đó (ref: a = i), security ở
`AccessPoint.{a}.Security.` (`ModeEnabled` ∈ None, WEP-64, WEP-128, WPA-Personal, WPA2-Personal,
WPA-WPA2-Personal, WPA3-Personal, WPA3-Personal-Transition, WPA-Enterprise…).

| TR-098 | TR-181 (MDM) | RW MDM | Hiện có | Việc |
|---|---|---|---|---|
| `Enable, Status, BSSID, MACAddress, Name, SSID` | `SSID.{i}.` | Enable/SSID RW | có | — |
| `RadioEnabled, Channel, AutoChannelEnable, PossibleChannels, RegulatoryDomain, TransmitPower, Standard` | `Radio.{r}.` (`OperatingStandards` → Standard) | RW | có | `Standard` để read-only (ghi `OperatingStandards` ảnh hưởng MLO) |
| `SSIDAdvertisementEnabled, WMMEnable, TotalAssociations` | `AccessPoint.{a}.` | RW | có | — |
| `BeaconType, WPAEncryptionModes, IEEE11iEncryptionModes, WPAAuthenticationMode, IEEE11iAuthenticationMode, BasicAuthenticationMode, BasicEncryptionModes, KeyPassphrase, WEPKeyIndex` | `AccessPoint.{a}.Security.ModeEnabled/KeyPassphrase/X_BROADCOM_COM_WlKeyIndex/X_BROADCOM_COM_WlWpaEncryption` | RW | có (`0020`): Encryption RW ↔ `X_BROADCOM_COM_WlWpaEncryption` aes/tkip/tkip+aes, Auth suy từ `ModeEnabled` (Enterprise → EAP), Basic từ WEP, `WEPKeyIndex` RW | BeaconType `Basicand*` (WEP + WPA trộn) trả 9007 — driver không hỗ trợ |
| `PreSharedKey.1.` `PreSharedKey, KeyPassphrase, AssociatedDeviceMACAddress` | `Security.PreSharedKey/KeyPassphrase` | RW (write-only, đọc rỗng) | có | — |
| `WEPKey.{i}.WEPKey` (4) | `Security.X_BROADCOM_COM_WlKey1..4` | RW | có (`0020`, write-only) | — |
| `MaxBitRate, BasicDataTransmitRates, OperationalDataTransmitRates, PossibleDataTransmitRates, AutoRateFallBackEnabled` | `Radio.{r}.MaxBitRate`, `BasicDataTransmitRates`, `OperationalDataTransmitRates`, `SupportedDataTransmitRates` (kiểm tên trong `cms-dm-tr181-wifi-unfwlcfg.xml` khi làm) | R/RW | có (`0020`): `MaxBitRate`, `Basic/OperationalDataTransmitRates` RW, `PossibleDataTransmitRates` ← `SupportedDataTransmitRates`, `AutoRateFallBackEnabled` const 1 | — |
| `MACAddressControlEnabled` | `AccessPoint.{a}.X_BROADCOM_COM_WlFltMacMode` (`disabled/allow/deny`) | RW | có (`0020`): 1 → `allow`, 0 → `disabled` | — |
| `WEPEncryptionLevel, InsecureOOBAccessEnabled, BeaconAdvertisementEnabled, LocationDescription, TotalIntegrityFailures, ChannelsInUse, DistanceFromRoot, PeerBSSID, AuthenticationServiceMode, DeviceOperationMode, TotalPSKFailures` | `Radio.{r}.ChannelsInUse`, còn lại const/không có | R | có (`0020`): `ChannelsInUse` ← Radio, còn lại const | — |
| `TotalBytesSent/Received, TotalPacketsSent/Received` | `SSID.{i}.Stats.Bytes*/Packets*` | R | có (`0020`) | — |
| `Stats.` (TR-098 Amd 2 WiFiLAN:2, 11 leaf `ErrorsSent … UnknownProtoPacketsReceived`) | `SSID.{i}.Stats.` cùng tên | R | có (`0020`) | — |
| `AssociatedDevice.{i}.` `AssociatedDeviceMACAddress, AssociatedDeviceIPAddress, AssociatedDeviceAuthenticationState, LastRequestedUnicastCipher, LastRequestedMulticastCipher, LastPMKId, LastDataTransmitRate` | `AccessPoint.{a}.AssociatedDevice.{i}.` `MACAddress, AuthenticationState, LastDataDownlinkRate/UplinkRate, SignalStrength`; IP từ `Hosts.Host` theo MAC | R | có (`0020`): 7/7, IP tra `Hosts.Host` theo MAC | — |
| `WPS.` (TR-098 Amd 2: `Enable, DeviceName, DevicePassword, ConfigMethodsSupported, ConfigMethodsEnabled, SetupLockedState, ConfigurationState`) | `AccessPoint.{a}.WPS.` `Enable, ConfigMethodsSupported/Enabled, PIN, Status`, `WiFi.X_BROADCOM_COM_WpsCfg.WpsDeviceName/WpsDevicePin` | RW | có (`0020`): `DeviceName/DevicePassword` ← `WpsCfg`, `ConfigurationState` ← `Wsc_config_state`, ConfigMethods giữ chữ TR-181 (`PushButton,PIN`) | — |
| `X_MARUSYS_COM_OperatingChannelBandwidth`, `X_MARUSYS_COM_SecurityModeEnabled/ModesSupported` | `Radio.OperatingChannelBandwidth`, `Security.ModeEnabled/ModesSupported` | RW | có | — |
| `X_BROADCOM_COM_WlanAdapter.*` (Legacy98, ~200 param) | NVRAM qua `WiFi.*.X_BROADCOM_COM_*` | — | — | không làm, ACS mới nên dùng `X_MARUSYS_COM_*` |

Rủi ro riêng WiFi: set security/channel đi qua `mdm_cbk_wifi` → nvram → `wifi_apply` (restart
`wl`/hostapd), ảnh hưởng MLO/WBD — regression theo
[mlo_throughput_test_matrix.md](mlo_throughput_test_matrix.md) sau khi phần 3 lên board.

## Phần 4 — Hệ thống

| TR-098 | TR-181 (MDM) | RW MDM | Hiện có | Việc |
|---|---|---|---|---|
| `InternetGatewayDevice.DeviceSummary` | chuỗi cố định theo cây đã có, ví dụ `InternetGatewayDevice:1.4[](Baseline:1, EthernetLAN:1, WiFiLAN:1, EthernetWAN:1, Time:1, IPPing:1)` | R | có (`0021`) | cập nhật khi thêm profile |
| `LANDeviceNumberOfEntries`, `WANDeviceNumberOfEntries` | const `1`, `1` | R | có (`0021`) | — |
| `IPv4/IPv6 Download/Upload/UDPEcho DiagnosticsSupported` | `IP.Diagnostics.IPv4UDPEchoDiagnosticsSupported` (TRUE), Download/Upload: không có trong dump | R | có (`0021`, tất cả `0`) | bật `1` khi có TR-143 |
| `DeviceInfo.` 16 leaf | `DeviceInfo.*` (+ override UCI `cwmp.cpe.*`, `0016`) | R | có | — |
| `DeviceInfo.ModemFirmwareVersion, EnabledOptions` | không có | R | có (`0021`, rỗng) | — |
| `DeviceInfo.VendorConfigFile.{i}.` `Name, Version, Date, Description` | `DeviceInfo.VendorConfigFile.{i}.` cùng tên (ref: 1) | R | có (`0021`) | — |
| `DeviceInfo.MemoryStatus.Total/Free`, `ProcessStatus.CPUUsage` (TR-098 Amd 2) | `DeviceInfo.MemoryStatus.*`, `ProcessStatus.CPUUsage` (ref: 0 — STL devinfo có thể chưa điền) | R | có (`0021`) | giá trị 0 nếu devinfo_md không điền |
| `ManagementServer.` (33 leaf) | UCI `cwmp` của icwmpd, sync hai chiều với `Device.ManagementServer.*` | RW | có (bản iopsys) | — |
| `Time.` `Enable, Status, NTPServer1..5, CurrentLocalTime, LocalTimeZone, LocalTimeZoneName, DaylightSavingsUsed/Start/End` | `Device.Time.` 1:1 (`X_BROADCOM_COM_LocalTimeZoneName`, `X_BROADCOM_COM_DaylightSavings*`) | RW | có (`0021`, `system_bdk.c`) | — |
| `IPPingDiagnostics.` `DiagnosticsState, Interface, Host, NumberOfRepetitions, Timeout, DataBlockSize, DSCP, SuccessCount, FailureCount, Average/Minimum/MaximumResponseTime` | `IP.Diagnostics.IPPing.` cùng tên (`Interface` = path TR-181 ↔ path IGD của WANIPConnection) | RW | có (`0021`): bảng + dịch `Interface`; icwmpd nhận `CMS_MSG_PING_STATE_CHANGED` (tr69_md forward) → `8 DIAGNOSTICS COMPLETE` | test: set `Host`, `DiagnosticsState=Requested` → session mới với `8 DIAGNOSTICS COMPLETE`, đọc `SuccessCount` |
| `TraceRouteDiagnostics.` (TR-098 Amd 1: `DiagnosticsState, Interface, Host, NumberOfTries, Timeout, DataBlockSize, DSCP, MaxHopCount, ResponseTime, NumberOfRouteHops, RouteHops.{i}.HopHost/HopHostAddress/HopErrorCode/HopRTTimes`) | `IP.Diagnostics.TraceRoute.` + `RouteHops.{i}.` | RW | có (`0021`, + `RouteHops.{i}`) | — |
| `DownloadDiagnostics`, `UploadDiagnostics` | không có trong dump (profile) | — | — | chỉ khi bật `BUILD_TR143` |
| `DeviceConfig.ConfigFile` | `icwmp_bdk_apply_vendor_config` (set), get = dump config (`cmsMgm_writeConfigToBuf`?) | RW | thiếu | sau |

## Phần 5 — Routing / Bridging / vendor

| TR-098 | TR-181 (MDM) | RW MDM | Hiện có | Việc |
|---|---|---|---|---|
| `Layer3Forwarding.DefaultConnectionService`, `ForwardNumberOfEntries`, `Forwarding.{i}.` `Enable, Status, Type, DestIPAddress, DestSubnetMask, SourceIPAddress, SourceSubnetMask, ForwardingPolicy, GatewayIPAddress, Interface, ForwardingMetric, MTU` | `Routing.Router.1.IPv4Forwarding.{i}.` (`StaticRoute`, `Origin`), `Router.1.X_BROADCOM_COM_DefaultConnectionServices` | RW, Add/Delete | có (`0022`): Add = route tĩnh (`StaticRoute=1`), `DEL_ALL` chỉ xóa route tĩnh, `Interface` dịch path | — |
| `Layer2Bridging.` (7) + `Bridge.{i}` + `Filter.{i}` + `AvailableInterface.{i}` | `Bridging.Bridge.1` + `Port.{i}` (5) | R (view) | thiếu | read-only view, Filter không map |
| `X_BROADCOM_COM_*` root (Syslog, Login, IGMP, PwrMngt) | `X_BROADCOM_COM_SyslogCfg`, `LoginCfg`, `IGMPCfg`, `PwrMngtCfg` | RW | thiếu | thay các `X_IOPSYS_EU_*` bị bỏ, chỉ khi ACS cần |
| `QueueManagement.*` | `QoS.*` (401 object) | RW | — | không làm nếu ACS không dùng |

## Phần 6 — Wi-Fi 7 / MLO / thông tin client / mesh (proxy TR-181)

TR-098 không có object nào cho Wi-Fi 7, MLO, EasyMesh hay client chi tiết. Trên SDK này toàn bộ
dữ liệu đó nằm trong TR-181 (`Device.WiFi.DataElements.*` = EasyMesh R5 Data Elements, 89 object /
620 param, `cms-dm-tr181-wifi-dataelements.xml`, profile `Device2_WiFiDataElements:1` bật bởi
`BUILD_WLDATAELD=y` — MO77300EB có, `make.common:2769`). Thay vì map từng leaf, `0023`
(`67ad62d`) thêm **proxy** `InternetGatewayDevice.X_MARUSYS_COM_Device.<rest>` ↔ `Device.<rest>`:
GPV/GPN/SPV/Add/Delete đi thẳng generic HAL, ACS gọi path TR-181 với tiền tố vendor.

| Nhu cầu | Nguồn TR-181 (đường proxy) | Có gì | Điều kiện |
|---|---|---|---|
| **Client (STA) local** | `WiFi.AccessPoint.{a}.AssociatedDevice.{i}.` `MACAddress, OperatingStandard, AuthenticationState, LastDataDownlink/UplinkRate, AssociationTime, SignalStrength, Noise, Retransmissions, Active` + `.Stats.` (`Bytes/PacketsSent/Received, ErrorsSent, RetransCount, FailedRetransCount, RetryCount`) + `Hosts.Host.{i}` (IP, HostName, lease, `Layer1Interface`) | có sẵn, không cần daemon | bản chuẩn `WLANConfiguration.{i}.AssociatedDevice.{i}` đã map 7 leaf + `X_MARUSYS_COM_SignalStrength/LastData*Rate` |
| **Client toàn mesh** (kể cả STA nối vào agent) | `WiFi.DataElements.Network.Device.{d}.Radio.{r}.BSS.{b}.STA.{s}.` `MACAddress, HT/VHT/HECapabilities, LastDataDownlink/UplinkRate, EstMACDataRateDownlink/Uplink, SignalStrength, LastConnectTime, Bytes/Packets/Errors, RetransCount, IPV4Address, IPV6Address, Hostname` + `.MultiAPSTA.` (`AssociationTime, Noise, SteeringHistory.{i}`, `SteeringSummaryStats`) | 31 + 3 leaf | `wldataeld` chạy (dưới) |
| **Wi-Fi 7 / MLO — trạng thái** | `…Network.Device.{d}.APMLD.{m}.` (`MLDMACAddress`), `.AffiliatedAP.{i}.` (`BSSID, LinkID, RUID, DisabledSubChannels, counters`), `.STAMLD.{s}.` (`MLDMACAddress, Hostname, IPV4Address, IsbSTA, LastConnectTime, counters`), `.STAMLD.{s}.WiFi7Capabilities.` (`EMLMRSupport, EMLSRSupport, NSTRSupport, STRSupport, TIDLinkMapNegotiation`), `.STAMLD.{s}.AffiliatedSTA.{i}.` (`MACAddress, BSSID, SignalStrength, rates, counters`), `Device.{d}.bSTAMLD.` (backhaul MLD), `Device.{d}.MaxNumMLDs, APMLDMaxLinks, bSTAMLDMaxLinks, TIDLinkMapCapability`, `Radio.{r}.Capabilities.WiFi7APRole/WiFi7bSTARole.` | đủ theo EasyMesh R5 | `wldataeld` chạy; ref board dump 28/08 `SupportedStandards=ax` — MO77300EB phải kiểm `Device.WiFi.Radio.{r}.SupportedStandards` có `be` |
| **Wi-Fi 7 / MLO — cấu hình** | Gốc SDK: TR-181 chỉ có `Radio.{r}.OperatingStandards` (RW, có `be` nếu driver hỗ trợ) và `Radio.{r}.X_BROADCOM_COM_WlEhtFeatures` (RW, nvram `wlX_eht_features`, -1 = mặc định). **MLO config thật nằm ở NVRAM**: kernel nvram `wl_mlo_config` (driver đọc lúc module init, `mlo_ipc.c:292-310`) + nvram thường `wl_mlo_bss_enabled`, `wl_mlo_interface`, `wl_mlo_selected_config`, `wl_mlo_ssid`, `wl_mlo_security_mode`, `wl_mlo_wpa_psk`, `wl_mlo_description` do WebUI Marusys `cgi_marusys_mlo.c` ghi — trên UNFWLCFG nvram thường **cũng nằm trong MDM** (wlmdm: có tag → param, không tag → gộp vào `Device.WiFi.X_BROADCOM_COM_WlNvram`), nhưng không có tag nên không đọc/ghi được như param | **đã thêm trong icwmp** (`0025`, phần 7) `X_MARUSYS_COM_MloCfg.` dưới cả hai root | libtr098 đọc/ghi `wl_mlo_*` bằng CLI `nvram`, `nvram kset wl_mlo_config`, và ghi link BSS qua generic HAL — không đổi data model SDK |
| **Mesh — topology / backhaul** | `…Network.` (`ID, ControllerID, DeviceNumberOfEntries`), `Network.Device.{d}.` (`ID`=AL MAC, `Manufacturer, SerialNumber, SoftwareVersion, BackhaulMACAddress, BackhaulALID, BackhaulMediaType, BackhaulPHYRate, CountryCode, MultiAPCapabilities, ControllerOperationMode, RadioNumberOfEntries…`), `.MultiAPDevice.` (`ManufacturerOUI, LastContactTime, EasyMeshController/AgentOperationMode`), `.MultiAPDevice.Backhaul.` (`LinkType, BackhaulMACAddress, BackhaulDeviceID, MACAddress`), `.Radio.{r}.` (`ID, Enabled, Noise, Utilization, Transmit, ReceiveSelf/Other, SteeringPolicy*, thresholds*`), `.Radio.{r}.BSS.{b}.` (`BSSID, SSID, Enabled, byte counters, BackhaulUse, FronthaulUse, STANumberOfEntries`), `Network.MultiAPSteeringSummaryStats.` | đủ | `wldataeld` chạy |
| **Mesh — cấu hình** | `WiFi.X_BROADCOM_COM_WbdCfg.` (`MultiapMode` RW 0/1/2/3, `WbdIfNames`, `MapBHOpen`, `WpsBhIfNames`, `WbdMsgLevel`, `MapMsgLevel`), `WbdCfg.MapBss.{i}.` (`Prefix, Map, BandFlag, Ssid, Akm, Crypto, WpsPsk` — BSS backhaul/fronthaul), `WiFi.SSID.{i}.X_BROADCOM_COM_WlMap` (bit Multi-AP của từng BSS), `WiFi.X_BROADCOM_COM_BsdCfg.` (band steering) | RW | ghi qua proxy → `rcl2_unfwifi` → nvram → restart Wi-Fi (rủi ro như phần 3); `multiap_mode` đổi = restart WBD, RootAP phải giữ mode 3 ([rootap_controller_agent_trace.md](rootap_controller_agent_trace.md)) |
| **Mesh — 1905 topology thô** | `Device.IEEE1905.AL.*` có trong XML (`cms-dm-tr181-ieee1905.xml`, 19 object) nhưng **không có trong dump** ref board và không thấy `DMP_` gate trong `make.common` | không dùng | dùng DataElements thay |

Bật nguồn dữ liệu (một lần, `wldataeld` thu thập DataElements/MLO qua WBD/1905 và ghi MDM bằng
`cmsMdm_*`, `wldataeld/cms_helper.c`; `wlssk` chỉ start khi nvram `wldataeld_enable=1`,
`wlssk_ops.c:607-640`; **MO77300EB default = 0**, `targets/defaultcfg/MO77300EB.conf`):

```sh
nvram set wldataeld_enable=1; nvram commit
wlssk restart        # hoặc reboot; kiểm: pidof wldataeld, ls /var/wldataeld_notify
```

Kiểm qua ubus (image từ `0023`):

```sh
D '{"cmd":"names","path":"InternetGatewayDevice.X_MARUSYS_COM_Device.WiFi.DataElements.Network.","next_level":true}'
D '{"cmd":"get","path":"InternetGatewayDevice.X_MARUSYS_COM_Device.WiFi.DataElements.Network.Device.1.APMLD."}'
D '{"cmd":"get","path":"InternetGatewayDevice.X_MARUSYS_COM_Device.WiFi.DataElements.Network.Device.1.Radio.1.BSS.1.STA."}'
D '{"cmd":"get","path":"InternetGatewayDevice.X_MARUSYS_COM_Device.WiFi.AccessPoint.1.AssociatedDevice."}'
D '{"cmd":"get","path":"InternetGatewayDevice.X_MARUSYS_COM_Device.WiFi.X_BROADCOM_COM_WbdCfg."}'
D '{"cmd":"get","path":"InternetGatewayDevice.X_MARUSYS_COM_Device.WiFi.Radio.1.SupportedStandards"}'   # có "be"?
```

Giới hạn proxy: không gộp vào GPN/GPV của root `InternetGatewayDevice.` (ACS phải gọi đúng
`X_MARUSYS_COM_Device.`), không hỗ trợ Get/SetParameterAttributes (9001) → không có active
notification cho cây này. Muốn một leaf trong đó có tên TR-098 "đẹp" (ví dụ
`WLANConfiguration.{i}.X_MARUSYS_COM_MLDMACAddress`) thì thêm dòng map như phần 3.

## Phần 7 — Cấu hình MLO trong icwmp: `X_MARUSYS_COM_MloCfg.` (`0025`)

Nguồn gốc [Verified source]: `cgi_marusys_mlo.c` (`router/www/broadcom/cgi/`) là nơi duy nhất cấu
hình MLO — MLO AP = một BSS phụ mỗi radio (`wl<u>.1`) chung SSID/PSK; CGI ghi nvram `wl_mlo_*` rồi
đồng bộ sang `wl<u>.1_ssid/wpa_psk/akm/crypto/mfp/bss_enabled`, tính `wl_mlo_config` từ lựa chọn
radio, `nvram kset wl_mlo_config` + `kcommit`. Trên UNFWLCFG `nvram_set()` đi qua `wlmdm`
(`conv_set`: special → mapped → unmapped) nên `wl_mlo_*` nằm trong `Device.WiFi.X_BROADCOM_COM_WlNvram`
(list unmapped) và được lưu cùng config MDM. `0025` (`tr098/bdk/mlo_bdk.c`) dùng đúng các biến đó
qua CLI `nvram get/set/commit` (libnvram link `mdm_cbk_wifi`, không link vào icwmpd được;
kernel nvram chỉ có `kset/kget`), và ghi link BSS qua generic HAL trong cùng batch SPV → RCL wifi
của SDK áp dụng + restart Wi-Fi. Yêu cầu 20/09: nằm trong icwmp, cả hai model, không hard-code.

| Leaf | Kiểu / RW | Nguồn | Ghi |
|---|---|---|---|
| `Enable` | boolean RW | `wl_mlo_bss_enabled` | true: `nvram kset wl_mlo_config=<SelectedConfig>` + `kcommit`, `SSID.{i}.Enable` của link trong config = 1, link khác = 0; false: kset `-1 -1 -1 -1`, mọi link BSS = 0 |
| `LinkRadios` | string RW | `wl_mlo_selected_config` ↔ instance Radio | "1,2,3" = `Device.WiFi.Radio.{i}`, main trước; 2..3 radio, không trùng, unit < 4 → config "0 1 2 -1" (index = wl unit từ `Radio.{i}.Name`); `wl_mlo_interface` (selector WebUI) chỉ cập nhật khi khớp 1 trong 4 preset theo band; nếu `Enable` đang true thì áp dụng ngay như CGI |
| `LinkBssIndex` | unsignedInt RW (0..15) | `wl_mlo_bss_index` (của icwmp), mặc định 1 | BSS nào của mỗi radio là link (`wl<u>.<idx>`, 0 = BSS chính) |
| `LinkInterfaces` | string RO | — | "wl0.1,wl1.1,wl2.1" của LinkRadios hiện tại |
| `SelectedConfig` / `RuntimeConfig` | string RO | `wl_mlo_selected_config` / `nvram kget wl_mlo_config` | khác nhau = chưa reboot |
| `Status` | string RO | — | `Disabled` / `Enabled` / `RebootRequired` (config mong muốn ≠ kernel) |
| `SSID` | string(1..32) RW | `wl_mlo_ssid` | + `Device.WiFi.SSID.{i}.SSID` của mọi link BSS |
| `SecurityMode` | wpa \| owe RW | `wl_mlo_security_mode` | `AccessPoint.{i}.Security.WlAuthAkm/X_BROADCOM_COM_WlWpaEncryption/WlMFP` từng link: wpa → radio 6 GHz `sae/aes/2`, khác `psk2 sae/aes/1` (band từ `Radio.OperatingFrequencyBand`, không hard-code wl0=6G như CGI); owe → `owe/aes/2` |
| `KeyPassphrase` | string RW, đọc trả "" | `wl_mlo_wpa_psk` | 8..63 ký tự hoặc 64 hex → `Security.KeyPassphrase` mọi link |
| `Description` | string(≤64) RW | `wl_mlo_description` | chỉ lưu |

Topology (cache theo `bdk_ctx_generation()`, một lần mỗi RPC): `Radio.{i}.Name` = `wl<u>` →
unit; `Radio.{i}.OperatingFrequencyBand`; link BSS = `SSID.{i}.Name == wl<u>.<LinkBssIndex>`
(GPV cả `Device.WiFi.SSID.` một lần); AP = `AccessPoint.{i}.SSIDReference == Device.WiFi.SSID.<i>`.
Hằng số duy nhất: contract driver `wl_mlo_config` (4 slot, ≤3 link, `mlo_ipc.c:81-129`).

Ràng buộc và rủi ro:

- **Reboot** sau `Enable`/`LinkRadios` — driver chỉ đọc `wl_mlo_config` lúc module init
  ([reference_board_mlo_bringup_guide.md](reference_board_mlo_bringup_guide.md)); `Status` báo.
- nvram ghi ngay ở VALUESET, HAL batch commit sau: batch fail (9007 ở một link) thì `wl_mlo_*`
  đã đổi còn link chưa — SPV lại là khớp. Chấp nhận, ghi lại.
- `nvram commit` mỗi setter (như CGI mỗi lần apply); `nvram set` qua CLI ~ms.
- [Not established] CLI `nvram set/get` từ tiến trình tr69 (icwmpd) đi qua nvram daemon như từ
  httpd — chưa chạy thử; `nvram kget` đã dùng trong bringup guide.
- Tên leaf/object tạm (`X_MARUSYS_COM_`), đổi tên sau khi chốt với ACS — chỉ sửa bảng
  `tMloCfgParam` và hai dòng root.

## Phần 8 — Data model TR-181 chọn bằng config (`0024`)

`cwmp.cpe.datamodel=tr181` (option có sẵn của iopsys, seed `files/cwmp` ghi cách dùng):

| Việc | TR-098 (`tr098`, mặc định) | TR-181 (`tr181`) |
|---|---|---|
| Root / cây tĩnh | `InternetGatewayDevice.` — `tEntry098Obj` (phần 1..7) | `Device.` — `tEntry181Obj` có `WiFi.X_MARUSYS_COM_MloCfg.`, `ManagementServer.` (8 leaf icwmp sở hữu mà MDM không có: `HTTPCompressionSupported/HTTPCompression`, `LightweightNotificationProtocolsSupported/Used`, `UDPLightweightNotificationHost/Port`, `AliasBasedAddressing`, `InstanceMode` — stock `managementserver.c`) + `ManagementServer.X_MARUSYS_COM_Icwmp.` (`tr098/bdk/root181_bdk.c`), `dm_platform_select_root()`; HAL + walk tĩnh gộp cho `Device.`/`Device.WiFi.`/`Device.ManagementServer.` (`proxy_merge_static`), leaf tĩnh trong object HAL nhận diện bằng `proxy_static_leaves` |
| GPV/GPN/SPV/Add/Del | bảng map + proxy `X_MARUSYS_COM_Device.` | mọi path `Device.*` → generic HAL (`dm_platform_param_method`), `""` = `Device.`; `OGF_OMIT_HIDDEN_OBJ_PARAM`, giá trị `isPassword` trả "" (tr69c `writeGetPValueToFile`) |
| Inform forced | `DeviceSummary`, `DeviceInfo.*`, `ManagementServer.*` của cây tĩnh | 6 param `informParameters_TR181` của tr69c (`RootDataModelVersion`, `HardwareVersion`, `SoftwareVersion`, `ProvisioningCode`, `ParameterKey`, `ConnectionRequestURL`) |
| Get/SetParameterAttributes | cây tĩnh (list UCI); proxy 9001 | `bcm_generic_get/setParameterAttributes` như tr69c `doGet/SetParameterAttributes` — notification nằm trong MDM, lưu cùng config, WebUI thấy; chỉ 0/1/2 (lightweight 3..6 → 9003); leaf tĩnh (MloCfg) vẫn list UCI; SPA → reload config |
| Value change | `DM_ENABLED_NOTIFY` do walk cây tĩnh | file dựng từ attribute MDM (GPA `Device.` → param có notif, GPV theo chunk 64) bởi `dm_platform_enabled_notify()`, engine nối thêm leaf tĩnh; thread notify của icwmp chạy y nguyên (GPV từng dòng qua hook); active: MDM gửi `CMS_MSG_TR69_ACTIVE_NOTIFICATION` → icwmpd đánh thức thread (đã có) |
| ManagementServer | libtr098 ghi UCI, icwmpd đẩy UCI→MDM cuối session | ACS ghi thẳng MDM (URL/Username/Password/PeriodicInform*/ConnectionRequest*/CWMPRetry*/UpgradesManaged/EnableCWMP); icwmpd kéo MDM→UCI cuối session + reload; `ParameterKey`/`ConnectionRequestURL` override khi đọc và đẩy vào MDM; 8 leaf icwmp-only từ UCI (bảng trên); `DeviceInfo.*` override identity UCI như DeviceId |
| Cấu hình icwmp từ ACS (`0026`) | `InternetGatewayDevice.ManagementServer.X_MARUSYS_COM_Icwmp.` | `Device.ManagementServer.X_MARUSYS_COM_Icwmp.` — `DataModel` tr098\|tr181 (đổi root từ ACS, hiệu lực session sau), `AmdVersion` 1..5, `LogSeverity` EMERG..DEBUG, `SessionTimeout` 1..3600, `ConnectionRequestPort` RO, `DataModelBackend` RO; = UCI `cwmp.cpe.datamodel/amd_version/log_severity/session_timeout/port`, set → `END_SESSION_RELOAD` |
| Đổi mode | `uci set cwmp.cpe.datamodel=…; commit; ubus call tr069 command reload` (icwmpd `icwmp_bdk_load_mode()`, libtr098 `bdk_proxy_load_mode()` đọc lại) | |

Giới hạn: GPN/GPV `Device.` toàn cây = ~20k param như tr69c (ACS thường gọi NextLevel); alias-based
addressing không; `Device.ManagementServer.ConnectionRequestURL` là `http://<ip>:<port>/` với
`ip` = varstate `cwmp.cpe.ip` (netlink) — sửa này áp dụng cả TR-098 (Inform trước đây rỗng).

## Không map được (TR-181 của BDK không hỗ trợ)

| TR-098 | Lý do |
|---|---|
| `LANHostConfigManagement.ReservedAddresses` | `Pool.ReservedAddresses` NotSupported |
| `DHCPConditionalServingPool.{i}` | `Pool.Chaddr/ChaddrMask/VendorClassID/ClientID/UserClassID` NotSupported |
| `Hosts.Host.{i}.VendorClassID/ClientID/UserClassID` | NotSupported |
| `WANIPConnection.DNSEnabled` set | `DNS.Client.Enable` RW nhưng tắt DNS client = mất DNS WAN, để read-only |
| `Layer2Bridging.Filter` | Bridging BDK không có Filter (`MaxFilterEntries=0`) |
| `UPnP`, `DNS.Relay` | NotSupported |

## Chưa chứng minh được

- **[Not established]** Dump ref board là profile 96765REF1; MO77300EB có thể khác ở object Wi-Fi
  (số radio/SSID), `Ethernet.Interface` (eth1 WAN) và `DMP_*` bật. Xác nhận bằng `dumpmdm` trên
  board MO77300EB (cùng lệnh, lưu vào `logs/`), rồi chạy lại parser ở scratchpad.
- **[Not established]** RCL của một số param `ReadWrite` trong XML có thật sự áp dụng runtime không
  (ví dụ `Ethernet.Interface.Enable`, `Radio.RegulatoryDomain`) — chỉ chứng minh bằng SPV trên board.
- **[Not established]** `AccessPoint.{a}` có luôn cùng số với `SSID.{i}` trên MO77300EB; code scan
  `SSIDReference` nên đúng cả khi khác, nhưng chưa thấy trường hợp khác.
- **[Not established]** `MemoryStatus`/`ProcessStatus` STL của `devinfo_md` có điền không (ref: 0).
- **[Not established]** `0024`/`0025` chưa qua compiler. `bcm_generic_setParameterAttributes`
  nhận path object (subtree) như tr69c truyền — suy từ `dmCms.c:996`, chưa đọc implementation.
- **[Not established]** CLI `nvram set` từ icwmpd (tiến trình tr69, không phải httpd) ghi được
  `wl_mlo_*` qua nvram daemon — kiểm bằng phần 7 gate 3.

## Patch/debug artifact liên quan

- Code: `issues/20260916_tr069_app_use_icwmp/sdk-overlay/userspace/` (commit theo phần), patch
  `sdk-overlay/00NN-*.patch`, tarball `icwmp_bdk_port_overlay.tar.gz`.
- Test: `issues/20260916_tr069_app_use_icwmp/debug-commands.md` gate 3 (GPV/GPN/SPV) — cập nhật
  theo từng phần.
- Thiết kế: [icwmp_bdk_port_design.md](icwmp_bdk_port_design.md); guide TR-181 phía MDM:
  [tr181_parameter_development_guide.md](tr181_parameter_development_guide.md).
