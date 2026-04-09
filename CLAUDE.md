# Lab3 - BLE Communication

## 專案概述

嵌入式系統實驗 Lab3，使用 Raspberry Pi 3 作為 BLE Central，與手機（iOS/Android）上的 BLE Tool App 通訊。主要目標是示範 CCCD（Client Characteristic Configuration Descriptor）設定，將 CCCD 寫入 0x0002（Enable Indication）。

## 檔案結構

```
Lab3/
├── ble_scan_conn.py          # 基礎 BLE 掃描與連線（bluepy）
├── ble_cccd_demo.py          # CCCD 設定示範 — 主要實驗程式（bluepy）
├── pyproject.toml            # uv 套件管理（相依：bluepy）
├── gattlib_c/                # Option: C 語言版本
│   ├── gattlib_cccd_demo.c   # GATTLIB CCCD 示範
│   └── Makefile              # 編譯用
├── README.md                 # 操作說明
└── .gitignore
```

## 技術架構

### BLE 角色

- **Raspberry Pi 3**：BLE Central（Client） — 掃描、連線、讀寫 CCCD
- **手機 App**：BLE Peripheral（Server） — 廣播、提供 GATT Service

### 使用的 BLE App

- **nRF Connect**（iOS/Android）：可建立 GATT Server + Advertiser，有 Server Log
- **LightBlue**（iOS）：連線中可手動推 Value，功能較完整
- **BLE Tool / BLE Scanner**（Android）

### GATT 結構

實驗操作的目標 Service/Characteristic：

```
Service: FFF0 (自訂測試用)
└── Characteristic: FFF1
    ├── Properties: Read, Write, Notify, Indicate
    ├── Descriptor: Extended Properties (0x2900) — 可能存在
    └── Descriptor: CCCD (0x2902) — 實驗目標
```

## 實驗過程中遇到的問題與解法

### 1. bluepy scanner.scan() 偶發斷線

bluepy 的 `scanner.stop()` 會拋出 `BTLEDisconnectError`。
**解法**：try-except 包住，fallback 用 `scanner.getDevices()`。

### 2. CCCD handle 偏移問題

假設 CCCD 在 `value_handle + 1` 是錯的。當 Characteristic 有 Extended Properties (0x2900) 時，CCCD 在 `value_handle + 2`。
**解法**（Python）：用 `peripheral.getDescriptors()` 搜尋 UUID 0x2902 找到正確 handle。

### 3. iOS getDescriptors() 觸發斷線

在 iOS 裝置上呼叫 `ch.getDescriptors()` 會導致連線中斷。
**解法**：改用 Characteristic properties 判斷是否支援 NOTIFY/INDICATE，不呼叫 getDescriptors。

### 4. iOS CCCD 顯示 N/A

nRF Connect 的 CCCD Value 永遠顯示 N/A，因為是 "OS-Managed Attribute"。
**證明方式**：用 Server Log 的 `subscribed` / `unsubscribed` 訊息，或 Python 端 read back 驗證。

### 5. gattlib（C 版）scan 卡住

缺少 GLib Main Loop。gattlib 底層透過 D-Bus 跟 BlueZ 通訊，必須有 event loop。
**解法**：所有 BLE 操作放在 `ble_task()` 裡，由 `gattlib_mainloop()` 啟動。

### 6. gattlib 手動寫 CCCD handle 失敗（err=2）

gattlib 回報的 service end handle 比 bluepy 的小，導致 CCCD handle 超出範圍。
**解法**：改用 `gattlib_notification_start()` / `gattlib_notification_stop()`，讓 gattlib 透過 D-Bus 內部處理 CCCD。

### 7. gattlib 觸發 iOS 配對請求

gattlib 走 BlueZ daemon，BlueZ 預設會嘗試配對；bluepy 直接操作 HCI 層，用最低安全等級。
**解法**：連線時指定 `GATTLIB_CONNECTION_OPTIONS_LEGACY_BT_SEC_LOW`。

### 8. gattlib notification_start UUID 格式問題（err=1）

16-bit UUID（如 0xfff1）需轉成完整 128-bit 字串格式才能匹配 D-Bus 路徑。
**解法**：先 `gattlib_uuid_to_string()` 再 `gattlib_string_to_uuid()` 轉回完整格式，附帶 fallback。

## Python vs C 版本差異

| 面向 | Python (bluepy) | C (gattlib) |
|------|-----------------|-------------|
| 底層 | 自己的 bluepy-helper → HCI | D-Bus → BlueZ daemon |
| 配對 | 不觸發（最低安全等級） | 需明確指定 SEC_LOW |
| CCCD 寫入 | 手動寫 handle | 用 notification_start API |
| Service 探索 | handle 範圍正確 | end handle 可能偏小 |
| Event loop | 不需要 | 必須用 gattlib_mainloop |

## 部署環境

- **開發機**：macOS（編輯程式碼、git push）
- **執行機**：Raspberry Pi 3（Debian/Ubuntu、BlueZ 5.x）
- **測試裝置**：iOS（nRF Connect）或 Android（BLE Tool）
- **Python 套件管理**：uv
- **遠端 repo**：git@github.com:Embedded-in-your-heart/Lab3.git
