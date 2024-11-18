#include <iostream>
#include <cstdlib>
#include <cstring>
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/socket.h>
#include <unistd.h>
#include <sys/time.h>
#include "protocol.h"
#include <cstdio>


// Function to print usage and exit
void printUsageAndExit() {
    std::cerr << "Usage: ./client <IP/DNS>:<Port>" << std::endl;
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


// Function to split the input into IP and port
bool parseIpPort(const std::string& input, std::string& ip, int& port) {

    size_t delimiterPos = input.find(':');
    if (delimiterPos == std::string::npos) {
        return false; // ':' not found
    }

    ip = input.substr(0, delimiterPos);
    std::string portStr = input.substr(delimiterPos + 1);

    try {
        port = std::stoi(portStr);
        if (port <= 0 || port > 65535) {
            throw std::out_of_range("Invalid port");
        }
    } catch (...) {
        return false; // Port is not a valid number or out of range
    }
    printf("Host: %s, Port: %d.\n", ip.c_str(), port);
    return true;
}

void performCalculation(calcProtocol& response, int sockfd, struct addrinfo* res) {
    int arith = ntohl(response.arith);
    int intValue1 = ntohl(response.inValue1);
    int intValue2 = ntohl(response.inValue2);
    double flValue1 = response.flValue1;  
    double flValue2 = response.flValue2;  

    switch (arith) {
        case 1:  // Addition
            response.inResult = htonl(intValue1 + intValue2);
            std::cout << "ASSIGNMENT: add " << intValue1 << " " << intValue2 << std::endl;
            break;
        case 2:  // Subtraction
            response.inResult = htonl(intValue1 - intValue2);
            std::cout << "ASSIGNMENT: sub " << intValue1 << " " << intValue2 << std::endl;
            break;
        case 3:  // Multiplication
            response.inResult = htonl(intValue1 * intValue2);
            std::cout << "ASSIGNMENT: mul " << intValue1 << " " << intValue2 << std::endl;
            break;
        case 4:  // Division
            if (intValue2 != 0) {
                response.inResult = htonl(intValue1 / intValue2);
                std::cout << "ASSIGNMENT: div " << intValue1 << " " << intValue2 << std::endl;
            } else {
                std::cerr << "Division by zero error!" << std::endl;
                return;
            }
            break;
        case 5:  // Floating-point Addition
            response.flResult = flValue1 + flValue2;
            std::cout << "ASSIGNMENT: fadd " << flValue1 << " " << flValue2 << std::endl;
            break;
        case 6:  // Floating-point Subtraction
            response.flResult = flValue1 - flValue2;
            std::cout << "ASSIGNMENT: fsub " << flValue1 << " " << flValue2 << std::endl;
            break;
        case 7:  // Floating-point Multiplication
            response.flResult = flValue1 * flValue2;
            std::cout << "ASSIGNMENT: fmul " << flValue1 << " " << flValue2 << std::endl;
            break;
        case 8:  // Floating-point Division
            if (flValue2 != 0.0) {
                response.flResult = flValue1 / flValue2;
                std::cout << "ASSIGNMENT: fdiv " << flValue1 << " " << flValue2 << std::endl;
            } else {
                std::cerr << "Division by zero error!" << std::endl;
                return;
            }
            break;
        default:
            std::cerr << "Invalid arithmetic operation code: " << arith << std::endl;
            return;
    }

    // Update calcprotocal struct for sending back to server. Change the type equals to 2 so that specifies client to server message. 
    response.type = htons(2); 
    response.major_version = htons(1);
    response.minor_version = htons(0);

    // Send the updated struct back to the server.
    ssize_t sentBytes = sendto(sockfd, &response, sizeof(response), 0, res->ai_addr, res->ai_addrlen);
    if (sentBytes < 0) {
        perror("Failed to send result to server");
    }
    // Wait for and process server's confirmation response
    calcMessage serverResponse;
    socklen_t addrLen = res->ai_addrlen;

    struct timeval timeout;
    timeout.tv_sec = 2;  // Set timeout of 2 seconds
    timeout.tv_usec = 0;

    fd_set readfds;
    FD_ZERO(&readfds);
    FD_SET(sockfd, &readfds);

    int selectResult = select(sockfd + 1, &readfds, nullptr, nullptr, &timeout);
    
    if (selectResult > 0 && FD_ISSET(sockfd, &readfds)) {
    ssize_t receivedBytes = recvfrom(sockfd, &serverResponse, sizeof(serverResponse), 0, res->ai_addr, &addrLen);
    if (receivedBytes > 0) {
        // Check the 'message' field in the server's response
        int serverMessage = ntohl(serverResponse.message);
        if (serverMessage == 1) {
            std::cout << "OK" << std::endl;
        } else {
            std::cout << "NOT OK" << std::endl;
        }
    } else {
        perror("Failed to receive server's response");
    }
    } else if (selectResult == 0) {
    std::cerr << "Timeout waiting for server's response." << std::endl;
    } else {
    perror("Error while waiting for server's response");
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
    if (argc != 2) {
        printUsageAndExit();
    }

    std::string serverAddress, ip;
    int port;

    serverAddress = argv[1];
    if (!parseIpPort(serverAddress, ip, port)) {
        std::cerr << "Invalid input format. Expected <IP>:<Port>." << std::endl;
        printUsageAndExit();
    }

    // Resolve the address (IPv4, IPv6, and DNS)
    struct addrinfo hints{}, *res;
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_DGRAM;

    int status = getaddrinfo(ip.c_str(), std::to_string(port).c_str(), &hints, &res);
    if (status != 0) {
        std::cerr << "Error resolving address: " << gai_strerror(status) << std::endl;
        exit(EXIT_FAILURE);
    }

    // Create a UDP socket
    int sockfd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (sockfd < 0) {
        perror("Socket creation failed");
        freeaddrinfo(res);
        exit(EXIT_FAILURE);
    }

    // Prepare the initial calcMessage to send to the server
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
        performCalculation(response, sockfd, res);
    }

    // Clean up
    close(sockfd);
    freeaddrinfo(res);

    return 0;
}
