#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/select.h>
#include <errno.h>
#include <net/if.h>
#include <stdbool.h>

#define MAX_CLIENTS 500
#define BUFFER_SIZE 1024

struct client_info {
        struct sockaddr_in6 addr;
        uint16_t port;
};

struct client_info client_addresses[MAX_CLIENTS];
int num_clients = 0;

bool add_client(struct sockaddr_in6 *client_addr, uint16_t port) {
    if (num_clients < MAX_CLIENTS) {
        client_addresses[num_clients].addr = *client_addr;
        client_addresses[num_clients].port = port; // Convert to host byte order
        fprintf(stderr, "Adding client: %s, Port: %d\n",
                inet_ntop(AF_INET6, &client_addr->sin6_addr, (char[INET6_ADDRSTRLEN]){0}, INET6_ADDRSTRLEN),
                ntohs(port));
        num_clients++;
        return true;
    } else {
        fprintf(stderr, "Max clients reached, cannot add more\n");
        return false;
    }
}

bool remove_client(struct sockaddr_in6 *client_addr, uint16_t port) {
    char client_addr_str[INET6_ADDRSTRLEN];
    inet_ntop(AF_INET6, &client_addr->sin6_addr, client_addr_str, INET6_ADDRSTRLEN);
    uint16_t port_host_order = ntohs(port); // Convert received port to host byte order

    for (int i = 0; i < num_clients; i++) {
        char existing_client_addr_str[INET6_ADDRSTRLEN];
        inet_ntop(AF_INET6, &client_addresses[i].addr.sin6_addr, existing_client_addr_str, INET6_ADDRSTRLEN);

        if (strcmp(existing_client_addr_str, client_addr_str) == 0 &&
            client_addresses[i].port == port) {
            client_addresses[i] = client_addresses[--num_clients];
            fprintf(stderr, "Removed client: %s, Port:%d\n", client_addr_str, port_host_order);
            return true;
        }
    }
    fprintf(stderr, "Failed to remove client: %s, Port: %d) \n", client_addr_str, port_host_order);
    return false;
}

void forward_packet(int multicast_socket, char *buffer, ssize_t len) {
    for (int i = 0; i < num_clients; i++) {
        client_addresses[i].addr.sin6_port = htons(client_addresses[i].port);
        sendto(multicast_socket, buffer, len, 0, (struct sockaddr *)&client_addresses[i].addr, sizeof(struct sockaddr_in6));
    }
}

void send_response(int socket, struct sockaddr_in6 *client_addr, const char *message) {
    sendto(socket, message, strlen(message), 0, (struct sockaddr *)client_addr, sizeof(struct sockaddr_in6));
}

void handle_join_leave_request(int multicast_socket, int request_socket) {
    struct sockaddr_in6 client_addr;
    unsigned int addr_len = sizeof(struct sockaddr_in6);
    char buffer[BUFFER_SIZE];
    ssize_t recv_len;
    char addr_str[INET6_ADDRSTRLEN];

    recv_len = recvfrom(request_socket, buffer, BUFFER_SIZE, 0, (struct sockaddr *)&client_addr, &addr_len);
    if (recv_len <= 0) return;

    buffer[recv_len] = '\0'; // Ensure null-termination for string operations

    char *token = strtok(buffer, ":");
    if (token != NULL) {
        char *cmd = token;
        token = strtok(NULL, ":");
        if (token != NULL) {
            uint16_t port = ntohs((uint16_t)atoi(token));

            inet_ntop(AF_INET6, &client_addr.sin6_addr, addr_str, INET6_ADDRSTRLEN);

            if (strcmp(cmd, "JOIN") == 0) {
                bool success = add_client(&client_addr, port);
                send_response(request_socket, &client_addr, success ? "SUCCESSFULLY JOINED" : "FAILED TO JOIN");
                printf("Client joined: %s, Port: %d\n", addr_str, ntohs(port));
            } else if (strcmp(cmd, "LEAVE") == 0) {
                bool success = remove_client(&client_addr, port);
                send_response(request_socket, &client_addr, success ? "SUCCESSFULLY LEFT" : "FAILED TO LEAVE");
                if(success) { printf("Client left: %s, Port: %d\n", addr_str, ntohs(port)); }
            } else {
                send_response(request_socket, &client_addr, "Uknown request\n");
                printf("Unknown request\n");
            }
        }
    }
}

