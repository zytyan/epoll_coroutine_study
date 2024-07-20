#include <stdio.h>
#include <stdlib.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <unistd.h>
#include <netinet/in.h>
#include <string.h>
#include <errno.h>
#include <signal.h>
#include <stdbool.h>
#include <fcntl.h>
#include <limits.h>
#include <arpa/inet.h>
#include <time.h>
#include "coroutine_imp/coroutines.h"

#define MAX_EVENTS 2048
#define PORT 8080
static bool g_running = true;
static bool print_detail = false;
static struct co_event_loop *loop;


void sig_handler(int signo) {
    if (signo == SIGINT) {
        g_running = false;
    }
}

int set_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1) {
        return -1;
    }
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

int set_server_socket() {
    int server_fd;
    struct sockaddr_in address;
    // 创建socket
    if ((server_fd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0)) == 0) {
        perror("socket failed");
        exit(EXIT_FAILURE);
    }

    // 设置socket选项
    int opt = 1;
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt, sizeof(opt))) {
        perror("setsockopt");
        close(server_fd);
        exit(EXIT_FAILURE);
    }

    // 绑定socket
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(PORT);

    if (bind(server_fd, (struct sockaddr *) &address, sizeof(address)) < 0) {
        perror("bind failed");
        close(server_fd);
        exit(EXIT_FAILURE);
    }

    // 监听socket
    if (listen(server_fd, 512) < 0) {
        perror("listen");
        close(server_fd);
        exit(EXIT_FAILURE);
    }
    return server_fd;
}

struct my_epoll_data {
    int fd;
    int epoll_fd;
    uint32_t expect_event_mask;
    struct co_future *future;
};
#define SAVE_ERRNO(x) do {\
    int _errno = errno;\
    x;\
    errno = _errno;\
} while(0)

static ssize_t coroutine_block_read(int fd, void *buf, size_t count, struct my_epoll_data *data) {
    while (true) {
        ssize_t read_size = read(fd, buf, count);
        if (read_size < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            struct co_future future = co_new_future();
            data->future = &future;
            data->expect_event_mask = EPOLLIN | EPOLLHUP;
            SAVE_ERRNO(co_block());
            continue;
        }
        return read_size;
    }
}

static ssize_t coroutine_block_write(int fd, void *buf, size_t count, struct my_epoll_data *data) {
    while (true) {
        ssize_t write_size = write(fd, buf, count);
        if (write_size < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            struct co_future future = co_new_future();
            data->future = &future;
            data->expect_event_mask = EPOLLOUT | EPOLLHUP;
            SAVE_ERRNO(co_block());
            continue;
        } else if (write_size <= 0) {
            return write_size;
        } else if (write_size < count) {
            struct co_future future = co_new_future();
            data->future = &future;
            data->expect_event_mask = EPOLLOUT | EPOLLHUP;
            count -= write_size;
            SAVE_ERRNO(co_block());
            continue;
        }
        return write_size;
    }
}

int format_socket_address(struct sockaddr_in *addr, char *buf, size_t size) {
    return snprintf(buf, size, "%s:%d", inet_ntoa(addr->sin_addr), ntohs(addr->sin_port));
}

void handle_client(void *arg) {
    struct my_epoll_data *data = (struct my_epoll_data *) arg;
    int fd = data->fd;
    char buffer[1024];
    ssize_t read_size = coroutine_block_read(fd, buffer, sizeof(buffer), data);
    if (read_size == 0) {
        printf("client closed\n");
        close(fd);
        free(data);
        return;
    }
    char *response = "HTTP/1.1 200 OK\r\n"
                     "Content-Length: 15\r\n\r\n"
                     "Hello, World!\r\n";
    co_sleep(100 * 1000 * 1000);// sleep 100ms
    ssize_t write_size = coroutine_block_write(fd, response, strlen(response), data);
    if (write_size == -1) {
        perror("write");
    }
    close(fd);
    free(data);
}

