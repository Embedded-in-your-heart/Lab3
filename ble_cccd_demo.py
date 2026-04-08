#!/usr/bin/env python3
"""
BLE Central Program - CCCD Demo
Scans for BLE devices, connects to a selected device,
and sets the CCCD (Client Characteristic Configuration Descriptor)
value to 0x0002 (Enable Indication) on a chosen characteristic.

Usage: sudo python3 ble_cccd_demo.py
"""

import struct
import sys
import time
from bluepy.btle import Peripheral, UUID, DefaultDelegate, Scanner

# CCCD UUID is standardized as 0x2902
CCCD_UUID = UUID(0x2902)

# CCCD values
CCCD_NOTIFY = 0x0001
CCCD_INDICATE = 0x0002
CCCD_DISABLE = 0x0000


class NotificationDelegate(DefaultDelegate):
    """Delegate to handle notifications/indications from the peripheral."""

    def __init__(self):
        DefaultDelegate.__init__(self)

    def handleNotification(self, cHandle, data):
        print("[Notification/Indication] Handle: 0x%04X, Data: %s" %
              (cHandle, data.hex()))


class ScanDelegate(DefaultDelegate):
    """Delegate to handle scan discovery events."""

    def __init__(self):
        DefaultDelegate.__init__(self)

    def handleDiscovery(self, dev, isNewDev, isNewData):
        if isNewDev:
            print("  Found: %s" % dev.addr)


def scan_devices(duration=10.0):
    """Scan for BLE devices and return the list."""
    print("=== Scanning for BLE devices (%d seconds) ===" % int(duration))
    scanner = Scanner().withDelegate(ScanDelegate())
    try:
        devices = scanner.scan(duration)
    except Exception:
        # bluepy scan stop may raise spurious disconnect errors
        devices = scanner.getDevices()

    dev_list = []
    for i, dev in enumerate(devices):
        name = ""
        for (adtype, desc, value) in dev.getScanData():
            if desc == "Complete Local Name" or desc == "Short Local Name":
                name = value
        print("%d: %s [%s] RSSI=%d dB %s" %
              (i, dev.addr, dev.addrType, dev.rssi, name))
        dev_list.append(dev)

    return dev_list


def list_services_and_chars(peripheral):
    """List all services and characteristics of the connected device.
    Returns list of (characteristic, svc_end_handle) tuples."""
    print("\n=== Services and Characteristics ===")
    char_list = []

    for svc in peripheral.services:
        print("\nService: %s" % str(svc))
        svc_end = svc.hndEnd
        for ch in svc.getCharacteristics():
            props = ch.propertiesToString()
            print("  Char: %s (Handle: 0x%04X) Properties: %s" %
                  (str(ch.uuid), ch.getHandle(), props))
            char_list.append((ch, svc_end))

            if "NOTIFY" in props or "INDICATE" in props:
                print("    -> Supports CCCD")

    return char_list


def find_cccd_characteristics(char_list):
    """Find characteristics that have CCCD descriptors (support notify/indicate)."""
    cccd_chars = []
    for ch, svc_end in char_list:
        props = ch.propertiesToString()
        if "NOTIFY" in props or "INDICATE" in props:
            cccd_chars.append((ch, svc_end))
    return cccd_chars


def find_cccd_handle(peripheral, char_handle, svc_end_handle):
    """
    Search for the CCCD descriptor (UUID 0x2902) in the handle range
    after the characteristic value. Falls back to char_handle + 1.
    """
    try:
        descs = peripheral.getDescriptors(char_handle + 1, svc_end_handle)
        for desc in descs:
            if desc.uuid == CCCD_UUID:
                print("  Found CCCD at handle 0x%04X" % desc.handle)
                return desc.handle
    except Exception:
        pass

    # Fallback
    fallback = char_handle + 1
    print("  CCCD not found via descriptor search, using fallback handle 0x%04X" % fallback)
    return fallback


