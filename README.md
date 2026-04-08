# Lab3 - BLE Communication (Raspberry Pi 3)

使用 Raspberry Pi 3 作為 BLE Central，透過 Python (bluepy) 與 Android 手機上的 BLE Tool App 進行通訊。

## 環境需求

- Raspberry Pi 3（或其他支援 BLE 的 Linux 主機）
- Python 3
- bluepy 套件
- Android 手機安裝 BLE Tool（或 BLE Scanner by Bluepixel）

### 安裝 bluepy

```bash
sudo apt-get install python3-pip libglib2.0-dev
sudo pip3 install bluepy
```

## 程式說明

### ble_scan_conn.py - 基礎掃描與連線

掃描附近 BLE 裝置，連線後讀取指定 Service (0xFFF0) 的 Characteristic (0xFFF1)。

```bash
sudo python3 ble_scan_conn.py
```

操作步驟：
1. 程式啟動後會掃描 10 秒
2. 列出所有找到的 BLE 裝置
3. 輸入要連線的裝置編號
4. 自動連線並列出 Service、讀取 Characteristic

### ble_cccd_demo.py - CCCD 設定示範（主要實驗）

示範設定 CCCD (Client Characteristic Configuration Descriptor) 值為 0x0002（啟用 Indication）。

```bash
sudo python3 ble_cccd_demo.py
```

操作步驟：
1. 開啟 Android 手機上的 BLE Tool App，啟動 GATT Server
2. 在 Raspberry Pi 執行程式
3. 等待掃描完成（10 秒），找到手機裝置
4. 輸入手機對應的裝置編號
5. 程式連線後會列出所有 Service 和 Characteristic
6. 選擇一個支援 NOTIFY/INDICATE 的 Characteristic
7. 程式自動寫入 CCCD = 0x0002 並讀回驗證
8. 進入等待 Indication 模式，按 `Ctrl+C` 停止
9. 程式自動還原 CCCD = 0x0000 並斷線

## CCCD 說明

| CCCD 值 | 意義 |
|---------|------|
| 0x0000 | 停用通知/指示 |
| 0x0001 | 啟用 Notification（不需 ACK） |
| 0x0002 | 啟用 Indication（需 ACK） |

## 注意事項

- 所有程式需要 `sudo` 權限（BLE 操作需要 root）
- BLE Tool App 的 UI 可能無法即時顯示 CCCD 變更，可查看 App 的 Server Log 作為證明
- 如果連線失敗，確認手機 App 正在廣播且距離夠近
