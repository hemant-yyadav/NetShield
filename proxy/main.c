#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <sqlite3.h>
#include <pthread.h>
#include <sys/time.h>
#include <sys/select.h>
#include "proxy_parse.h"

#define MAX_BYTES 8192
#define MAX_CONNECTIONS 100 // Pool queue boundary
#define NUM_THREADS 20       // Concurrent worker pool

// Thread Pool Queue
int client_queue[MAX_CONNECTIONS];
int queue_head = 0;
int queue_tail = 0;
int queue_count = 0;

pthread_mutex_t queue_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t queue_cond = PTHREAD_COND_INITIALIZER;

char *db_path_global;

void set_socket_timeout(int sock, int seconds) {
    struct timeval tv;
    tv.tv_sec = seconds;
    tv.tv_usec = 0;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof tv);
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, (const char*)&tv, sizeof tv);
}

void enqueue(int client_socket) {
    pthread_mutex_lock(&queue_mutex);
    if (queue_count < MAX_CONNECTIONS) {
        client_queue[queue_tail] = client_socket;
        queue_tail = (queue_tail + 1) % MAX_CONNECTIONS;
        queue_count++;
        pthread_cond_signal(&queue_cond);
    } else {
        close(client_socket); // Reject if pool is fundamentally saturated
    }
    pthread_mutex_unlock(&queue_mutex);
}

int dequeue() {
    pthread_mutex_lock(&queue_mutex);
    while (queue_count == 0) {
        pthread_cond_wait(&queue_cond, &queue_mutex);
    }
    int client_socket = client_queue[queue_head];
    queue_head = (queue_head + 1) % MAX_CONNECTIONS;
    queue_count--;
    pthread_mutex_unlock(&queue_mutex);
    return client_socket;
}

int connectRemoteServer(char *host_addr, int port_num) {
    int remoteSocket = socket(AF_INET, SOCK_STREAM, 0);
    if (remoteSocket < 0) return -1;
    struct hostent *host = gethostbyname(host_addr);
    if (host == NULL) return -1;
    struct sockaddr_in server_addr;
    bzero((char *)&server_addr, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port_num);
    bcopy((char *)host->h_addr, (char *)&server_addr.sin_addr.s_addr, host->h_length);
    if (connect(remoteSocket, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) return -1;
    return remoteSocket;
}

int is_domain_blocked(sqlite3 *db, const char *domain) {
    if (!db || !domain) return 0;
    
    // Check purely for sub-domain anomalies seamlessly
    const char *target = domain;
    if (strncmp(target, "www.", 4) == 0) target += 4;
    
    const char *sql = "SELECT id FROM threats WHERE (domain = ? OR domain = ?) AND is_active = 1 LIMIT 1;";
    sqlite3_stmt *stmt;
    int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) return 0;
    
    sqlite3_bind_text(stmt, 1, target, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, domain, -1, SQLITE_STATIC);
    
    int blocked = 0;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        blocked = 1;
    }
    sqlite3_finalize(stmt);
    return blocked;
}

void log_to_db(sqlite3 *db, const char *domain, const char *method, int status_code, 
               int req_size, int res_size, const char *client_ip, int blocked) {
    char *err_msg = 0;
    char sql[2048];
    const char *d = domain ? domain : "Unknown";
    const char *m = method ? method : "Unknown";
    const char *ip = client_ip ? client_ip : "0.0.0.0";
    
    snprintf(sql, sizeof(sql), 
             "INSERT INTO traffic_logs (domain, method, status_code, request_size, response_size, blocked, client_ip, timestamp) "
             "VALUES ('%s', '%s', %d, %d, %d, %d, '%s', CURRENT_TIMESTAMP);", d, m, status_code, req_size, res_size, blocked, ip);

    int rc = sqlite3_exec(db, sql, 0, 0, &err_msg);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "SQL error: %s\n", err_msg);
        sqlite3_free(err_msg);
    }
}

