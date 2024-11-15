#include <iostream>
#include <cstdlib>
#include <cstring>
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/socket.h>
#include <unistd.h>
#include <sys/time.h>
#include "protocol.h"


// Function to print usage and exit
void printUsageAndExit() {
    std::cerr << "Usage: ./client <IP/DNS> <Port>" << std::endl;
    exit(EXIT_FAILURE);
}
// Function to check and print "NOT OK" message
bool isNotOkMessage(const calcMessage& response) { 
    if (ntohs(response.type) == 2 && ntohl(response.message) == 2 &&
        ntohs(response.major_version) == 1 && ntohs(response.minor_version) == 0) {
        std::cerr << "Server sent a 'NOT OK' message. Terminating client." << std::endl;
        return true;
    }
    return false;
}


// Perform the operation based on arith value and update results
void performCalculation(calcProtocol& response) {
    int arith = ntohl(response.arith);
    int intValue1 = ntohl(response.inValue1);
    int intValue2 = ntohl(response.inValue2);
    double flValue1 = response.flValue1;  
    double flValue2 = response.flValue2;  

    switch (arith) {
        case 1:  // add
            response.inResult = htonl(intValue1 + intValue2);
            std::cout << "Assignment: add " << intValue1 << " " << intValue2 << std::endl;
            break;
        case 2:  // sub
            response.inResult = htonl(intValue1 - intValue2);
            std::cout << "Assignment: sub " << intValue1 << " " << intValue2 << std::endl;
            break;
        case 3:  // mul
            response.inResult = htonl(intValue1 * intValue2);
            std::cout << "Assignment: mul " << intValue1 << " " << intValue2 << std::endl;
            break;
        case 4:  // div
            if (intValue2 != 0) {
                response.inResult = htonl(intValue1 / intValue2);
                std::cout << "Assignment: mul " << intValue1 << " "<< intValue2 << std::endl;
            } else {
                std::cerr << "Division by zero error!" << std::endl;
            }
            break;
        case 5:  // fadd
            response.flResult = flValue1 + flValue2;
            std::cout << "Assignment: fadd " << flValue1 << " " << flValue2 << std::endl;
            break;
        case 6:  // fsub
            response.flResult = flValue1 - flValue2;
            std::cout << "Assignment: fsub " << flValue1 << " " << flValue2 << std::endl;
            break;
        case 7:  // fmul
            response.flResult = flValue1 * flValue2;
            std::cout << "Assignment: fmul " << flValue1 << " " << flValue2 << std::endl;
            break;
        case 8:  // fdiv
            if (flValue2 != 0.0) {
                response.flResult = flValue1 / flValue2;
                std::cout << "Assignment: fdiv " << flValue1 << " " << flValue2 << std::endl;
            } else {
                std::cerr << "Division by zero error!" << std::endl;
            }
            break;
        default:
            std::cerr << "Invalid arithmetic operation code: " << arith << std::endl;
            break;
    }
}


// Function to send and receive message with retry
bool sendAndReceiveWithRetry(int sockfd, struct addrinfo* res, calcMessage& message, calcProtocol& response) {
    const int maxRetries = 3;
    const int timeoutSec = 2;
    socklen_t addrLen = res->ai_addrlen;

    for (int attempt = 1; attempt <= maxRetries; ++attempt) {
        // Send the message
        ssize_t sentBytes = sendto(sockfd, &message, sizeof(message), 0, res->ai_addr, res->ai_addrlen);
        if (sentBytes < 0) {
            perror("Failed to send message");
            return false;
        }

        // Set up timeout for receiving
        struct timeval timeout;
        timeout.tv_sec = timeoutSec;
        timeout.tv_usec = 0;

        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(sockfd, &readfds);

        // Wait for response with timeout
        int selectResult = select(sockfd + 1, &readfds, nullptr, nullptr, &timeout);
        if (selectResult > 0 && FD_ISSET(sockfd, &readfds)) {
            // Receive the response from the server
            ssize_t receivedBytes = recvfrom(sockfd, &response, sizeof(response), 0, res->ai_addr, &addrLen);
            if (receivedBytes > 0) {
                return true;  // Successfully received the response
            } else {
                perror("Failed to receive response");
            }
        } else if (selectResult < 0) {
            perror("Error in select");
            return false;
        }
    }

    std::cerr << "No response from server after " << maxRetries << " attempts. Terminating." << std::endl;
    return false;  // No response after maximum retries
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

    // Prepare the initial calcMessage to send to the server.
    calcMessage message;
    memset(&message, 0, sizeof(message));
    message.type = htons(22);             // Client-to-server binary protocol
    message.message = htonl(0);           // First message
    message.protocol = htons(17);         // UDP protocol
    message.major_version = htons(1);     // Protocol version 1.0
    message.minor_version = htons(0);

    // Prepare the response buffer and message count
    calcProtocol response;

    // Attempt to send and receive response with retries
    if (sendAndReceiveWithRetry(sockfd, res, message, response)) {
    performCalculation(response);
}

    // Clean up
    close(sockfd);
    freeaddrinfo(res);

    return 0;
}
