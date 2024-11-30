#include <iostream>
#include <vector>
#include <string>
#include <cstdlib>
#include <cstring>
#include <netdb.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <cerrno>
#include <cstdio>
#include <sstream>
#include <cstdint>
#include <iomanip>
#include <random>
#include <signal.h>
#include <calcLib.h>
#include <algorithm>
#include <chrono>
#include <map>
#include "protocol.h"

struct ClientInfo {
    sockaddr_storage addr;
    socklen_t addrLen;
    int id;
    std::chrono::steady_clock::time_point lastActivity;
    calcProtocol assignment;
};

std::map<int, ClientInfo> clients;

int getRandomId() {
    static std::random_device rd;
    static std::mt19937 gen(rd());
    static std::uniform_int_distribution<int> distribution(0, 10000);
    
    int randomId;
    do {
        randomId = distribution(gen);
    } while (clients.find(randomId) != clients.end());
    
    return randomId;
}

void removeInactiveClients() {
    auto now = std::chrono::steady_clock::now();
    for (auto it = clients.begin(); it != clients.end();) {
        if (std::chrono::duration_cast<std::chrono::seconds>(now - it->second.lastActivity).count() > 10) {
            std::cout << "Client " << it->first << " timed out and removed" << std::endl;
            it = clients.erase(it);
        } else {
            ++it;
        }
    }
}

int setupSocket(const char* ip, int port) {
    struct addrinfo hints, *servinfo, *p;
    int sockfd;
    
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_DGRAM;
    
    char portStr[6];
    snprintf(portStr, sizeof(portStr), "%d", port);
    
    if (getaddrinfo(ip, portStr, &hints, &servinfo) != 0) {
        std::cerr << "getaddrinfo error" << std::endl;
        exit(1);
    }
    
    for(p = servinfo; p != NULL; p = p->ai_next) {
        if ((sockfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) == -1) {
            continue;
        }
        
        if (bind(sockfd, p->ai_addr, p->ai_addrlen) == -1) {
            close(sockfd);
            continue;
        }
        
        break;
    }
    
    if (p == NULL) {
        std::cerr << "Failed to bind socket" << std::endl;
        exit(1);
    }
    
    freeaddrinfo(servinfo);
    
    std::cout << "Server listening on " << ip << ":" << port << std::endl;
    return sockfd;
}

calcProtocol generateAssignment() {
    calcProtocol assignment;
    assignment.major_version = htons(1);
    assignment.minor_version = htons(0);
    assignment.id = htonl(getRandomId());
    assignment.type = htons(1);
    
    char* op = randomType();
    if (strcmp(op, "add") == 0) assignment.arith = htonl(1);
    else if (strcmp(op, "sub") == 0) assignment.arith = htonl(2);
    else if (strcmp(op, "mul") == 0) assignment.arith = htonl(3);
    else if (strcmp(op, "div") == 0) assignment.arith = htonl(4);
    else if (strcmp(op, "fadd") == 0) assignment.arith = htonl(5);
    else if (strcmp(op, "fsub") == 0) assignment.arith = htonl(6);
    else if (strcmp(op, "fmul") == 0) assignment.arith = htonl(7);
    else if (strcmp(op, "fdiv") == 0) assignment.arith = htonl(8);
    
    if (ntohl(assignment.arith) <= 4) {
        assignment.inValue1 = htonl(randomInt());
        assignment.inValue2 = htonl(randomInt());
    } else {
        assignment.flValue1 = randomFloat();
        assignment.flValue2 = randomFloat();
    }
    
    return assignment;
}