void handle_https(int client_sock, int remote_sock) {
    char *response = "HTTP/1.1 200 Connection Established\r\n\r\n";
    send(client_sock, response, strlen(response), 0);

    fd_set fdset;
    int maxfd = (client_sock > remote_sock) ? client_sock : remote_sock;
    char buffer[MAX_BYTES];

    while (1) {
        FD_ZERO(&fdset);
        FD_SET(client_sock, &fdset);
        FD_SET(remote_sock, &fdset);

        struct timeval tv;
        tv.tv_sec = 30; 
        tv.tv_usec = 0;

        int ready = select(maxfd + 1, &fdset, NULL, NULL, &tv);
        if (ready <= 0) break; 

        if (FD_ISSET(client_sock, &fdset)) {
            int bytes = recv(client_sock, buffer, sizeof(buffer), 0);
            if (bytes <= 0) break;
            if (send(remote_sock, buffer, bytes, 0) <= 0) break;
        }

        if (FD_ISSET(remote_sock, &fdset)) {
            int bytes = recv(remote_sock, buffer, sizeof(buffer), 0);
            if (bytes <= 0) break;
            if (send(client_sock, buffer, bytes, 0) <= 0) break;
        }
    }
}

void* worker_thread(void* arg) {
    (void)arg; 
    
    sqlite3 *db;
    int rc = sqlite3_open(db_path_global, &db);
    if (rc == SQLITE_OK) {
        sqlite3_busy_timeout(db, 5000); 
    }

    while (1) {
        int client_sock = dequeue();
        set_socket_timeout(client_sock, 30); 
        
        struct sockaddr_in addr;
        socklen_t addr_size = sizeof(struct sockaddr_in);
        int res = getpeername(client_sock, (struct sockaddr *)&addr, &addr_size);
        char client_ip[INET_ADDRSTRLEN] = "0.0.0.0";
        if (res == 0) inet_ntop(AF_INET, &addr.sin_addr, client_ip, INET_ADDRSTRLEN);

        char buffer[MAX_BYTES];
        bzero(buffer, MAX_BYTES);
        int bytes_recv = recv(client_sock, buffer, MAX_BYTES - 1, 0);

        if (bytes_recv > 0) {
            if (strncmp(buffer, "CONNECT", 7) == 0) {
                char host[256];
                int port = 443;
                bzero(host, 256);
                
                if (sscanf(buffer, "CONNECT %255[^:]:%d", host, &port) != 2) {
                    sscanf(buffer, "CONNECT %255s", host);
                }
                char *space = strchr(host, ' ');
                if (space) *space = '\0';

                // Real-time Intelligence Query
                int is_blocked = db ? is_domain_blocked(db, host) : 0;
                
                if (is_blocked) {
                    // Blocked! Immediately sever connection and log violation symmetrically 
                    char *err = "HTTP/1.1 403 Forbidden\r\n\r\n";
                    send(client_sock, err, strlen(err), 0);
                    if (db) log_to_db(db, host, "CONNECT", 403, bytes_recv, strlen(err), client_ip, 1);
                } else {
                    int remote_sock = connectRemoteServer(host, port);
                    if (remote_sock >= 0) {
                        set_socket_timeout(remote_sock, 30);
                        handle_https(client_sock, remote_sock);
                        close(remote_sock);
                        if (db) log_to_db(db, host, "CONNECT", 200, bytes_recv, 0, client_ip, 0);
                    } else {
                        char *err = "HTTP/1.1 502 Bad Gateway\r\n\r\n";
                        send(client_sock, err, strlen(err), 0);
                        if (db) log_to_db(db, host, "CONNECT", 502, bytes_recv, 0, client_ip, 0);
                    }
                }
            } else { 
                struct ParsedRequest *req = ParsedRequest_create();
                if (ParsedRequest_parse(req, buffer, bytes_recv) == 0) {
                    ParsedHeader_set(req, "Connection", "close");
                    if (ParsedHeader_get(req, "Host") == NULL && req->host != NULL) {
                        ParsedHeader_set(req, "Host", req->host);
                    }

                    int is_blocked = db ? is_domain_blocked(db, req->host) : 0;
                    
                    if (is_blocked) {
                        char *err = "HTTP/1.1 403 Forbidden\r\n\r\n";
                        send(client_sock, err, strlen(err), 0);
                        if (db) log_to_db(db, req->host, req->method, 403, bytes_recv, strlen(err), client_ip, 1);
                    } else {
                        int unparse_len = ParsedRequest_totalLen(req);
                        char *forward_buf = (char *)malloc(unparse_len + 1);
                        if (ParsedRequest_unparse(req, forward_buf, unparse_len) >= 0) {
                            forward_buf[unparse_len] = '\0';

                            int server_port = req->port ? atoi(req->port) : 80;
                            int remote_sock = connectRemoteServer(req->host, server_port);

                            if (remote_sock >= 0) {
                                set_socket_timeout(remote_sock, 30);
                                send(remote_sock, forward_buf, unparse_len, 0);

                                int response_size = 0;
                                int status_code = 200;
                                char resp_buffer[MAX_BYTES];
                                bzero(resp_buffer, MAX_BYTES);
                                
                                int b_recv = recv(remote_sock, resp_buffer, MAX_BYTES - 1, 0);

                                int should_intercept = 0;
                                char intercept_filename[512] = "unknown_payload.bin";

                                if (b_recv > 0) {
                                    sscanf(resp_buffer, "HTTP/%*s %d", &status_code);

                                    // Phase 3, Step 5: Proxy Header Diagnostics
                                    char *headers_end = strstr(resp_buffer, "\r\n\r\n");
                                    if (headers_end) {
                                        *headers_end = '\0'; // Temporarily null-terminate to isolate headers

                                        char *ctype = strstr(resp_buffer, "Content-Type:");
                                        char *cdisp = strstr(resp_buffer, "Content-Disposition:");

                                        // Diagnostic A: Attachment Extraction
                                        if (cdisp && strstr(cdisp, "attachment")) {
                                            should_intercept = 1;
                                            char *fname_ptr = strstr(cdisp, "filename=");
                                            if (fname_ptr) {
                                                fname_ptr += 9;
                                                if (*fname_ptr == '"') fname_ptr++;
                                                char *fname_end = strchr(fname_ptr, '"');
                                                if (!fname_end) fname_end = strchr(fname_ptr, '\r');
                                                if (fname_end && (fname_end - fname_ptr) < sizeof(intercept_filename)) {
                                                    strncpy(intercept_filename, fname_ptr, fname_end - fname_ptr);
                                                    intercept_filename[fname_end - fname_ptr] = '\0';
                                                }
                                            }
                                        } 
                                        // Diagnostic B: Raw MIME type boundaries
                                        else if (ctype && (strstr(ctype, "application/octet-stream") || 
                                                           strstr(ctype, "application/x-msdownload") || 
                                                           strstr(ctype, "application/zip"))) {
                                            should_intercept = 1;
                                        }

                                        *headers_end = '\r'; // Restore stream purity

                                        if (should_intercept) {
                                            printf("\n[NETSHIELD MITM] ⚠️ Intercept Triggered. Targeting File: %s\n", intercept_filename);
                                        }
                                    }
                                }

                                int pipefd[2];
                                pid_t intercept_pid = -1;

                                if (should_intercept) {
                                    if (pipe(pipefd) < 0) {
                                        perror("Pipe creation failed");
                                    } else {
                                        intercept_pid = fork();
                                        if (intercept_pid == 0) {
                                            // Secure Child Pipeline Process
                                            close(pipefd[1]); // Close write end
                                            dup2(pipefd[0], STDIN_FILENO); // STDIN directly mapped to pipe!
                                            
                                            // Step 7: Stream directly cleanly through cross-container network API endpoints natively!
                                            char curl_cmd[1024];
                                            snprintf(curl_cmd, sizeof(curl_cmd), 
                                                "curl -X POST http://api:8000/api/vault/share/generate "
                                                "-F 'file=@-;filename=\"%s\"' "
                                                "-F 'expiry_hours=24'", intercept_filename);
                                            
                                            execlp("sh", "sh", "-c", curl_cmd, NULL);
                                            exit(1);
                                        }
                                        close(pipefd[0]); // Parent exclusively writes
                                    }
                                }

                                // Initial First Block Router (Split Body from Headers)
                                char *body_offset = NULL;
                                int bytes_to_push = b_recv;
                                
                                if (should_intercept) {
                                    body_offset = strstr(resp_buffer, "\r\n\r\n");
                                    if (body_offset) {
                                        body_offset += 4;
                                        bytes_to_push = b_recv - (body_offset - resp_buffer);
                                    }
                                }

                                if (bytes_to_push > 0) {
                                    if (should_intercept && intercept_pid > 0) {
                                        if (body_offset) write(pipefd[1], body_offset, bytes_to_push);
                                    } else {
                                        send(client_sock, resp_buffer, b_recv, 0); // Passthrough unmodified completely
                                    }
                                }

                                // Passive Secondary Continuous Block Receiver
                                bzero(resp_buffer, MAX_BYTES);
                                b_recv = recv(remote_sock, resp_buffer, MAX_BYTES - 1, 0);

                                while (b_recv > 0) {
                                    if (should_intercept && intercept_pid > 0) {
                                        // Massive pure throughput push straight onto OS Pipeline
                                        write(pipefd[1], resp_buffer, b_recv);
                                    } else {
                                        send(client_sock, resp_buffer, b_recv, 0);
                                    }
                                    
                                    response_size += b_recv;
                                    bzero(resp_buffer, MAX_BYTES);
                                    b_recv = recv(remote_sock, resp_buffer, MAX_BYTES - 1, 0);
                                }

                                if (should_intercept) {
                                    if (intercept_pid > 0) {
                                        close(pipefd[1]); // Flushes STDIN EOF down to child securely
                                    }
                                    
                                    char fallbk[1024];
                                    snprintf(fallbk, sizeof(fallbk),
                                        "HTTP/1.1 200 OK\r\n"
                                        "Content-Type: text/html\r\n"
                                        "Connection: close\r\n\r\n"
                                        "<html><body style='background:#0f1117;color:#6366f1;font-family:sans-serif;text-align:center;padding-top:100px;'>"
                                        "<h1>NetShield Vault Security</h1>"
                                        "<p><b>%s</b> was actively intercepted and natively isolated inside your Secure Share Vault.</p>"
                                        "</body></html>", intercept_filename);
                                    send(client_sock, fallbk, strlen(fallbk), 0);
                                }

                                if (db) log_to_db(db, req->host, req->method, status_code, bytes_recv, response_size, client_ip, should_intercept ? 1 : 0);
                                close(remote_sock);
                            } else {
                                char *err = "HTTP/1.1 500 Internal Server Error\r\n\r\n";
                                send(client_sock, err, strlen(err), 0);
                                if (db) log_to_db(db, req->host, req->method, 500, bytes_recv, strlen(err), client_ip, 0);
                            }
                        }
                        free(forward_buf);
                    }
                }
                ParsedRequest_destroy(req);
            }
        }
        close(client_sock);
    }
    return NULL;
}

