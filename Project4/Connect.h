#include <cstdlib>
#include <iostream>
#include <stdio.h>
#include <string>
#include <boost/asio.hpp>

using namespace std;
using boost::asio::ip::tcp;

class connect_operation
 : public enable_shared_from_this<connect_operation> {
private:
    tcp::socket client_socket;
    tcp::socket server_socket;
    tcp::endpoint endpoint_;
    enum { max_length = 15000 };
    unsigned char client_buf[max_length];
    unsigned char server_buf[max_length];

public:
    connect_operation(tcp::socket client_sock, tcp::socket server_sock, tcp::endpoint endpoint);
    void do_connect();
    void connect_reply();
    void start();
    void read_server();
    void read_client();
    void write_server(size_t length);
    void write_client(size_t length);
};