void setup_sockaddr_in6(struct sockaddr_in6 *addr, const char *ip, int port) {
    memset(addr, 0, sizeof(struct sockaddr_in6));
    addr->sin6_family = AF_INET6;
    addr->sin6_port = htons(port);
    inet_pton(AF_INET6, ip, &addr->sin6_addr);
}

int create_socket(int domain, int type, int protocol) {
    int sock = socket(domain, type, protocol);
    if (sock < 0) {
        perror("Error creating socket");
        exit(EXIT_FAILURE);
    }

    int reuse = 1;
    if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, (char *)&reuse, sizeof(reuse)) < 0) {
        perror("Setting SO_REUSEADDR error");
        close(sock);
        exit(EXIT_FAILURE);
    }

    return sock;
}

void bind_socket(int socket, struct sockaddr_in6 *addr) {
    if (bind(socket, (struct sockaddr *)addr, sizeof(*addr)) < 0) {
        perror("Error binding socket");
        close(socket);
        exit(EXIT_FAILURE);
    }
}

void join_multicast_group(int socket, const char *multicast_ip, unsigned int interface_index) {
    struct ipv6_mreq group;
    inet_pton(AF_INET6, multicast_ip, &group.ipv6mr_multiaddr);
    group.ipv6mr_interface = interface_index; // Use the interface index

    if (setsockopt(socket, IPPROTO_IPV6, IPV6_JOIN_GROUP, &group, sizeof(group)) < 0) {
        perror("Error joining multicast group");
        close(socket);
        exit(EXIT_FAILURE);
    }
}

unsigned int get_interface_index(const char *interface_name) {
    unsigned int index = if_nametoindex(interface_name);
    if (index == 0) {
        perror("Error getting interface index");
        exit(EXIT_FAILURE);
    }
    return index;
}

int main(int argc, char *argv[]) {
if (argc != 6) {
        fprintf(stderr, "Usage: %s <multicast_ip> <multicast_port> <request_port> <multicast_interface> <request_interface>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    const char *multicast_ip = argv[1];
    int multicast_port = atoi(argv[2]);
    int request_port = atoi(argv[3]);
    const char *multicast_interface = argv[4];
    const char *request_interface = argv[5];

    unsigned int multicast_interface_index = get_interface_index(multicast_interface);
    unsigned int request_interface_index = get_interface_index(request_interface);

    int multicast_socket = create_socket(AF_INET6, SOCK_DGRAM, 0);
    int request_socket = create_socket(AF_INET6, SOCK_DGRAM, 0);

    struct sockaddr_in6 multicast_addr;
    setup_sockaddr_in6(&multicast_addr, multicast_ip, multicast_port);
    bind_socket(multicast_socket, &multicast_addr);

    join_multicast_group(multicast_socket, multicast_ip, multicast_interface_index);

    struct sockaddr_in6 request_addr;
    memset(&request_addr, 0, sizeof(request_addr));
    request_addr.sin6_family = AF_INET6;
    request_addr.sin6_port = htons(request_port);
    request_addr.sin6_scope_id = request_interface_index;
    bind_socket(request_socket, &request_addr);

    printf("Listening for multicast UDP packets on %s:%d and for join/leave requests on port %d\n", multicast_ip, multicast_port, request_port);

    fd_set readfds;
    int max_sd;

    while (1) {
        FD_ZERO(&readfds);
        FD_SET(multicast_socket, &readfds);
        FD_SET(request_socket, &readfds);
        max_sd = (request_socket > multicast_socket) ? request_socket : multicast_socket;

                int activity = select(max_sd + 1, &readfds, NULL, NULL, NULL);

                if ((activity < 0) && (errno != EINTR)) {
                        printf("select error");
                }

                if (FD_ISSET(multicast_socket, &readfds)) {
                        char buffer[BUFFER_SIZE];
                        struct sockaddr_in6 src_addr;
                        unsigned int addr_len = sizeof(struct sockaddr_in6);

                        ssize_t recv_len = recvfrom(multicast_socket, buffer, sizeof(buffer), 0, (struct sockaddr *)&src_addr, &addr_len);
                        if (recv_len > 0) {
                                printf("Received %ld bytes from multicast group\n", recv_len);
                                forward_packet(multicast_socket, buffer, recv_len);
                        }
                }

                if (FD_ISSET(request_socket, &readfds)) {
                        handle_join_leave_request(multicast_socket, request_socket);
                }
    }

    close(multicast_socket);
    close(request_socket);
    return 0;
}