def set_cccd(peripheral, char_handle, value, svc_end_handle=None):
    """
    Set the CCCD value for a characteristic.
    Searches for the actual CCCD descriptor handle.
    """
    if svc_end_handle:
        cccd_handle = find_cccd_handle(peripheral, char_handle, svc_end_handle)
    else:
        cccd_handle = char_handle + 1

    # Pack the value as little-endian uint16
    cccd_value = struct.pack('<H', value)

    print("\nWriting CCCD value 0x%04X to handle 0x%04X..." %
          (value, cccd_handle))
    try:
        peripheral.writeCharacteristic(cccd_handle, cccd_value)
        print("CCCD write successful!")

        # Verify by reading back the CCCD
        read_back = peripheral.readCharacteristic(cccd_handle)
        read_value = struct.unpack('<H', read_back)[0]
        print("CCCD read back: 0x%04X" % read_value)

        if read_value == value:
            print("CCCD value confirmed: 0x%04X" % read_value)
        else:
            print("Warning: CCCD read back (0x%04X) differs from written value (0x%04X)" %
                  (read_value, value))

        return True
    except Exception as e:
        print("Error writing CCCD: %s" % str(e))
        return False


def main():
    # Step 1: Scan for devices
    dev_list = scan_devices(10.0)

    if not dev_list:
        print("No devices found. Exiting.")
        sys.exit(1)

    # Step 2: Select a device
    print("\n" + "=" * 50)
    number = input("Enter device number to connect: ")
    num = int(number)
    target_dev = dev_list[num]

    print("\nConnecting to %s [%s]..." %
          (target_dev.addr, target_dev.addrType))

    # Step 3: Connect
    peripheral = Peripheral(target_dev.addr, target_dev.addrType)
    peripheral.setDelegate(NotificationDelegate())
    print("Connected!")

    try:
        # Step 4: Discover services and characteristics
        char_list = list_services_and_chars(peripheral)

        # Step 5: Find characteristics with CCCD (notify/indicate support)
        cccd_chars = find_cccd_characteristics(char_list)

        if not cccd_chars:
            print("\nNo characteristics with NOTIFY/INDICATE property found.")
            print("Listing all characteristics for manual selection:")
            for i, (ch, svc_end) in enumerate(char_list):
                print("  %d: %s (Handle: 0x%04X) %s" %
                      (i, ch.uuid, ch.getHandle(), ch.propertiesToString()))
            char_idx = int(input("Enter characteristic number: "))
            selected_char, selected_svc_end = char_list[char_idx]
        else:
            print("\n=== Characteristics with NOTIFY/INDICATE support ===")
            for i, (ch, svc_end) in enumerate(cccd_chars):
                print("  %d: %s (Handle: 0x%04X) %s" %
                      (i, ch.uuid, ch.getHandle(), ch.propertiesToString()))
            char_idx = int(input("Enter characteristic number for CCCD setting: "))
            selected_char, selected_svc_end = cccd_chars[char_idx]

        # Step 6: Set CCCD to 0x0002 (Enable Indication)
        print("\n=== Setting CCCD to 0x0002 (Enable Indication) ===")
        success = set_cccd(peripheral, selected_char.getHandle(),
                           CCCD_INDICATE, selected_svc_end)

        if success:
            print("\n=== CCCD set to 0x0002 successfully! ===")
            print("Waiting for indications (press Ctrl+C to stop)...")

            # Step 7: Wait and listen for indications
            try:
                while True:
                    if peripheral.waitForNotifications(1.0):
                        continue
                    print("  (waiting for indications...)")
            except KeyboardInterrupt:
                print("\nStopped listening.")

            # Step 8: Optionally reset CCCD to 0x0000
            print("\nDisabling CCCD (setting to 0x0000)...")
            set_cccd(peripheral, selected_char.getHandle(),
                     CCCD_DISABLE, selected_svc_end)

    finally:
        print("\nDisconnecting...")
        peripheral.disconnect()
        print("Disconnected.")


if __name__ == "__main__":
    main()
