#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <string.h>
#include <time.h>
#include <errno.h>
#include <fcntl.h>
#include <grp.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <poll.h>
#include <hidapi/hidapi.h>
#include "demux.h"
#include "fsk.h"

#define VENDOR_ID  0x16d0
#define PRODUCT_ID 0x0787

#define PACKET_SIZE 64 // Device sends exactly 64 bytes, no Report ID
#define SOCKET_PATH "/run/cidv6d.sock"
#define SOCKET_GROUP "cidv6d"
#define MAX_CLIENTS 32

volatile int keep_running = 1;

// Handle Ctrl+C gracefully to close the USB interface
void sigint_handler(int dummy) {
    keep_running = 0;
}

// Unix socket client tracking
static int socket_fd = -1;
static int client_fds[MAX_CLIENTS];
static int num_clients = 0;

// Initialize Unix domain socket
static int init_socket(void) {
    struct sockaddr_un addr;
    int fd;

    // Unlink existing socket file if present
    unlink(SOCKET_PATH);

    fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) {
        perror("socket");
        return -1;
    }

    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, SOCKET_PATH, sizeof(addr.sun_path) - 1);

    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind");
        close(fd);
        return -1;
    }

    // Set socket permissions: 0660 (owner and group can read/write)
    chmod(SOCKET_PATH, 0660);

    // Set socket group ownership to "cidv6d"
    struct group *grp = getgrnam(SOCKET_GROUP);
    if (grp) {
        if (chown(SOCKET_PATH, -1, grp->gr_gid) < 0) {
            perror("chown");
            // Continue anyway, group might not exist yet
        }
    } else {
        // Group doesn't exist, will be created by postinst
        fprintf(stderr, "[!] Warning: Group '" SOCKET_GROUP "' not found. Socket will not be group-writable.\n");
    }

    if (listen(fd, SOMAXCONN) < 0) {
        perror("listen");
        close(fd);
        return -1;
    }

    // Make socket non-blocking
    int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);

    return fd;
}

// Add a new client
static void add_client(int fd) {
    if (num_clients >= MAX_CLIENTS) {
        close(fd);
        return;
    }

    // Make client socket non-blocking
    int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);

    client_fds[num_clients++] = fd;
}

// Remove a client by index
static void remove_client(int index) {
    if (index < 0 || index >= num_clients) return;

    close(client_fds[index]);
    // Shift remaining clients down
    for (int i = index; i < num_clients - 1; i++) {
        client_fds[i] = client_fds[i + 1];
    }
    num_clients--;
}

// Broadcast JSON to all connected clients
static void broadcast_json(const char *json) {
    size_t len = strlen(json);
    int i = 0;

    while (i < num_clients) {
        ssize_t sent = send(client_fds[i], json, len, MSG_NOSIGNAL);
        if (sent < 0) {
            // Error sending, remove client
            remove_client(i);
            // Don't increment i, as we shifted elements
        } else if (sent < len) {
            // Partial send - buffer and retry?
            // For simplicity, we'll just remove on any error
            // In production, you'd want proper buffering
            remove_client(i);
        } else {
            i++;
        }
    }
}

// Cleanup socket and all clients
static void cleanup_socket(void) {
    for (int i = 0; i < num_clients; i++) {
        close(client_fds[i]);
    }
    num_clients = 0;

    if (socket_fd >= 0) {
        close(socket_fd);
        socket_fd = -1;
    }

    unlink(SOCKET_PATH);
}

// Debug: Print first N bytes of a packet in hex
static void dump_packet(const unsigned char *buf, int len, const char *label) {
    printf("[DEBUG %s] len=%d: ", label, len);
    int dump_len = len < 16 ? len : 16;
    for (int i = 0; i < dump_len; i++) {
        printf("%02x ", buf[i]);
    }
    if (len > 16) printf("...");
    printf("\n");
}

// Print usage
static void print_usage(const char *progname) {
    printf("Usage: %s [OPTIONS]\n", progname);
    printf("Options:\n");
    printf("  -j, --json      Pure JSON output mode (no other messages)\n");
    printf("  -d, --debug     Enable debug output (packet dumps, counters)\n");
    printf("  -r, --raw       Also save raw audio to /tmp/cid_line[1-4].raw\n");
    printf("  -h, --help      Show this help message\n");
}

// Emit JSON event to stdout and all socket clients
static void emit_cid_event(const caller_id_t *cid) {
    char timestamp[64];
    struct tm *tm_info = localtime(&cid->timestamp);
    strftime(timestamp, sizeof(timestamp), "%Y-%m-%dT%H:%M:%S%z", tm_info);

    char json[256];
    snprintf(json, sizeof(json), "{\"timestamp\":\"%s\",\"line\":%d,\"number\":\"%s\",\"name\":\"%s\"}\n",
             timestamp,
             cid->line_number,
             cid->phone_number[0] ? cid->phone_number : "unknown",
             cid->name[0] ? cid->name : "");

    // Write to stdout
    printf("%s", json);
    fflush(stdout);

    // Broadcast to all connected socket clients
    broadcast_json(json);
}

