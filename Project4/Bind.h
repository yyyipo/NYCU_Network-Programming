#include <cstdlib>
#include <iostream>
#include <memory>
#include <utility>
#include <stdio.h>
#include <iomanip>
#include <sstream>
#include <string>
#include <string.h>
#include <vector>
#include <boost/asio.hpp>
#include <boost/algorithm/string/classification.hpp>
#include <boost/algorithm/string/split.hpp>

using namespace std;
using boost::asio::ip::tcp;

class Bind : public enable_shared_from_this<Bind> {
private:
    tcp::socket client_socket;
    tcp::socket ftp_server_socket;
    tcp::acceptor acceptor_;
    enum { max_length = 15000 };
    unsigned char client_buf[max_length];
    unsigned char server_buf[max_length];
    unsigned short dstport;

public:
    Bind(tcp::socket client_sock, tcp::socket ftp_server_sock, boost::asio::io_context& io_context);
    void do_bind();
    void bind_reply(int reply_cnt);
    void do_accept();
    void read_server();
    void read_client();
    void write_server(size_t length);
    void write_client(size_t length);

};