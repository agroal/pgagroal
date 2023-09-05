#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <openssl/ssl.h>
#include <openssl/err.h>
#include "uthash.h"

#include <pgagroal.h>
#include <logging.h>
#include <utils.h>
#include <message.h>

#define HTTP_PORT 80
#define HTTPS_PORT 443
#define BUFFER_SIZE 1024

struct KV {
    char* key;
    char* value;
    UT_hash_handle hh;
};

static void initialize_ssl(void);
static SSL_CTX *create_ssl_context(void);
static void configure_ssl_context(SSL_CTX *ctx);
static void parse_query_string(const char *query_string, struct KV **hash_map);
static void free_hash_map(struct KV **hash_map);
static void handle(int client_fd, SSL *ssl, int is_http);
static void handle_url(const char *buffer, int client_fd, SSL *ssl, int is_http);
static void *http_thread_func(void *arg);
static void *https_thread_func(void *arg);
static void handle_get_password_by_name(const char *method, struct KV *params, char *response, size_t response_size);
static void handle_info(const char *method, const char *contents, char *response, size_t response_size);
static void handle_default(char *response, size_t response_size);

static SSL_CTX *ssl_ctx;
static int client_socket;

static void initialize_ssl(void) {
    SSL_library_init();
    SSL_load_error_strings();
    OpenSSL_add_all_algorithms();
}

static SSL_CTX *create_ssl_context() {
    const SSL_METHOD *method;
    SSL_CTX *ctx;

    method = SSLv23_server_method();
    ctx = SSL_CTX_new(method);
    if (ctx == NULL) {
        pgagroal_log_error("pgagroal_vault: failed to create ssl context");
        exit(EXIT_FAILURE);
    }

    return ctx;
}

static void configure_ssl_context(SSL_CTX *ctx) {
    char crt[100]; 
    char key[100];
    sprintf(crt, "%s/.pgagroal/server.crt", pgagroal_get_home_directory());
    sprintf(key, "%s/.pgagroal/server.key", pgagroal_get_home_directory());
    if (SSL_CTX_use_certificate_file(ctx, crt, SSL_FILETYPE_PEM) <= 0) {
        pgagroal_log_error("pgagroal_vault: failed to use cert file %s", "server.crt");
        exit(EXIT_FAILURE);
    }

    if (SSL_CTX_use_PrivateKey_file(ctx, key, SSL_FILETYPE_PEM) <= 0) {
        pgagroal_log_error("pgagroal_vault: failed to use cert file %s", "server.key");
        exit(EXIT_FAILURE);
    }
}

static void parse_query_string(const char *path, struct KV **map) {
    char *token, *key, *value, *saveptr1, *saveptr2, *query;
    query = strchr(path, '?');
    if (query) {
        *query = '\0';
        token = strtok_r(query + 1, "&", &saveptr1);
        while (token) {
            key = strtok_r(token, "=", &saveptr2);
            value = strtok_r(NULL, "=", &saveptr2);
            struct KV *kv = malloc(sizeof(struct KV));
            kv->key = strcpy(malloc(strlen(key)), key);
            kv->value = strcpy(malloc(strlen(value)), value);
            HASH_ADD_STR(*map, key, kv);
            token = strtok_r(NULL, "&", &saveptr1);
        }
    }
}

static void free_hash_map(struct KV **hash_map) {
    struct KV *entry, *tmp;
    HASH_ITER(hh, *hash_map, entry, tmp) {
        HASH_DEL(*hash_map, entry);
        free((void *)entry->key);
        free((void *)entry->value);
        free(entry);
    }
}

static void handle(int client_fd, SSL *ssl, int is_http) {
    char buffer[BUFFER_SIZE];
    ssize_t bytes_read;
    if (is_http) {
        bytes_read = read(client_fd, buffer, sizeof(buffer) - 1);
    } else {
        bytes_read = SSL_read(ssl, buffer, sizeof(buffer) - 1);
    }
    if (bytes_read <= 0) {
        if (!is_http) {
            SSL_free(ssl);
        }
        close(client_fd);
        return;
    }
    buffer[bytes_read] = '\0';
    handle_url(buffer, client_fd, ssl, is_http);
}

static void handle_url(const char *buffer, int client_fd, SSL *ssl, int is_http) {
    char method[8];
    char path[128];
    char contents[BUFFER_SIZE];
    
    sscanf(buffer, "%7s %127s", method, path);
    
    // Extract the POST data 
    char *body = strstr(buffer, "\r\n\r\n");
    if (body) {
        strcpy(contents, body + 4);
    }

    struct KV *map = NULL;

    // Parse URL parameters for GET requests
    if (strcmp(method, "GET") == 0) {
        parse_query_string(path, &map);
    }

    // Call the appropriate handler function for the URL path
    char response[BUFFER_SIZE];
    if (strcmp(path, "/get_password_by_name") == 0) {
        handle_get_password_by_name(method, map, response, sizeof(response));
    } else if (strcmp(path, "/info") == 0) {
        handle_info(method, contents, response, sizeof(response));
    } else {
        handle_default(response, sizeof(response));
    }

    // Send the response
    if (is_http) {
        write(client_fd, response, strlen(response));
    } else {
        SSL_write(ssl, response, strlen(response));
        SSL_shutdown(ssl);
        SSL_free(ssl);
    }

    close(client_fd);
    free_hash_map(&map);
}

