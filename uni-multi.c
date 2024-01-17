#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

void forward_udp_unicast_to_multicast(const char *unicast_ip, int unicast_port, const char *multicast_ip, int multicast_port, const char *interface_ip) {
    int unicast_socket, multicast_socket;
    struct sockaddr_in6 unicast_addr, multicast_addr;
    unsigned int addr_len;
    char buffer[1024];
    int ttl = 255;  // Time-to-live

    // Create a UDP socket for receiving unicast
    unicast_socket = socket(AF_INET6, SOCK_DGRAM, 0);
    if (unicast_socket < 0) {
        perror("Error creating unicast socket");
        exit(EXIT_FAILURE);
    }

    memset(&unicast_addr, 0, sizeof(unicast_addr));
    unicast_addr.sin6_family = AF_INET6;
    unicast_addr.sin6_port = htons(unicast_port);
    inet_pton(AF_INET6, unicast_ip, &unicast_addr.sin6_addr);

    if (bind(unicast_socket, (struct sockaddr *)&unicast_addr, sizeof(unicast_addr)) < 0) {
        perror("Error binding unicast socket");
        exit(EXIT_FAILURE);
    }

    // Create a UDP socket for sending multicast
    multicast_socket = socket(AF_INET6, SOCK_DGRAM, 0);
    if (multicast_socket < 0) {
        perror("Error creating multicast socket");
        exit(EXIT_FAILURE);
    }

    // Set the time-to-live for messages
    if (setsockopt(multicast_socket, IPPROTO_IPV6, IPV6_MULTICAST_HOPS, &ttl, sizeof(ttl)) < 0) {
        perror("Error setting TTL for multicast socket");
        exit(EXIT_FAILURE);
    }

    printf("Listening for UDP packets on %s:%d\n", unicast_ip, unicast_port);
    printf("Forwarding packets to multicast group %s:%d\n", multicast_ip, multicast_port);

    while (1) {
        addr_len = sizeof(struct sockaddr_in6);
        ssize_t recv_len = recvfrom(unicast_socket, buffer, 1024, 0, (struct sockaddr *)&unicast_addr, &addr_len);
        if (recv_len > 0) {
            printf("Received %ld bytes\n", recv_len);

            memset(&multicast_addr, 0, sizeof(multicast_addr));
            multicast_addr.sin6_family = AF_INET6;
            multicast_addr.sin6_port = htons(multicast_port);
            inet_pton(AF_INET6, multicast_ip, &multicast_addr.sin6_addr);

            sendto(multicast_socket, buffer, recv_len, 0, (struct sockaddr *)&multicast_addr, sizeof(multicast_addr));
            printf("Forwarded data to multicast group %s:%d\n", multicast_ip, multicast_port);
        }
    }

    close(unicast_socket);
    close(multicast_socket);
}

int main(int argc, char *argv[]) {
    if (argc != 6) {
        fprintf(stderr, "Usage: %s <unicast_ip> <unicast_port> <multicast_ip> <multicast_port> <interface_ip>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    const char *unicast_ip = argv[1];
    int unicast_port = atoi(argv[2]);
    const char *multicast_ip = argv[3];
    int multicast_port = atoi(argv[4]);
    const char *interface_ip = argv[5];

    forward_udp_unicast_to_multicast(unicast_ip, unicast_port, multicast_ip, multicast_port, interface_ip);
    return 0;
}
