/**
 * gattlib_cccd_demo.c
 *
 * BLE Central program using GATTLIB (C language).
 * Scans for BLE devices, connects to a selected device,
 * discovers services/characteristics, and sets CCCD to 0x0002 (Indication).
 *
 * Based on gattlib examples: ble_scan, discover, notification, read_write.
 *
 * Build: make
 * Usage: sudo ./gattlib_cccd_demo
 */

#include <ctype.h>
#include <pthread.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <glib.h>
#include "gattlib.h"

#define MAX_DEVICES 64
#define SCAN_TIMEOUT 10

/* CCCD values */
#define CCCD_DISABLE  0x0000
#define CCCD_NOTIFY   0x0001
#define CCCD_INDICATE 0x0002

/* Scan results */
static char discovered_addr[MAX_DEVICES][18];
static char discovered_name[MAX_DEVICES][64];
static int device_count = 0;

/* Synchronization for async connection */
static pthread_cond_t g_conn_cond = PTHREAD_COND_INITIALIZER;
static pthread_mutex_t g_conn_mutex = PTHREAD_MUTEX_INITIALIZER;
static gattlib_connection_t *g_connection = NULL;
static int g_conn_error = -1;

/* For clean Ctrl+C shutdown */
static gattlib_adapter_t *g_adapter = NULL;

static void sigint_handler(int sig) {
	printf("\n\nCaught SIGINT, cleaning up...\n");

	if (g_connection != NULL) {
		gattlib_disconnect(g_connection, false);
		g_connection = NULL;
	}
	if (g_adapter != NULL) {
		gattlib_adapter_close(g_adapter);
		g_adapter = NULL;
	}

	exit(0);
}

/**
 * Callback: BLE device discovered during scan.
 */
