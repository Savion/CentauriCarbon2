/***************************************************************************** 
 * @Author       : Jack
 * @Date         : 2025-01-10 11:29:59
 * @LastEditors  : Jack
 * @LastEditTime : 2025-07-15 16:28:01
 * @Description  : 
 * 
 * Copyright (c) 2025 by ELEGOO, All Rights Reserved. 
 *****************************************************************************/
#pragma once
#include <functional>

#include <mutex>
#include <iostream>
#include <memory>
#include <cstring>  // For memset
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>  // For close and unlink

#define SOCKET_FILE "/tmp/aicamera_uds"

namespace znp {

class ClientConnection {
public:
    ClientConnection(int sockfd) : m_sockfd(sockfd) {}
    ~ClientConnection() { close(m_sockfd); }

    bool send(const std::string& message);
    bool receive(std::string& message);

private:
    int m_sockfd;
    static const int MAX_RECEIVE = 4096; // Buffer size for receiving data
};

class ServerSocket {
    using Callback = std::function<void(const std::string&)>;
public:
    ServerSocket(const std::string& socket_path, Callback callback);
    ~ServerSocket();
    
    void acceptConnection(std::shared_ptr<ClientConnection> &m_client_connection);
    bool sendMsg(const std::string& message);
    void serverLoop0();
    void serverLoop1();

private:
    int m_sockfd;
    std::mutex comm_mutex_;
    sockaddr_un m_addr;
    Callback callback_; // 数据处理回调函数
    std::shared_ptr<ClientConnection> m0_client_connection;  // 与客户端0通信
    std::shared_ptr<ClientConnection> m1_client_connection;  // 与客户端1通信

};

}


