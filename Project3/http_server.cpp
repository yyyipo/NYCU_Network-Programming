#include <cstdlib>
#include <stdio.h>
#include <iostream>
#include <memory>
#include <utility>
#include <unistd.h>
#include <sys/wait.h>
#include <string>
#include <vector>
#include <boost/asio.hpp>
#include <boost/filesystem.hpp>
#include <boost/algorithm/string/classification.hpp>
#include <boost/algorithm/string/split.hpp>

using namespace std;
using boost::asio::ip::tcp;

boost::asio::io_context io_context;
std::string status_str = "HTTP/1.0 200 OK\r\n";

class Session
 : public std::enable_shared_from_this<Session> {
public:
    /* constructor */
    Session(tcp::socket socket)
     : socket_(std::move(socket)) {
    }

    /* function */
    void start() {
        do_read();
    }

private:
    /* variables */
    tcp::socket socket_;
    enum { max_length = 15000 };
    char data_[max_length];
    string request_method;
    string request_uri;
    string query_string;
    string server_protocol;
    string http_host;
    string server_addr;
    string server_port;
    string remote_addr;
    string remote_port;

    /* functions */
    void do_read() {
        auto self(shared_from_this()); // auto self = std::make_shared<Session>(shared_from_this());
        socket_.async_read_some(boost::asio::buffer(data_, max_length), [this, self](boost::system::error_code ec, std::size_t length) {
                if (!ec) { 
                    ParseHTTPRequest();
                    do_write(length);
                }
            });
    }

    void do_write(std::size_t length) {
        auto self(shared_from_this());
        boost::asio::async_write(socket_, boost::asio::buffer(status_str, status_str.length()), [this, self](boost::system::error_code ec, std::size_t ) {
            if (!ec) {
                /* fork */
                io_context.notify_fork(boost::asio::io_context::fork_prepare);
                int status;
                pid_t pid = fork();
                while (pid < 0) {
                    wait(&status);
                    pid = fork();
                }
                /* child process */
                if (pid == 0) {
                    io_context.notify_fork(boost::asio::io_context::fork_child);

                    /* set environment variables */
                    SetEnv();
                    /* change the file descriptor and exec */
                    Exec();

                }
                /* parent process */
                else {
                    io_context.notify_fork(boost::asio::io_context::fork_parent);
                    signal(SIGCHLD,SIG_IGN);
                    socket_.close();
                }
            }
        });
    }


    void ParseHTTPRequest() {
        string http_header = string(data_); 
        vector<string> header_lines;
        boost::split(header_lines, http_header, boost::is_any_of("\r\n"), boost::token_compress_on);

        vector<string> header_first_line;
        boost::split(header_first_line, header_lines[0], boost::is_any_of(" "), boost::token_compress_on);
        request_method = header_first_line[0];
        request_uri = header_first_line[1];
        server_protocol = header_first_line[2];
        
        vector<string> split_request_uri;
        boost::split(split_request_uri, request_uri, boost::is_any_of("?"), boost::token_compress_on);
        if (split_request_uri.size() == 2) {
            request_uri = split_request_uri[0];
            query_string = split_request_uri[1];
        }

        vector<string> header_second_line;
        boost::split(header_second_line, header_lines[1], boost::is_any_of(" "), boost::token_compress_on);
        http_host = header_second_line[1];

        server_addr = socket_.local_endpoint().address().to_string();
        server_port = to_string(socket_.local_endpoint().port());
        remote_addr = socket_.remote_endpoint().address().to_string();
        remote_port = to_string(socket_.remote_endpoint().port());

        cout << "======== http request ========" << endl;
        cout << "request_method: " << request_method << endl;
        cout << "request_uri: " << request_uri << endl;
        cout << "query_string: " << query_string << endl;
        cout << "server_protocol: " << server_protocol << endl;
        cout << "http_host: " << http_host << endl;
        cout << "server_addr: " << server_addr << endl;
        cout << "server_port: " << server_port << endl;
        cout << "remote_addr: " << remote_addr << endl;
        cout << "remote_port: " << remote_port << endl;
        cout << endl;
    }

    void SetEnv() {
        setenv("REQUEST_METHOD", request_method.c_str(), 1);
        setenv("REQUEST_URI", request_uri.c_str(), 1);
        setenv("QUERY_STRING", query_string.c_str(), 1);
        setenv("SERVER_PROTOCOL", server_protocol.c_str(), 1);
        setenv("HTTP_HOST", http_host.c_str(), 1);
        setenv("SERVER_ADDR", server_addr.c_str(), 1);
        setenv("SERVER_PORT", server_port.c_str(), 1);
        setenv("REMOTE_ADDR", remote_addr.c_str(), 1);
        setenv("REMOTE_PORT", remote_port.c_str(), 1);
    }

    void Exec() {
        dup2(socket_.native_handle(), STDIN_FILENO);
        dup2(socket_.native_handle(), STDOUT_FILENO);
        dup2(socket_.native_handle(), STDERR_FILENO);
        socket_.close();

        string cgi_file = boost::filesystem::current_path().string() + request_uri;
        if (execlp(cgi_file.c_str(), cgi_file.c_str(), NULL) < 0) {
            cout << "Content-type:text/html\r\n\r\n<h1>exec failure</h1>";
		}
    }
};

class HTTPServer {
public:
    HTTPServer(boost::asio::io_context &io_context, short port)
     : acceptor_(io_context, tcp::endpoint(tcp::v4(), port)) {
            do_accept();
    }

private:
    /* variable */
    tcp::acceptor acceptor_;

    /* function */
    void do_accept() {
        acceptor_.async_accept([this](boost::system::error_code ec, tcp::socket socket) {
            if (!ec) {
                /* create a Session with socket, and pass the socket to the Session */
                std::make_shared<Session>(std::move(socket))->start();  
            }

            do_accept();
        });
    }
};

int main(int argc, char* argv[]) {
    try {
        if (argc != 2) {
            cerr << "Usage: async_tcp_echo_server <port>" << endl;
            return 1;
        }

        HTTPServer server(io_context, atoi(argv[1]));
        io_context.run();
    }
    catch (exception& e) {
        cerr << "Exception: " << e.what() << "\n";
    }
    return 0;
}