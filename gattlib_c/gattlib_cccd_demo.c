/**
 * gattlib_cccd_demo.c
 *
 * BLE Central program using GATTLIB (C language).
 * Scans for BLE devices, connects to a selected device,
 * discovers services/characteristics, and sets CCCD to 0x0002 (Indication).
 *
 * Build: make
 * Usage: sudo ./gattlib_cccd_demo
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <pthread.h>
#include <gattlib.h>

#define MAX_DEVICES 64
#define SCAN_TIMEOUT 10

/* CCCD values */
#define CCCD_DISABLE  0x0000
#define CCCD_NOTIFY   0x0001
#define CCCD_INDICATE 0x0002

/* Stored scan results */
static char discovered_addr[MAX_DEVICES][18];
static char discovered_name[MAX_DEVICES][64];
static int device_count = 0;
static volatile int running = 1;

/* Connection state (set by async callback) */
static gattlib_connection_t *g_connection = NULL;
static volatile int g_connected = 0;
static pthread_mutex_t g_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t g_cond = PTHREAD_COND_INITIALIZER;

static void sigint_handler(int sig) {
    running = 0;
}

/**
 * Callback invoked for each discovered BLE device during scanning.
 */
static void on_device_discovered(gattlib_adapter_t *adapter, const char *addr,
                                  const char *name, void *user_data) {
    if (device_count >= MAX_DEVICES)
        return;

    /* Skip duplicates */
    for (int i = 0; i < device_count; i++) {
        if (strcmp(discovered_addr[i], addr) == 0)
            return;
    }

    strncpy(discovered_addr[device_count], addr, 17);
    discovered_addr[device_count][17] = '\0';

    if (name) {
        strncpy(discovered_name[device_count], name, 63);
        discovered_name[device_count][63] = '\0';
    } else {
        discovered_name[device_count][0] = '\0';
    }

    printf("  %d: %s %s\n", device_count, addr,
           name ? name : "(unknown)");
    device_count++;
}

/**
 * Callback invoked when connection is established.
 */
static void on_connected(gattlib_adapter_t *adapter, const char *dst,
                          gattlib_connection_t *conn, int error,
                          void *user_data) {
    if (error != 0) {
        fprintf(stderr, "Error: Connection failed (err=%d)\n", error);
        pthread_mutex_lock(&g_mutex);
        g_connected = -1;
        pthread_cond_signal(&g_cond);
        pthread_mutex_unlock(&g_mutex);
        return;
    }

    pthread_mutex_lock(&g_mutex);
    g_connection = conn;
    g_connected = 1;
    pthread_cond_signal(&g_cond);
    pthread_mutex_unlock(&g_mutex);
}

/**
 * Callback for incoming notifications/indications.
 */
static void on_notification(const uuid_t *uuid, const uint8_t *data,
                             size_t len, void *user_data) {
    printf("[Indication] Data (%zu bytes): ", len);
    for (size_t i = 0; i < len; i++) {
        printf("%02X ", data[i]);
    }
    printf("\n");
}

/**
 * Scan for nearby BLE devices.
 */
static int scan_devices(gattlib_adapter_t *adapter) {
    printf("=== Scanning for BLE devices (%d seconds) ===\n", SCAN_TIMEOUT);

    int ret = gattlib_adapter_scan_enable(
        adapter,
        on_device_discovered,
        SCAN_TIMEOUT,
        NULL
    );

    if (ret != GATTLIB_SUCCESS) {
        fprintf(stderr, "Error: Failed to scan (err=%d)\n", ret);
        return -1;
    }

    printf("\nFound %d device(s).\n", device_count);
    return 0;
}

/**
 * Discover and print all services and characteristics.
 */
static void discover_services(gattlib_connection_t *conn) {
    gattlib_primary_service_t *services;
    gattlib_characteristic_t *chars;
    int svc_count, char_count;

    printf("\n=== Services ===\n");
    int ret = gattlib_discover_primary(conn, &services, &svc_count);
    if (ret != GATTLIB_SUCCESS) {
        fprintf(stderr, "Error: Service discovery failed (err=%d)\n", ret);
        return;
    }

    for (int i = 0; i < svc_count; i++) {
        char uuid_str[MAX_LEN_UUID_STR + 1];
        gattlib_uuid_to_string(&services[i].uuid, uuid_str, sizeof(uuid_str));
        printf("\nService: %s (Handle: 0x%04X - 0x%04X)\n",
               uuid_str, services[i].attr_handle_start,
               services[i].attr_handle_end);
    }
    free(services);

    printf("\n=== Characteristics ===\n");
    ret = gattlib_discover_char(conn, &chars, &char_count);
    if (ret != GATTLIB_SUCCESS) {
        fprintf(stderr, "Error: Characteristic discovery failed (err=%d)\n", ret);
        return;
    }

    for (int i = 0; i < char_count; i++) {
        char uuid_str[MAX_LEN_UUID_STR + 1];
        gattlib_uuid_to_string(&chars[i].uuid, uuid_str, sizeof(uuid_str));
        printf("  Char %d: %s (Value Handle: 0x%04X, Properties: 0x%02X)\n",
               i, uuid_str, chars[i].value_handle, chars[i].properties);

        printf("           [");
        if (chars[i].properties & GATTLIB_CHARACTERISTIC_READ)
            printf(" READ");
        if (chars[i].properties & GATTLIB_CHARACTERISTIC_WRITE)
            printf(" WRITE");
        if (chars[i].properties & GATTLIB_CHARACTERISTIC_WRITE_WITHOUT_RESP)
            printf(" WRITE_NO_RESP");
        if (chars[i].properties & GATTLIB_CHARACTERISTIC_NOTIFY)
            printf(" NOTIFY");
        if (chars[i].properties & GATTLIB_CHARACTERISTIC_INDICATE)
            printf(" INDICATE");
        printf(" ]\n");
    }
    free(chars);
}