bool verifyResult(const calcProtocol& assignment, const calcProtocol& result) {
    int op = ntohl(assignment.arith);
    if (op <= 4) {
        int v1 = ntohl(assignment.inValue1);
        int v2 = ntohl(assignment.inValue2);
        int res = ntohl(result.inResult);
        switch(op) {
            case 1: return res == v1 + v2;
            case 2: return res == v1 - v2;
            case 3: return res == v1 * v2;
            case 4: return v2 != 0 && res == v1 / v2;
        }
    } else {
        double v1 = assignment.flValue1;
        double v2 = assignment.flValue2;
        double res = result.flResult;
        switch(op) {
            case 5: return std::abs(res - (v1 + v2)) < 1e-6;
            case 6: return std::abs(res - (v1 - v2)) < 1e-6;
            case 7: return std::abs(res - (v1 * v2)) < 1e-6;
            case 8: return v2 != 0 && std::abs(res - (v1 / v2)) < 1e-6;
        }
    }
    return false;
}

int main(int argc, char *argv[]) {
    if (argc != 2) {
        std::cerr << "Usage: " << argv[0] << " <IP:port>" << std::endl;
        return 1;
    }
    
    std::string arg(argv[1]);
    size_t colonPos = arg.find(':');
    if (colonPos == std::string::npos) {
        std::cerr << "Invalid argument format. Use IP:port" << std::endl;
        return 1;
    }
    
    std::string ip = arg.substr(0, colonPos);
    int port = std::stoi(arg.substr(colonPos + 1));
    
    int sockfd = setupSocket(ip.c_str(), port);
    
    initCalcLib();
    
    while (true) {
        removeInactiveClients();
        
        char buffer[sizeof(calcProtocol)];
        sockaddr_storage clientAddr;
        socklen_t clientAddrLen = sizeof(clientAddr);
        
        ssize_t bytesReceived = recvfrom(sockfd, buffer, sizeof(buffer), 0, (struct sockaddr*)&clientAddr, &clientAddrLen);
        
        if (bytesReceived == sizeof(calcMessage)) {
            calcMessage* msg = (calcMessage*)buffer;
            if (ntohs(msg->major_version) == 1 && ntohs(msg->minor_version) == 0 &&
                ntohs(msg->protocol) == 17 && ntohl(msg->message) == 0) {
                calcProtocol assignment = generateAssignment();
                ClientInfo client = {clientAddr, clientAddrLen, ntohl(assignment.id), std::chrono::steady_clock::now(), assignment};
                clients[ntohl(assignment.id)] = client;
                
                sendto(sockfd, &assignment, sizeof(assignment), 0, (struct sockaddr*)&clientAddr, clientAddrLen);
                std::cout << "Sent assignment to client" << std::endl;
            }
        } else if (bytesReceived == sizeof(calcProtocol)) {
            calcProtocol* result = (calcProtocol*)buffer;
            int clientId = ntohl(result->id);
            auto it = clients.find(clientId);
            
            if (it != clients.end()) {
                calcMessage response;
                response.major_version = htons(1);
                response.minor_version = htons(0);
                response.protocol = htons(17);
                response.type = htons(2);
                
                if (verifyResult(it->second.assignment, *result)) {
                    response.message = htonl(1);  // OK
                    std::cout << "Client " << clientId << " provided correct result" << std::endl;
                } else {
                    response.message = htonl(2);  // NOT OK
                    std::cout << "Client " << clientId << " provided incorrect result" << std::endl;
                }
                
                sendto(sockfd, &response, sizeof(response), 0, (struct sockaddr*)&it->second.addr, it->second.addrLen);
                clients.erase(it);
            } else {
                calcMessage errorResponse;
                errorResponse.major_version = htons(1);
                errorResponse.minor_version = htons(0);
                errorResponse.protocol = htons(17);
                errorResponse.type = htons(2);
                errorResponse.message = htonl(2);  // NOT OK
                sendto(sockfd, &errorResponse, sizeof(errorResponse), 0, (struct sockaddr*)&clientAddr, clientAddrLen);
                std::cout << "Rejected result from unknown or timed-out client" << std::endl;
            }
        }
    }
    
    close(sockfd);
    return 0;
}
