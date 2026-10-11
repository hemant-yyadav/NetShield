#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <pthread.h>
#include <sqlite3.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#define MAX_BYTES 8192
#define MAX_REQUEST_BYTES 65536
#define MAX_CONNECTIONS 100
#define NUM_THREADS 20
#define TIMEOUT 15

static int queue[MAX_CONNECTIONS], head, tail, count;
static pthread_mutex_t queue_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t queue_cond = PTHREAD_COND_INITIALIZER;
static const char *db_path;

typedef struct {
    char method[16], host[256], port[6], path[4096];
    const char *headers;
    size_t headers_len;
} HttpRequest;

static void timeout(int fd) {
    struct timeval tv = {.tv_sec = TIMEOUT, .tv_usec = 0};
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
}

static int send_all(int fd, const void *data, size_t length) {
    const char *p = data;
    while (length) {
        ssize_t written = send(fd, p, length, 0);
        if (written <= 0) return -1;
        p += written; length -= (size_t)written;
    }
    return 0;
}

static void error_response(int fd, int status, const char *reason) {
    char message[256];
    int length = snprintf(message, sizeof(message),
        "HTTP/1.1 %d %s\r\nConnection: close\r\nContent-Length: 0\r\n\r\n", status, reason);
    if (length > 0 && (size_t)length < sizeof(message)) send_all(fd, message, (size_t)length);
}

static void enqueue(int client) {
    pthread_mutex_lock(&queue_mutex);
    if (count == MAX_CONNECTIONS) { error_response(client, 503, "Service Unavailable"); close(client); }
    else { queue[tail] = client; tail = (tail + 1) % MAX_CONNECTIONS; count++; pthread_cond_signal(&queue_cond); }
    pthread_mutex_unlock(&queue_mutex);
}

static int dequeue(void) {
    pthread_mutex_lock(&queue_mutex);
    while (!count) pthread_cond_wait(&queue_cond, &queue_mutex);
    int client = queue[head]; head = (head + 1) % MAX_CONNECTIONS; count--;
    pthread_mutex_unlock(&queue_mutex);
    return client;
}

/* getaddrinfo is reentrant; gethostbyname was unsafe in the worker pool. */
static int connect_remote(const char *host, const char *port) {
    struct addrinfo hints = {0}, *addresses = NULL, *entry;
    hints.ai_family = AF_UNSPEC; hints.ai_socktype = SOCK_STREAM;
    if (getaddrinfo(host, port, &hints, &addresses) != 0) return -1;
    int remote = -1;
    for (entry = addresses; entry; entry = entry->ai_next) {
        int fd = socket(entry->ai_family, entry->ai_socktype, entry->ai_protocol);
        if (fd < 0) continue;
        int flags = fcntl(fd, F_GETFL, 0);
        if (flags < 0 || fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) { close(fd); continue; }
        int rc = connect(fd, entry->ai_addr, entry->ai_addrlen);
        if (rc < 0 && errno == EINPROGRESS) {
            fd_set writable; FD_ZERO(&writable); FD_SET(fd, &writable);
            struct timeval tv = {.tv_sec = TIMEOUT, .tv_usec = 0};
            rc = select(fd + 1, NULL, &writable, NULL, &tv);
            if (rc > 0) { int so_error = 0; socklen_t length = sizeof(so_error);
                rc = getsockopt(fd, SOL_SOCKET, SO_ERROR, &so_error, &length) || so_error ? -1 : 0; }
            else rc = -1;
        }
        if (!rc && !fcntl(fd, F_SETFL, flags)) { remote = fd; break; }
        close(fd);
    }
    freeaddrinfo(addresses);
    if (remote >= 0) timeout(remote);
    return remote;
}