/**
 * Write CCCD value to enable Indication (0x0002) on a given handle.
 */
static int write_cccd(gattlib_connection_t *conn, uint16_t cccd_handle,
                       uint16_t value) {
    uint8_t cccd_val[2];
    cccd_val[0] = value & 0xFF;
    cccd_val[1] = (value >> 8) & 0xFF;

    printf("\nWriting CCCD value 0x%04X to handle 0x%04X...\n",
           value, cccd_handle);

    int ret = gattlib_write_char_by_handle(conn, cccd_handle, cccd_val, 2);
    if (ret != GATTLIB_SUCCESS) {
        fprintf(stderr, "Error: CCCD write failed (err=%d)\n", ret);
        return -1;
    }

    printf("CCCD write successful!\n");
    return 0;
}

int main(int argc, char *argv[]) {
    gattlib_adapter_t *adapter;
    int ret;

    signal(SIGINT, sigint_handler);

    /* Step 1: Open Bluetooth adapter */
    ret = gattlib_adapter_open(NULL, &adapter);
    if (ret != GATTLIB_SUCCESS) {
        fprintf(stderr, "Error: Failed to open adapter (err=%d)\n", ret);
        return 1;
    }

    /* Step 2: Scan for devices */
    ret = scan_devices(adapter);
    if (ret != 0 || device_count == 0) {
        fprintf(stderr, "No devices found.\n");
        gattlib_adapter_close(adapter);
        return 1;
    }

    /* Step 3: Select device */
    int num;
    printf("\nEnter device number to connect: ");
    if (scanf("%d", &num) != 1 || num < 0 || num >= device_count) {
        fprintf(stderr, "Invalid selection.\n");
        gattlib_adapter_close(adapter);
        return 1;
    }

    printf("\nConnecting to %s (%s)...\n",
           discovered_addr[num], discovered_name[num]);

    /* Step 4: Connect (async with callback) */
    ret = gattlib_connect(adapter, discovered_addr[num],
                          GATTLIB_CONNECTION_OPTIONS_LEGACY_DEFAULT,
                          on_connected, NULL);
    if (ret != GATTLIB_SUCCESS) {
        fprintf(stderr, "Error: Connection request failed (err=%d)\n", ret);
        gattlib_adapter_close(adapter);
        return 1;
    }

    /* Wait for connection callback */
    pthread_mutex_lock(&g_mutex);
    while (g_connected == 0) {
        pthread_cond_wait(&g_cond, &g_mutex);
    }
    pthread_mutex_unlock(&g_mutex);

    if (g_connected < 0 || g_connection == NULL) {
        fprintf(stderr, "Error: Connection failed.\n");
        gattlib_adapter_close(adapter);
        return 1;
    }
    printf("Connected!\n");

    /* Step 5: Discover services and characteristics */
    discover_services(g_connection);

    /* Step 6: Get CCCD handle from user */
    unsigned int cccd_handle;
    printf("\nEnter CCCD handle (hex, e.g. 005D): ");
    if (scanf("%x", &cccd_handle) != 1) {
        fprintf(stderr, "Invalid handle.\n");
        gattlib_disconnect(g_connection, false);
        gattlib_adapter_close(adapter);
        return 1;
    }

    /* Step 7: Write CCCD = 0x0002 (Enable Indication) */
    printf("\n=== Setting CCCD to 0x0002 (Enable Indication) ===\n");
    ret = write_cccd(g_connection, (uint16_t)cccd_handle, CCCD_INDICATE);
    if (ret != 0) {
        gattlib_disconnect(g_connection, false);
        gattlib_adapter_close(adapter);
        return 1;
    }

    /* Step 8: Register notification handler and wait */
    gattlib_register_notification(g_connection, on_notification, NULL);

    printf("\nWaiting for indications (press Ctrl+C to stop)...\n");
    while (running) {
        sleep(1);
    }

    /* Step 9: Disable CCCD */
    printf("\nDisabling CCCD (setting to 0x0000)...\n");
    write_cccd(g_connection, (uint16_t)cccd_handle, CCCD_DISABLE);

    /* Step 10: Disconnect */
    printf("\nDisconnecting...\n");
    gattlib_disconnect(g_connection, false);
    gattlib_adapter_close(adapter);
    printf("Done.\n");

    return 0;
}