int main() {
    int port_number = 8888;
    char *port_env = getenv("PROXY_PORT");
    if (port_env != NULL) port_number = atoi(port_env);

    db_path_global = getenv("DB_PATH");
    if (db_path_global == NULL) db_path_global = "/data/netshield.db";

    pthread_t threads[NUM_THREADS];
    for (int i = 0; i < NUM_THREADS; i++) {
        pthread_create(&threads[i], NULL, worker_thread, NULL);
    }

    int proxy_socketId = socket(AF_INET, SOCK_STREAM, 0);
    int reuse = 1;
    setsockopt(proxy_socketId, SOL_SOCKET, SO_REUSEADDR, (const char *)&reuse, sizeof(reuse));

    struct sockaddr_in server_addr;
    bzero((char *)&server_addr, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port_number);
    server_addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(proxy_socketId, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("Bind failed");
        exit(1);
    }

    listen(proxy_socketId, 500);
    printf("NetShield Phase 9 [INTELLIGENCE] Proxy Server running on port %d with Live Blocklists!\n", port_number);

    while (1) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        int client_socketId = accept(proxy_socketId, (struct sockaddr *)&client_addr, &client_len);
        if (client_socketId >= 0) {
            enqueue(client_socketId);
        }
    }

    close(proxy_socketId);
    return 0;
}