static int blocked(sqlite3 *db, const char *domain) {
    if (!db || !domain || !*domain) return 0;
    const char *name = strncmp(domain, "www.", 4) ? domain : domain + 4;
    sqlite3_stmt *statement = NULL;
    if (sqlite3_prepare_v2(db, "SELECT 1 FROM threats WHERE (domain=? OR domain=?) AND is_active=1 LIMIT 1", -1, &statement, NULL) != SQLITE_OK) return 0;
    sqlite3_bind_text(statement, 1, name, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(statement, 2, domain, -1, SQLITE_TRANSIENT);
    int result = sqlite3_step(statement) == SQLITE_ROW;
    sqlite3_finalize(statement);
    return result;
}

static void log_request(sqlite3 *db, const char *domain, const char *method, int status, int request_size, int response_size, const char *ip, int is_blocked) {
    if (!db) return;
    sqlite3_stmt *statement = NULL;
    const char *sql = "INSERT INTO traffic_logs (domain,method,status_code,request_size,response_size,blocked,client_ip,timestamp) VALUES (?,?,?,?,?,?,?,CURRENT_TIMESTAMP)";
    if (sqlite3_prepare_v2(db, sql, -1, &statement, NULL) != SQLITE_OK) return;
    sqlite3_bind_text(statement, 1, domain ? domain : "Unknown", -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(statement, 2, method ? method : "Unknown", -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(statement, 3, status); sqlite3_bind_int(statement, 4, request_size);
    sqlite3_bind_int(statement, 5, response_size); sqlite3_bind_int(statement, 6, is_blocked);
    sqlite3_bind_text(statement, 7, ip, -1, SQLITE_TRANSIENT);
    if (sqlite3_step(statement) != SQLITE_DONE) fprintf(stderr, "[proxy] database log failed: %s\n", sqlite3_errmsg(db));
    sqlite3_finalize(statement);
}

static ssize_t receive_headers(int fd, char *buffer, size_t capacity) {
    size_t used = 0;
    while (used + 1 < capacity) {
        ssize_t got = recv(fd, buffer + used, capacity - used - 1, 0);
        if (got <= 0) return got;
        used += (size_t)got; buffer[used] = 0;
        if (strstr(buffer, "\r\n\r\n")) return (ssize_t)used;
    }
    return -1;
}

static int copy_string(char *to, size_t capacity, const char *from, size_t size) {
    if (!size || size >= capacity) return -1;
    memcpy(to, from, size); to[size] = 0; return 0;
}

static int parse_authority(const char *authority, char *host, size_t host_size, char *port, size_t port_size, const char *default_port) {
    const char *start = authority, *end, *port_start = NULL;
    if (*start == '[') { start++; end = strchr(start, ']'); if (!end || (end[1] && end[1] != ':')) return -1; if (end[1]) port_start = end + 2; }
    else { const char *colon = strrchr(start, ':'); if (colon && strchr(start, ':') == colon) { end = colon; port_start = colon + 1; } else end = start + strlen(start); }
    if (copy_string(host, host_size, start, (size_t)(end - start)) < 0) return -1;
    if (!port_start) return copy_string(port, port_size, default_port, strlen(default_port));
    if (!*port_start || strspn(port_start, "0123456789") != strlen(port_start) || strtoul(port_start, NULL, 10) > 65535) return -1;
    return copy_string(port, port_size, port_start, strlen(port_start));
}

static int parse_request(char *buffer, HttpRequest *request) {
    char *line_end = strstr(buffer, "\r\n"), *headers_end = strstr(buffer, "\r\n\r\n");
    char target[4096], version[16];
    if (!line_end || !headers_end || line_end >= headers_end) return -1;
    *line_end = 0;
    if (sscanf(buffer, "%15s %4095s %15s", request->method, target, version) != 3 || (strcmp(version, "HTTP/1.0") && strcmp(version, "HTTP/1.1"))) return -1;
    if (strcmp(request->method, "GET") && strcmp(request->method, "HEAD")) return -2;
    if (!strncmp(target, "http://", 7)) {
        char *authority = target + 7, *path = strchr(authority, '/');
        if (path) { *path++ = 0; if (snprintf(request->path, sizeof(request->path), "/%s", path) >= (int)sizeof(request->path)) return -1; }
        else strcpy(request->path, "/");
        if (parse_authority(authority, request->host, sizeof(request->host), request->port, sizeof(request->port), "80") < 0) return -1;
    } else if (target[0] == '/') {
        char authority[256], *host = NULL;
        for (char *header = line_end + 2; header < headers_end;) {
            char *next = strstr(header, "\r\n"); if (!next) return -1;
            if (!strncasecmp(header, "Host:", 5)) {
                host = header + 5; while (*host == ' ' || *host == '\t') host++;
                if (copy_string(authority, sizeof(authority), host, (size_t)(next - host)) < 0) return -1;
                break;
            }
            header = next + 2;
        }
        if (!host || parse_authority(authority, request->host, sizeof(request->host), request->port, sizeof(request->port), "80") < 0 || copy_string(request->path, sizeof(request->path), target, strlen(target)) < 0) return -1;
    } else return -1;
    request->headers = line_end + 2; request->headers_len = (size_t)(headers_end - request->headers) + 2;
    return 0;
}

static int forward_request(int remote, const HttpRequest *request) {
    char line[8192];
    int length = snprintf(line, sizeof(line), "%s %s HTTP/1.1\r\n", request->method, request->path);
    if (length < 0 || (size_t)length >= sizeof(line) || send_all(remote, line, (size_t)length) < 0) return -1;
    int have_host = 0; const char *cursor = request->headers, *end = cursor + request->headers_len - 2;
    while (cursor < end) {
        const char *line_end = strstr(cursor, "\r\n"); if (!line_end || line_end > end) return -1;
        size_t line_length = (size_t)(line_end - cursor);
        if (strncasecmp(cursor, "Connection:", 11) && strncasecmp(cursor, "Proxy-Connection:", 17)) {
            if (!strncasecmp(cursor, "Host:", 5)) have_host = 1;
            if (send_all(remote, cursor, line_length) || send_all(remote, "\r\n", 2)) return -1;
        }
        cursor = line_end + 2;
    }
    if (!have_host) {
        length = snprintf(line, sizeof(line), strcmp(request->port, "80") ? "Host: %s:%s\r\n" : "Host: %s\r\n", request->host, request->port);
        if (length < 0 || (size_t)length >= sizeof(line) || send_all(remote, line, (size_t)length)) return -1;
    }
    return send_all(remote, "Connection: close\r\n\r\n", 21);
}

static int relay(int source, int destination) {
    char buffer[MAX_BYTES]; int total = 0;
    for (;;) { ssize_t got = recv(source, buffer, sizeof(buffer), 0);
        if (!got) return total;
        if (got < 0) return total ? total : -1;
        if (send_all(destination, buffer, (size_t)got)) return -1;
        total += (int)got;
    }
}

static void handle_connect(int client, sqlite3 *db, const char *ip, char *buffer, int size) {
    char authority[256], host[256], port[6];
    if (sscanf(buffer, "CONNECT %255[^ ]", authority) != 1 || parse_authority(authority, host, sizeof(host), port, sizeof(port), "443") < 0) { error_response(client, 400, "Bad Request"); return; }
    if (blocked(db, host)) { error_response(client, 403, "Forbidden"); log_request(db, host, "CONNECT", 403, size, 0, ip, 1); return; }
    int remote = connect_remote(host, port);
    if (remote < 0) { error_response(client, 502, "Bad Gateway"); log_request(db, host, "CONNECT", 502, size, 0, ip, 0); return; }
    if (!send_all(client, "HTTP/1.1 200 Connection Established\r\n\r\n", 39)) {
        fd_set set; char bytes[MAX_BYTES]; int maxfd = client > remote ? client : remote;
        for (;;) { FD_ZERO(&set); FD_SET(client, &set); FD_SET(remote, &set); struct timeval tv = {.tv_sec = TIMEOUT};
            if (select(maxfd + 1, &set, NULL, NULL, &tv) <= 0) break;
            if (FD_ISSET(client, &set)) { ssize_t got = recv(client, bytes, sizeof(bytes), 0); if (got <= 0 || send_all(remote, bytes, (size_t)got)) break; }
            if (FD_ISSET(remote, &set)) { ssize_t got = recv(remote, bytes, sizeof(bytes), 0); if (got <= 0 || send_all(client, bytes, (size_t)got)) break; }
        }
        log_request(db, host, "CONNECT", 200, size, 0, ip, 0);
    }
    close(remote);
}

static void *worker(void *unused) {
    (void)unused; sqlite3 *db = NULL;
    if (sqlite3_open_v2(db_path, &db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, NULL) == SQLITE_OK) sqlite3_busy_timeout(db, 5000);
    else { if (db) sqlite3_close(db); db = NULL; }
    for (;;) {
        int client = dequeue(); timeout(client);
        struct sockaddr_in address; socklen_t address_size = sizeof(address); char ip[INET_ADDRSTRLEN] = "0.0.0.0";
        if (!getpeername(client, (struct sockaddr *)&address, &address_size)) inet_ntop(AF_INET, &address.sin_addr, ip, sizeof(ip));
        char buffer[MAX_REQUEST_BYTES]; ssize_t size = receive_headers(client, buffer, sizeof(buffer));
        if (size > 0) {
            fprintf(stderr, "[proxy] accepted %zd bytes from %s\n", size, ip);
            if (!strncmp(buffer, "CONNECT ", 8)) handle_connect(client, db, ip, buffer, (int)size);
            else {
                HttpRequest request = {0}; int parsed = parse_request(buffer, &request);
                if (parsed == -2) error_response(client, 405, "Method Not Allowed");
                else if (parsed < 0) error_response(client, 400, "Bad Request");
                else if (blocked(db, request.host)) { error_response(client, 403, "Forbidden"); log_request(db, request.host, request.method, 403, (int)size, 0, ip, 1); }
                else {
                    fprintf(stderr, "[proxy] %s %s:%s%s\n", request.method, request.host, request.port, request.path);
                    int remote = connect_remote(request.host, request.port);
                    if (remote < 0) { error_response(client, 502, "Bad Gateway"); log_request(db, request.host, request.method, 502, (int)size, 0, ip, 0); }
                    else { int response = forward_request(remote, &request) ? -1 : relay(remote, client);
                        if (response < 0) { error_response(client, 502, "Bad Gateway"); response = 0; }
                        fprintf(stderr, "[proxy] upstream response: %d bytes\n", response);
                        log_request(db, request.host, request.method, response ? 200 : 502, (int)size, response, ip, 0); close(remote); }
                }
            }
        }
        close(client);
    }
    return NULL;
}

int main(void) {
    const char *port_text = getenv("PROXY_PORT"); int port = port_text ? atoi(port_text) : 8888;
    db_path = getenv("DB_PATH"); if (!db_path) db_path = "/data/netshield.db";
    if (port < 1 || port > 65535) { fputs("Invalid PROXY_PORT\n", stderr); return 1; }
    pthread_t threads[NUM_THREADS];
    for (int i = 0; i < NUM_THREADS; i++) if (pthread_create(&threads[i], NULL, worker, NULL)) { perror("pthread_create"); return 1; }
    int listener = socket(AF_INET, SOCK_STREAM, 0), reuse = 1;
    if (listener < 0) { perror("socket"); return 1; }
    setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
    struct sockaddr_in address = {.sin_family = AF_INET, .sin_port = htons((unsigned short)port), .sin_addr.s_addr = INADDR_ANY};
    if (bind(listener, (struct sockaddr *)&address, sizeof(address)) || listen(listener, 500)) { perror("bind/listen"); close(listener); return 1; }
    fprintf(stderr, "NetShield Phase 9 [INTELLIGENCE] Proxy Server running on port %d with Live Blocklists!\n", port);
    for (;;) { int client = accept(listener, NULL, NULL); if (client >= 0) enqueue(client); else if (errno != EINTR) perror("accept"); }
}