void handle_server(int epoll_fd, int server_fd) {
    while (true) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        int new_socket = accept(server_fd, (struct sockaddr *) &client_addr, &client_len);
        if (new_socket == -1) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                return;
            }
            perror("accept");
            return;
        }
        set_nonblocking(new_socket);
        struct epoll_event event;
        event.events = EPOLLIN | EPOLLET;
        event.data.ptr = malloc(sizeof(struct my_epoll_data));
        struct my_epoll_data *data = (struct my_epoll_data *) event.data.ptr;
        data->fd = new_socket;
        data->epoll_fd = epoll_fd;
        data->expect_event_mask = EPOLLIN | EPOLLHUP;
        data->future = NULL;
        if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, new_socket, &event) == -1) {
            free(event.data.ptr);
            perror("epoll_ctl: new_socket");
            close(new_socket);
        }
        char name[32];
        format_socket_address(&client_addr, name, sizeof(name));
        if (co_spawn(loop, handle_client, data, name) != 0) {
            perror("new_coroutine");
            free(event.data.ptr);
            close(new_socket);
        }
    }
}

void handle_events(struct epoll_event *events, int num_events, struct my_epoll_data *epoll_h, int server_fd) {
    for (int i = 0; i < num_events; i++) {
        if ((events[i].events & EPOLLIN) && events[i].data.ptr == epoll_h) {
            // 处理可读事件
            int epoll_fd = epoll_h->fd;
            handle_server(epoll_fd, server_fd);
            continue;
        }
        struct my_epoll_data *data = (struct my_epoll_data *) events[i].data.ptr;
        if (data->future == NULL) {
            printf("data->future==NULL\n");
        }
        if (events[i].events & data->expect_event_mask) {
            co_wakeup(loop, data->future);
        } else {
            printf("unexpected event\n");
        }
    }
}

void sigquit_handler(int signo) {
    if (signo == SIGQUIT) {
        co_print_all_coroutine();
    }
}

int main(int argc, char *argv[]) {
    if (argc > 1 && strcmp(argv[1], "-v") == 0) {
        print_detail = true;
    } else {
        printf("use %s -v to print detail.\n", argv[0]);
    }
    int server_fd = set_server_socket();
    struct epoll_event event, events[MAX_EVENTS];
    if (co_setup(1000) != 0) {
        printf("co_setup failed\n");
        return -1;
    }
    loop = co_get_loop();
    // 创建epoll实例
    int epoll_fd = epoll_create1(0);
    if (epoll_fd == -1) {
        perror("epoll_create1");
        close(server_fd);
        exit(EXIT_FAILURE);
    }

    // 将服务器socket注册到epoll实例
    struct my_epoll_data epoll_h = {
            .fd = epoll_fd,
            .epoll_fd = epoll_fd,
            .expect_event_mask = EPOLLIN | EPOLLET,
            .future = NULL,
    };
    event.events = EPOLLIN | EPOLLET;
    event.data.ptr = &epoll_h;
    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, server_fd, &event) == -1) {
        perror("epoll_ctl: server_fd");
        close(server_fd);
        close(epoll_fd);
        exit(EXIT_FAILURE);
    }

    signal(SIGINT, sig_handler);
    signal(SIGQUIT, sigquit_handler);
    // 事件循环
    while (g_running) {
        int64_t wait_ns = co_min_wait_time();
        int wait_ms;
        if (wait_ns == -1) {
            wait_ms = -1;
        } else {
            int64_t wms = wait_ns / 1000000;
            wait_ms = wms > INT_MAX ? INT_MAX : (int) wms;
        }
        int num_events = epoll_wait(epoll_fd, events, MAX_EVENTS, wait_ms);
        if (num_events == -1) {
            if (errno != EINTR) {
                perror("epoll_wait");
                close(server_fd);
                close(epoll_fd);
                exit(EXIT_FAILURE);
            }
            co_dispatch(loop);
            continue;
        }
        handle_events(events, num_events, &epoll_h, server_fd);
        co_dispatch(loop);
    }
    co_teardown();
    close(server_fd);
    close(epoll_fd);
    return 0;
}