static void handle_get_password_by_name(const char *method, struct KV *params, char *response, size_t response_size) {
    // TODO: send messages to unix_management_socket 
    if (strcmp(method, "GET") == 0) {
        struct KV *kv;
        HASH_FIND_STR(params, "username", kv);
        if (kv && strcmp(kv->value, "myuser") == 0) {
            struct message* msg;
            pgagroal_write_frontend_password_request(NULL, client_socket, kv->value);
            pgagroal_read_socket_message(client_socket, &msg);
            snprintf(response, response_size, "HTTP/1.1 200 OK\r\n"
                     "Content-Type: text/plain\r\n"
                     "\r\n"
                     "Password is: %s\r\n", (char*)msg->data);
        } else {
            snprintf(response, response_size, "HTTP/1.1 400 Bad Request\r\n\r\n");
        }
    } else {
        snprintf(response, response_size, "HTTP/1.1 405 Method Not Allowed\r\n\r\n");
    }
}

static void handle_info(const char *method, const char *contents, char *response, size_t response_size) {
        snprintf(response, response_size, "HTTP/1.1 200 OK\r\n"
                "Content-Type: text/plain\r\n"
                "\r\n"
                "vault is good: \r\n");
}

static void handle_default(char *response, size_t response_size) {
    snprintf(response, response_size, "HTTP/1.1 404 Not Found\r\n\r\n");
}

static void *http_thread_func(void *arg) {
    int server_fd, client_fd;
    struct sockaddr_in server_addr, client_addr;
    socklen_t client_len = sizeof(client_addr);

    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        pgagroal_log_error("Cannot create socket");
        return NULL;
    }

    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(HTTP_PORT);

    if (bind(server_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        pgagroal_log_error("Bind failed");
        return NULL;
    }
    //TODO: ? connection requests will be queued
    if (listen(server_fd, 10) < 0) {
        pgagroal_log_error("Listen failed");
        return NULL;
    }

    printf("HTTP server is listening on port %d...\n", HTTP_PORT);

    while (1) {
        client_fd = accept(server_fd, (struct sockaddr *)&client_addr, &client_len);
        if (client_fd < 0) {
            pgagroal_log_error("Accept failed");
            continue;
        }

        handle(client_fd, NULL, 1);  // 1 indicates HTTP
    }

    close(server_fd);
    return NULL;
}

static void *https_thread_func(void *arg) {
    int server_fd, client_fd;
    struct sockaddr_in server_addr, client_addr;
    socklen_t client_len = sizeof(client_addr);

    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        pgagroal_log_error("Cannot create socket");
        return NULL;
    }

    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(HTTPS_PORT);

    if (bind(server_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        pgagroal_log_error("Bind failed");
        return NULL;
    }

    if (listen(server_fd, 10) < 0) {
        pgagroal_log_error("Listen failed");
        return NULL;
    }

    printf("HTTPS server is listening on port %d...\n", HTTPS_PORT);

    while (1) {
        client_fd = accept(server_fd, (struct sockaddr *)&client_addr, &client_len);
        if (client_fd < 0) {
            pgagroal_log_error("Accept failed");
            continue;
        }

        SSL *ssl = SSL_new(ssl_ctx);
        SSL_set_fd(ssl, client_fd);

        if (SSL_accept(ssl) <= 0) {
            pgagroal_log_error("pgagroal_vault: ssl filed to accept");
        } else {
           handle(client_fd, ssl, 0);  // 0 indicates HTTPS
        }

        // SSL_free(ssl);
    }

    close(server_fd);
    return NULL;
}

int main(int arvc, char** argv) {
    initialize_ssl();
    ssl_ctx = create_ssl_context();
    configure_ssl_context(ssl_ctx);

    printf("creating thread\n");

    client_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (client_socket == -1) {
        perror("Socket creation error");
        exit(EXIT_FAILURE);
    }
    
    struct sockaddr_in serverAddress;
    serverAddress.sin_family = AF_INET;
    serverAddress.sin_port = htons(6789);
    serverAddress.sin_addr.s_addr = inet_addr("127.0.0.1");
    
    if (connect(client_socket, (struct sockaddr *)&serverAddress, sizeof(serverAddress)) == -1) {
        perror("Connection error");
        close(client_socket);
        exit(EXIT_FAILURE);
    }

    pthread_t http_thread, https_thread;

    int result = pthread_create(&http_thread, NULL, http_thread_func, NULL);
    if(result != 0) {
        printf("something wrong\n");
    }
    pthread_create(&https_thread, NULL, https_thread_func, NULL);

    pthread_join(http_thread, NULL);
    pthread_join(https_thread, NULL);

    SSL_CTX_free(ssl_ctx);
    return 0;
}
