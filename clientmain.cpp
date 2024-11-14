

#include <iostream>
#include <cstdlib>
#include <cstring>
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/socket.h>
#include <unistd.h>
#include "protocol.h"

void printUsageAndExit() {
    std::cerr << "Usage: ./client <IP/DNS> <Port>" << std::endl;
    exit(EXIT_FAILURE);
}

int main(int argc, char *argv[]) {
    // Validate input and print usage.
    if (argc != 3) {
        printUsageAndExit();
    }

    // Parse the server address and port from input. 
    const char* serverAddress = argv[1];
    int port = std::atoi(argv[2]);

    if (port <= 0 || port > 65535) {
        std::cerr << "Invalid port number." << std::endl;
        printUsageAndExit();
    }

    // Resolve the address (IPv4, IPv6, and DNS)
    struct addrinfo hints{}, *res;
    hints.ai_family = AF_UNSPEC;        
    hints.ai_socktype = SOCK_DGRAM;     

    int status = getaddrinfo(serverAddress, argv[2], &hints, &res);
    if (status != 0) {
        std::cerr << "Error resolving address: " << gai_strerror(status) << std::endl;
        exit(EXIT_FAILURE);
    }

    // Create a UDP socket. 
    int sockfd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (sockfd < 0) {
        perror("Socket creation failed");
        freeaddrinfo(res);
        exit(EXIT_FAILURE);
    }

    // Prepare and send the calcMessage to the server.
    calcMessage message;
    memset(&message, 0, sizeof(message)); // Initialize to zero
    message.type = htons(22);             // Client-to-server binary protocol
    message.message = htonl(0);           // First message
    message.protocol = htons(17);         // UDP protocol
    message.major_version = htons(1);     // Protocol version 1.0
    message.minor_version = htons(0);

    ssize_t sentBytes = sendto(sockfd, &message, sizeof(message), 0, res->ai_addr, res->ai_addrlen);
    if (sentBytes < 0) {
        perror("Failed to send message");
        close(sockfd);
        freeaddrinfo(res);
        exit(EXIT_FAILURE);
    }

    // Receive the response from the server. 
    calcProtocol response;
    socklen_t addrLen = res->ai_addrlen;
    ssize_t receivedBytes = recvfrom(sockfd, &response, sizeof(response), 0, res->ai_addr, &addrLen);

    if (receivedBytes > 0) {
        std::cout << "Received response from server:" << std::endl;
        std::cout << "Type: " << ntohs(response.type) << std::endl;
        std::cout << "Major Version: " << ntohs(response.major_version) << std::endl;
        std::cout << "Minor Version: " << ntohs(response.minor_version) << std::endl;
        std::cout << "ID: " << ntohl(response.id) << std::endl;
        std::cout << "Arith: " << ntohl(response.arith) << std::endl;
    } else {
        perror("Failed to receive response");
    }

    // Clean up
    close(sockfd);
    freeaddrinfo(res);

    return 0;
}