static void on_device_discovered(gattlib_adapter_t *adapter, const char *addr,
                                 const char *name, void *user_data) {
	if (device_count >= MAX_DEVICES)
		return;

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
 * Callback: connection established (or failed).
 * Called from gattlib internally after gattlib_connect().
 */
static void on_device_connect(gattlib_adapter_t *adapter, const char *dst,
                               gattlib_connection_t *connection, int error,
                               void *user_data) {
	pthread_mutex_lock(&g_conn_mutex);
	g_connection = connection;
	g_conn_error = error;
	pthread_cond_signal(&g_conn_cond);
	pthread_mutex_unlock(&g_conn_mutex);
}

/**
 * Discover and print all services and characteristics.
 */
static void discover_services(gattlib_connection_t *conn) {
	gattlib_primary_service_t *services;
	gattlib_characteristic_t *chars;
	int svc_count, char_count;
	char uuid_str[MAX_LEN_UUID_STR + 1];

	printf("\n=== Services ===\n");
	int ret = gattlib_discover_primary(conn, &services, &svc_count);
	if (ret != GATTLIB_SUCCESS) {
		fprintf(stderr, "Error: Service discovery failed (err=%d)\n", ret);
		return;
	}

	for (int i = 0; i < svc_count; i++) {
		gattlib_uuid_to_string(&services[i].uuid, uuid_str, sizeof(uuid_str));
		printf("  service[%d] start:0x%04x end:0x%04x uuid:%s\n", i,
		       services[i].attr_handle_start,
		       services[i].attr_handle_end,
		       uuid_str);
	}
	free(services);

	printf("\n=== Characteristics ===\n");
	ret = gattlib_discover_char(conn, &chars, &char_count);
	if (ret != GATTLIB_SUCCESS) {
		fprintf(stderr, "Error: Characteristic discovery failed (err=%d)\n", ret);
		return;
	}

	for (int i = 0; i < char_count; i++) {
		gattlib_uuid_to_string(&chars[i].uuid, uuid_str, sizeof(uuid_str));
		printf("  char[%d] properties:%02x value_handle:0x%04x uuid:%s",
		       i, chars[i].properties, chars[i].value_handle, uuid_str);

		printf(" [");
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
 * Write CCCD value to a given handle.
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

/**
 * Main BLE task — runs inside gattlib_mainloop thread.
 * This is the pattern used by all gattlib examples.
 */
static void *ble_task(void *arg) {
	gattlib_adapter_t *adapter;
	int ret;

	signal(SIGINT, sigint_handler);

	/* Step 1: Open adapter */
	ret = gattlib_adapter_open(NULL, &adapter);
	if (ret != GATTLIB_SUCCESS) {
		fprintf(stderr, "Error: Failed to open adapter (err=%d)\n", ret);
		return NULL;
	}
	g_adapter = adapter;

	/* Step 2: Scan */
	printf("=== Scanning for BLE devices (%d seconds) ===\n", SCAN_TIMEOUT);
	ret = gattlib_adapter_scan_enable(adapter, on_device_discovered,
	                                   SCAN_TIMEOUT, NULL);
	if (ret != GATTLIB_SUCCESS) {
		fprintf(stderr, "Error: Scan failed (err=%d)\n", ret);
		gattlib_adapter_close(adapter);
		return NULL;
	}
	gattlib_adapter_scan_disable(adapter);

	printf("\nFound %d device(s).\n", device_count);
	if (device_count == 0) {
		fprintf(stderr, "No devices found.\n");
		gattlib_adapter_close(adapter);
		return NULL;
	}

	/* Step 3: Select device */
	int num;
	printf("\nEnter device number to connect: ");
	fflush(stdout);
	if (scanf("%d", &num) != 1 || num < 0 || num >= device_count) {
		fprintf(stderr, "Invalid selection.\n");
		gattlib_adapter_close(adapter);
		return NULL;
	}

	printf("\nConnecting to %s (%s)...\n",
	       discovered_addr[num], discovered_name[num]);

	/* Step 4: Connect (async) */
	ret = gattlib_connect(adapter, discovered_addr[num],
	                       GATTLIB_CONNECTION_OPTIONS_NONE,
	                       on_device_connect, NULL);
	if (ret != GATTLIB_SUCCESS) {
		fprintf(stderr, "Error: Connection request failed (err=%d)\n", ret);
		gattlib_adapter_close(adapter);
		return NULL;
	}

	/* Wait for connection callback */
	pthread_mutex_lock(&g_conn_mutex);
	while (g_conn_error == -1) {
		pthread_cond_wait(&g_conn_cond, &g_conn_mutex);
	}
	pthread_mutex_unlock(&g_conn_mutex);

	if (g_conn_error != 0 || g_connection == NULL) {
		fprintf(stderr, "Error: Connection failed (err=%d)\n", g_conn_error);
		gattlib_adapter_close(adapter);
		return NULL;
	}
	printf("Connected!\n");

	/* Step 5: Discover services and characteristics */
	discover_services(g_connection);

	/* Step 6: Get CCCD handle */
	unsigned int cccd_handle;
	printf("\nEnter CCCD handle (hex, e.g. 005D): ");
	fflush(stdout);
	if (scanf("%x", &cccd_handle) != 1) {
		fprintf(stderr, "Invalid handle.\n");
		goto EXIT;
	}

	/* Step 7: Write CCCD = 0x0002 (Enable Indication) */
	printf("\n=== Setting CCCD to 0x0002 (Enable Indication) ===\n");
	ret = write_cccd(g_connection, (uint16_t)cccd_handle, CCCD_INDICATE);
	if (ret != 0) {
		goto EXIT;
	}
	printf("\n=== CCCD set to 0x0002 successfully! ===\n");

	/* Step 8: Restore CCCD = 0x0000 (Disable) */
	printf("\nDisabling CCCD (setting to 0x0000)...\n");
	write_cccd(g_connection, (uint16_t)cccd_handle, CCCD_DISABLE);

EXIT:
	printf("\nDisconnecting...\n");
	gattlib_disconnect(g_connection, false);
	gattlib_adapter_close(adapter);
	printf("Done.\n");

	return NULL;
}

int main(int argc, char *argv[]) {
	int ret;

	ret = gattlib_mainloop(ble_task, NULL);
	if (ret != GATTLIB_SUCCESS) {
		fprintf(stderr, "Error: Failed to create gattlib mainloop (err=%d)\n", ret);
	}

	return ret;
}
