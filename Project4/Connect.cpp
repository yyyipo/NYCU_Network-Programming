#include "Connect.h"

connect_operation::connect_operation(tcp::socket client_sock, tcp::socket server_sock, tcp::endpoint endpoint)
: client_socket(move(client_sock)), server_socket(move(server_sock)), endpoint_(endpoint) {
    memset(client_buf, 0x00, max_length);
    memset(server_buf, 0x00, max_length);
}

void connect_operation::do_connect() {
    auto self(shared_from_this());
    server_socket.async_connect(endpoint_, [this, self](const boost::system::error_code& ec){
        if(!ec) {
            connect_reply();
        }
    });
}

void connect_operation::connect_reply() {
    auto self(shared_from_this());
    unsigned char reply[8] = {0, 90, 0, 0, 0, 0, 0, 0};
    memcpy(client_buf, reply, 8);
    boost::asio::async_write(
    client_socket, boost::asio::buffer(client_buf, 8), [this, self](boost::system::error_code ec, size_t /*length*/) {
        if (!ec) {
            read_client();
            read_server();
        }
    });
}

void connect_operation::read_client() {
    auto self(shared_from_this());
    client_socket.async_read_some(boost::asio::buffer(client_buf, max_length), [this, self](boost::system::error_code ec, size_t length) {
        if (!ec) {
            write_server(length);
        }
        else if(ec==boost::asio::error::eof){
            boost::system::error_code ect;
            client_socket.shutdown(boost::asio::ip::tcp::socket::shutdown_receive, ect);
            server_socket.shutdown(boost::asio::ip::tcp::socket::shutdown_send, ect);
        }
        else {
            read_client();
        }
        // client_socket.close();
    });
}

void connect_operation::write_client(size_t length) {
    auto self(shared_from_this());
    boost::asio::async_write(client_socket, boost::asio::buffer(server_buf, length), [this, self](boost::system::error_code ec, size_t length) {
        if (!ec) {
            read_server();
        }
        else {
            // server_socket.close();
        }
    });
}

void connect_operation::write_server(size_t length) {
    auto self(shared_from_this());
    boost::asio::async_write(server_socket, boost::asio::buffer(client_buf, length), [this, self](boost::system::error_code ec, size_t length) {
        if (!ec) {
            read_client();
        }
        else {
            // client_socket.close();
        }
    });
}

void connect_operation::read_server() {
    auto self(shared_from_this());
    server_socket.async_read_some(boost::asio::buffer(server_buf, max_length),[this, self](boost::system::error_code ec, size_t length) {
        if (!ec) {
            write_client(length);
        }
        else if (ec==boost::asio::error::eof){
            boost::system::error_code ect;
            client_socket.shutdown(boost::asio::ip::tcp::socket::shutdown_receive, ect);
            server_socket.shutdown(boost::asio::ip::tcp::socket::shutdown_send, ect);
        }
        else {
            read_server();
        }
        // server_socket.close();
    });
}