int main(int argc, char* argv[]) {
    hid_device *handle;
    unsigned char buf[PACKET_SIZE];
    uint8_t line_audio[4][SAMPLES_PER_PACKET];  // Audio for all 4 lines
    int res;
    int debug_mode = 0;
    int raw_mode = 0;
    int json_mode = 0;
    fsk_state_t *fsk[4] = {NULL, NULL, NULL, NULL};  // 4 FSK demodulators
    FILE *audio_out[4] = {NULL, NULL, NULL, NULL};    // 4 raw audio files

    // Parse command-line arguments
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-d") == 0 || strcmp(argv[i], "--debug") == 0) {
            debug_mode = 1;
        } else if (strcmp(argv[i], "-r") == 0 || strcmp(argv[i], "--raw") == 0) {
            raw_mode = 1;
        } else if (strcmp(argv[i], "-j") == 0 || strcmp(argv[i], "--json") == 0) {
            json_mode = 1;
        } else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            print_usage(argv[0]);
            return 0;
        } else {
            fprintf(stderr, "Unknown option: %s\n", argv[i]);
            print_usage(argv[0]);
            return 1;
        }
    }

    // Trap Ctrl+C
    signal(SIGINT, sigint_handler);

    if (hid_init() != 0) {
        fprintf(stderr, "Failed to initialize hidapi\n");
        return -1;
    }

    // Open the CID V6 Device
    handle = hid_open(VENDOR_ID, PRODUCT_ID, NULL);
    if (!handle) {
        if (!json_mode) fprintf(stderr, "Unable to open CID V6. Are you running with sudo? Check VID/PID.\n");
        hid_exit();
        return -1;
    }

    if (!json_mode) printf("[+] Sistemler CID V6 Connected.\n");

    // Set non-blocking mode so we can detect silence vs actual data
    if (hid_set_nonblocking(handle, 1) != 0) {
        if (!json_mode) fprintf(stderr, "[!] Warning: Could not set non-blocking mode.\n");
    } else if (debug_mode && !json_mode) {
        printf("[+] Non-blocking mode enabled.\n");
    }

    // Try to read device info
    if (!json_mode) {
        wchar_t wstr[256];
        if (hid_get_manufacturer_string(handle, wstr, sizeof(wstr)/sizeof(wstr[0])) == 0) {
            printf("[+] Manufacturer: %ls\n", wstr);
        }
        if (hid_get_product_string(handle, wstr, sizeof(wstr)/sizeof(wstr[0])) == 0) {
            printf("[+] Product: %ls\n", wstr);
        }
    }

    // Initialize FSK demodulators for all 4 lines
    for (int i = 0; i < 4; i++) {
        fsk[i] = fsk_init(i + 1, json_mode);  // Lines are 1-indexed
        if (!fsk[i]) {
            if (!json_mode) fprintf(stderr, "[!] Warning: FSK demodulator for Line %d failed to initialize.\n", i + 1);
        }
    }

    // Open raw audio files if requested
    if (raw_mode) {
        for (int i = 0; i < 4; i++) {
            char raw_path[64];
            snprintf(raw_path, sizeof(raw_path), "/tmp/cid_line%d.raw", i + 1);
            audio_out[i] = fopen(raw_path, "wb");
            if (!audio_out[i]) {
                if (!json_mode) fprintf(stderr, "[!] Warning: Failed to open %s\n", raw_path);
            } else if (!json_mode) {
                printf("[+] Streaming Line %d audio to %s\n", i + 1, raw_path);
            }
        }
    }

    // Initialize Unix domain socket
    socket_fd = init_socket();
    if (socket_fd < 0) {
        if (!json_mode) fprintf(stderr, "[!] Warning: Failed to create socket. Socket disabled.\n");
    } else if (!json_mode) {
        printf("[+] Socket listening on " SOCKET_PATH "\n");
    }

    if (!json_mode) {
        printf("[+] Listening for calls on all 4 lines. Press Ctrl+C to stop.\n");
        if (debug_mode) {
            printf("[DEBUG] Waiting for first packet...\n");
        }
        printf("\n");
    }

    int packet_count = 0;
    int silent_reads = 0;
    const int REPORT_INTERVAL = 250; // Print status every 250 packets (~1 second at 250Hz)

    // The Endless Hardware Polling Loop with poll()
    while (keep_running) {
        struct pollfd pfds[MAX_CLIENTS + 1];
        int nfds = 0;

        // Always check the HID device by polling it
        // (hidapi doesn't support poll, so we poll with timeout)
        // Then check socket after each poll cycle

        // Try HID read first
        res = hid_read(handle, buf, sizeof(buf));

        if (res < 0) {
            fprintf(stderr, "[-] Device disconnected or read error.\n");
            break;
        }

        if (res > 0) {
            // We got data!
            silent_reads = 0;
            packet_count++;

            // Debug: Print first packet to inspect structure
            if (debug_mode && packet_count == 1 && !json_mode) {
                dump_packet(buf, res, "FIRST PACKET");
                if (res == 64) {
                    printf("[DEBUG] Packet size: 64 bytes (no Report ID)\n");
                } else {
                    printf("[DEBUG] Packet size: %d bytes (unexpected)\n", res);
                }
            }

            // Progress indicator every second (only in debug mode)
            if (debug_mode && packet_count % REPORT_INTERVAL == 0 && !json_mode) {
                printf("[DEBUG] Received %d packets (~%d seconds of audio)\n", packet_count, packet_count / 250);
            }

            // Device sends exactly 64 bytes with no Report ID
            // buf contains the payload directly
            // Decode all 4 lines and feed to their FSK demodulators
            for (int line = 0; line < 4; line++) {
                demux_line(buf, line_audio[line], line + 1);

                // Feed to FSK demodulator if available
                if (fsk[line]) {
                    if (fsk_feed_audio(fsk[line], line_audio[line], SAMPLES_PER_PACKET)) {
                        // CID data received!
                        const caller_id_t *cid = fsk_get_cid_data(fsk[line]);
                        if (cid && cid->has_data) {
                            emit_cid_event(cid);
                        }
                    }
                }

                // Write raw audio if requested
                if (audio_out[line]) {
                    fwrite(line_audio[line], 1, SAMPLES_PER_PACKET, audio_out[line]);
                }
            }
        } else {
            // No HID data this cycle
            silent_reads++;
            // Print status every ~4 seconds if nothing received (only in debug mode)
            if (debug_mode && silent_reads == 1000 && !json_mode) {
                printf("[DEBUG] Still waiting for data... (no packets for ~4 sec)\n");
                silent_reads = 0;
            }
        }

        // Handle socket events (new connections and client activity)
        if (socket_fd >= 0) {
            // Build pollfd array
            pfds[0].fd = socket_fd;
            pfds[0].events = POLLIN;
            pfds[0].revents = 0;
            nfds = 1;

            // Add client fds
            for (int i = 0; i < num_clients && nfds <= MAX_CLIENTS; i++) {
                pfds[nfds].fd = client_fds[i];
                pfds[nfds].events = POLLIN | POLLHUP | POLLERR;
                pfds[nfds].revents = 0;
                nfds++;
            }

            // Poll with 0 timeout (non-blocking check)
            int poll_ret = poll(pfds, nfds, 0);

            if (poll_ret > 0) {
                // Check for new connection on listening socket
                if (pfds[0].revents & POLLIN) {
                    int client_fd = accept(socket_fd, NULL, NULL);
                    if (client_fd >= 0) {
                        add_client(client_fd);
                        if (debug_mode && !json_mode) {
                            printf("[DEBUG] Client connected (total: %d)\n", num_clients);
                        }
                    }
                }

                // Check existing clients for disconnects
                for (int i = 1; i < nfds; i++) {
                    if (pfds[i].revents & (POLLHUP | POLLERR)) {
                        // Client disconnected
                        if (debug_mode && !json_mode) {
                            printf("[DEBUG] Client disconnected (fd=%d)\n", pfds[i].fd);
                        }
                        // Find and remove this client
                        for (int j = 0; j < num_clients; j++) {
                            if (client_fds[j] == pfds[i].fd) {
                                remove_client(j);
                                break;
                            }
                        }
                        // Adjust index since we removed a client
                        i--;
                    } else if (pfds[i].revents & POLLIN) {
                        // Client sent data - we don't expect any, just drain it
                        char drain[256];
                        recv(pfds[i].fd, drain, sizeof(drain), 0);
                    }
                }
            }
        }
    }

    if (!json_mode) printf("\n[+] Shutting down gracefully...\n");

    // Close all audio files
    for (int i = 0; i < 4; i++) {
        if (audio_out[i]) {
            fclose(audio_out[i]);
        }
    }

    // Cleanup all FSK demodulators
    for (int i = 0; i < 4; i++) {
        if (fsk[i]) {
            fsk_cleanup(fsk[i]);
        }
    }

    // Cleanup socket and all clients
    cleanup_socket();

    hid_close(handle);
    hid_exit();

    return 0;
}
