#include <cstdlib>
#include <stdio.h>
#include <iostream>
#include <memory>
#include <string>
#include <vector>
#include <regex>
#include <utility>
#include <unistd.h>
#include <sys/wait.h>
#include <boost/asio.hpp>
#include <boost/filesystem.hpp>
#include <boost/algorithm/string/split.hpp>
#include <boost/algorithm/string/replace.hpp>
#include <boost/algorithm/string/classification.hpp>

#include "Connect.h"
#include "Bind.h"

using namespace std;
using boost::asio::ip::tcp;

class socks 
: public enable_shared_from_this<socks> {
private: 
    tcp::socket client_socket;
    enum { max_length = 15000 };
    unsigned char data_[max_length];
    boost::asio::io_context& io_context;
    struct socks4_request {
        int VN;
        int CD;
        string DSTPORT;
        string DSTIP;
        string USERID;
        string DOMAIN_NAME;
    };
    socks4_request request;
    enum {
        CONNECT = 1,
        BIND = 2
    };
    map<string, vector<string>> firewall_map;

public: 
    socks(tcp::socket socket, boost::asio::io_context& io_context) 
    : client_socket(move(socket)), io_context(io_context) {
    }

    void parse_request() {
        auto self(shared_from_this());
        client_socket.async_read_some(boost::asio::buffer(data_, max_length), [this, self](boost::system::error_code ec, size_t length) {
            if (!ec) {
                request.VN = data_[0];
                request.CD = data_[1];
                request.DSTPORT = to_string(((int)data_[2] * 256) + (int)data_[3]);
                request.DSTIP = get_dstip();
                request.DOMAIN_NAME = get_domain_name(length);

                do_operation();
            }
        });
    }

    string get_dstip() {
        string dstip = "";
        for(int i = 4; i < 8; ++i) {
            dstip += to_string(data_[i]);
            if(i != 7) {
                dstip += ".";
            }
        }
        return dstip;
    }

    string get_domain_name(size_t length) {
        bool is_4a = false;
        string domain_name = "";
        for(size_t i = 8; i < length-1; ++i) {
            if (is_4a) {
                domain_name += data_[i];
            }
            else if(data_[i] == 0 && i != length-1){
                is_4a = true;
            }
        }
        return domain_name;
    }

    void do_operation() {
        get_firewall_rule();

        tcp::socket server_socket(io_context);

        /* connect */
        if(request.CD == CONNECT) {
            string host;
            if(request.DOMAIN_NAME != "") {
                host = request.DOMAIN_NAME;
            }
            else {
                host = request.DSTIP;
            }

            tcp::resolver resolver(io_context);
            tcp::resolver::query query(host, request.DSTPORT);
            tcp::resolver::iterator iter = resolver.resolve(query);
            tcp::endpoint endpoint = iter->endpoint();
            
            bool connect_success = check_firewall();
            if(connect_success) {
                cout << "<S_IP>: " << client_socket.remote_endpoint().address().to_string() << endl;
                cout << "<S_PORT>: " << client_socket.remote_endpoint().port() << endl;
                cout << "<D_IP>: " << endpoint.address().to_string() << endl;
                cout << "<D_PORT>: " << request.DSTPORT << endl;
                cout << "<Command>: " << "CONNECT" << endl;
                cout << "<Reply>: " << "Accept" << endl;
                cout << endl;
                make_shared<connect_operation>(move(client_socket), move(server_socket), endpoint)->do_connect();
            }
            else {
                cout << "<S_IP>: " << client_socket.remote_endpoint().address().to_string() << endl;
                cout << "<S_PORT>: " << client_socket.remote_endpoint().port() << endl;
                cout << "<D_IP>: " << endpoint.address().to_string() << endl;
                cout << "<D_PORT>: " << request.DSTPORT << endl;
                cout << "<Command>: " << "CONNECT" << endl;
                cout << "<Reply>: " << "Reject" << endl;
                cout << endl;
                send_reject();
            }
        }

        /* bind */
        if(request.CD == BIND) {
            bool bind_success = check_firewall();
            if(bind_success) {
                cout << "<S_IP>: " << client_socket.remote_endpoint().address().to_string() << endl;
                cout << "<S_PORT>: " << client_socket.remote_endpoint().port() << endl;
                cout << "<D_IP>: " << request.DSTIP << endl;
                cout << "<D_PORT>: " << request.DSTPORT << endl;
                cout << "<Command>: " << "BIND" << endl;
                cout << "<Reply>: " << "Accept" << endl;
                cout << endl;
                make_shared<Bind>(move(client_socket), move(server_socket), io_context)->do_bind();
            }
            else {
                cout << "<S_IP>: " << client_socket.remote_endpoint().address().to_string() << endl;
                cout << "<S_PORT>: " << client_socket.remote_endpoint().port() << endl;
                cout << "<D_IP>: " << request.DSTIP << endl;
                cout << "<D_PORT>: " << request.DSTPORT << endl;
                cout << "<Command>: " << "BIND" << endl;
                cout << "<Reply>: " << "Reject" << endl;
                cout << endl;
                send_reject();
            }
        }
    }

    void get_firewall_rule() {
        vector<string> firewall = get_file("socks.conf");
        for(size_t i = 0; i < firewall.size(); ++i) {
            vector<string> rule;
            boost::split(rule, firewall[i], boost::is_any_of(" "), boost::token_compress_on);
            string pattern = rule[2];
            boost::replace_all(rule[2], ".", "\\.");
            boost::replace_all(rule[2], "*", ".*");
            firewall_map[rule[1]].push_back(rule[2]);
        }
    }

    vector<string> get_file(string filename) {
        vector<string> all_lines;
        string line;
        ifstream inFile;

        inFile.open(filename);
        while (getline(inFile, line)) {
            all_lines.push_back(line+"\n");
        }
        inFile.close(); 
        return all_lines;
    }

    bool check_firewall() {
        bool success = false;
        string client_ip = client_socket.remote_endpoint().address().to_string();

        /* connect */
        if (request.CD == CONNECT) {
            for (auto ip: firewall_map["c"]) {
                regex ip_pattern(ip);
                if(regex_match(client_ip, ip_pattern)) {
                    success = true;
                    break;
                }
            }
        }
        /* bind */
        else {
            for (auto ip: firewall_map["b"]) {
                regex ip_pattern(ip);
                if(regex_match(client_ip, ip_pattern)) {
                    success = true;
                    break;
                }
            }
        }

        return success;
    }

    void send_reject() {
        auto self(shared_from_this());
        unsigned char reply[8] = {0, 91, 0, 0, 0, 0, 0, 0};
        memcpy(data_, reply, 8);
        boost::asio::async_write(client_socket, boost::asio::buffer(data_, 8), [this, self](boost::system::error_code ec, size_t /*length*/) {
            if (!ec) {
                client_socket.close();
            }
        });
    }
};

class socks_server {
public:
    tcp::acceptor acceptor_;
    boost::asio::io_context& io_context;

    socks_server(boost::asio::io_context& io_context, short port)
    : acceptor_(io_context, tcp::endpoint(tcp::v4(), port)), io_context(io_context) {
        do_accept();
    }

    void do_accept() {
        acceptor_.async_accept([this](boost::system::error_code ec, tcp::socket socket) {
            if (!ec) {
                /* fork a process */
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
                    acceptor_.close();
                    make_shared<socks>(move(socket), io_context)->parse_request();
                }
                /* parent process */
                else{
                    io_context.notify_fork(boost::asio::io_context::fork_parent);
                    signal(SIGCHLD,SIG_IGN);
                    socket.close();
                    do_accept();
                }
            }
        });
    }
};

int main(int argc, char* argv[]) {
    try {
        if (argc != 2) {
            // cerr << "Usage: async_tcp_echo_server <port>" << endl;
            return 1;
        }

        boost::asio::io_context io_context;
        socks_server server(io_context, atoi(argv[1]));
        io_context.run();
    }
    catch (exception& e) {
        // cerr << "Exception: " << e.what() << "\n";
    }
    return 0;
}