#include "Bind.h"

Bind::Bind(tcp::socket client_sock, tcp::socket ftp_server_sock, boost::asio::io_context& io_context)
: client_socket(move(client_sock)), ftp_server_socket(move(ftp_server_sock)), acceptor_(io_context, tcp::endpoint(tcp::v4(), 0)) {
    dstport = acceptor_.local_endpoint().port();
    memset(client_buf, 0x00, max_length);
    memset(server_buf, 0x00, max_length);
}

void Bind::do_bind() {
    bind_reply(1);
    do_accept();
}

void Bind::bind_reply(int reply_cnt) {
    auto self(shared_from_this());
    unsigned char p1 = (unsigned char)(dstport / 256);
    unsigned char p2 = (unsigned char)(dstport % 256);
    unsigned char reply[8] = {0, 90, p1, p2, 0, 0, 0, 0};
    memcpy(client_buf, reply, 8);
    boost::asio::async_write(client_socket, boost::asio::buffer(client_buf, 8), [this, self, reply_cnt](boost::system::error_code ec, size_t /*length*/) {
        if (!ec) {
            if (reply_cnt == 1) {
                do_accept();
            }
            else if (reply_cnt == 2) {
                read_client();
                read_server();
            }
        }
        else {
            bind_reply(reply_cnt);
        }
    } );
}

void Bind::do_accept() {
    auto self(shared_from_this());
    acceptor_.async_accept(ftp_server_socket, [this, self](boost::system::error_code ec) {
        if (!ec) {
            bind_reply(2);
            acceptor_.close();
        }
    });
}

void Bind::read_client() {
    auto self(shared_from_this());
    client_socket.async_read_some(boost::asio::buffer(client_buf, max_length), [this, self](boost::system::error_code ec, size_t length) {
        if (!ec) {
            write_server(length);
        }
        else if (ec==boost::asio::error::eof){
            boost::system::error_code ect;
            client_socket.shutdown(boost::asio::ip::tcp::socket::shutdown_receive, ect);
            ftp_server_socket.shutdown(boost::asio::ip::tcp::socket::shutdown_send, ect);

        }
        else{
            read_client();
        }
        // client_socket.close();
    });
}

void Bind::write_client(size_t length) {
    auto self(shared_from_this());
    boost::asio::async_write(client_socket, boost::asio::buffer(server_buf, length), [this, self](boost::system::error_code ec, size_t length) {
        if (!ec) {
            read_server();
        }
        else {
            // ftp_server_socket.close();
        }
    });
}

void Bind::write_server(size_t length) {
    auto self(shared_from_this());
    boost::asio::async_write(ftp_server_socket, boost::asio::buffer(client_buf, length), [this, self](boost::system::error_code ec, size_t length) {
        if (!ec) {
            read_client();
        }
        else {
            // client_socket.close();
        }
    });
}

void Bind::read_server() {
    auto self(shared_from_this());
    ftp_server_socket.async_read_some(boost::asio::buffer(server_buf, max_length),[this, self](boost::system::error_code ec, size_t length) {
        if (!ec) {
            write_client(length);
        }
        else if (ec==boost::asio::error::eof){
            boost::system::error_code ect;
            client_socket.shutdown(boost::asio::ip::tcp::socket::shutdown_receive, ect);
            ftp_server_socket.shutdown(boost::asio::ip::tcp::socket::shutdown_send, ect);
        }
        else {
            read_server();
        }
        // ftp_server_socket.close();
    });
}