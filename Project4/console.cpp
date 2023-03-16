#include <string>
#include <string.h>
#include <iostream>
#include <fstream>
#include <vector>
#include <queue>
#include <boost/asio.hpp>
#include <boost/algorithm/string/classification.hpp>
#include <boost/algorithm/string/split.hpp>
#include <boost/algorithm/string/replace.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>

using boost::asio::ip::tcp;
using namespace std;

boost::asio::io_context io_context;
tcp::endpoint proxy_endpoint;
bool need_socks_server = false;

vector<string> parse_query_string();
void client_connect_server(vector<string> query_vec, string &html_thead, string &html_tbody);
void concat_html(string &thead, string &tbody, int idx, string hostname, string port);
void print_html(string thead, string tbody);

class Session
 : public enable_shared_from_this<Session> {
private:
    tcp::socket socket_;
    tcp::endpoint endpoint_;
    string index_;
    queue<string> file_content;
    enum { max_length = 1024 };
    char input_data[max_length];

public: 
    Session(tcp::socket socket, tcp::endpoint endpoint, string index , string filename)
    : socket_(move(socket)), endpoint_(endpoint), index_(index) {
        file_content = get_all_command(filename);
    }

    void start() {
        do_connect();
    }

    // void do_connect(tcp::resolver::iterator iter) {
    void do_connect() {
        auto self(shared_from_this());
        if (need_socks_server == false) {
            socket_.async_connect(endpoint_, [this, self](boost::system::error_code ec) {
                if(!ec){
                    do_read();
                }      
            });
        }
        else {
            socket_.async_connect(proxy_endpoint, [this, self](const boost::system::error_code ec){
                if(!ec) {
                    send_connect_request();
                }
                else{
                    // cout << "connect error" << endl;
                    cout << ec.message() << endl;
                }
            });
        }
    }

    void send_connect_request() {
        auto self(shared_from_this());

        unsigned short dstport = endpoint_.port();
        unsigned char port_1 = (unsigned char)(dstport / 256);
        unsigned char port_2 = (unsigned char)(dstport % 256);

        string dstip = endpoint_.address().to_string();
        vector<unsigned char> host = split_dstip(dstip);

        unsigned char request[9] = { 4, 1, port_1, port_2, host[0], host[1], host[2], host[3], 0 };
        memcpy(input_data, request, 9);
        socket_.async_write_some(boost::asio::buffer(input_data, 9), [this, self](boost::system::error_code ec, size_t length) {
            if(!ec) {
                get_reply();
            }
            else{
                // cout << "send_connect_request error" << endl;
                cout << ec.message() << endl;
            }
        });
    }

    void get_reply(){
        auto self(shared_from_this());
        socket_.async_read_some(boost::asio::buffer(input_data, max_length), [this, self](boost::system::error_code ec, size_t length) {
            if (!ec) {
                memset(input_data, '\0', max_length);
                // memset(output, '\0', max_length);
                do_read();
            }
            else{
                // cout << "get_connect_reply error" << endl;
                cout << ec.message() << endl;
            }
        });
    }

    vector<unsigned char> split_dstip(string dstip) {
        vector<string> dstip_split;
        vector<unsigned char> host;
        boost::split(dstip_split, dstip, boost::is_any_of("."), boost::token_compress_on);
        for (int i = 0; i < 4; ++i) {
            host.push_back((unsigned char) stoi(dstip_split[i]));
        }
        return host;
    }

    void do_read() {
        auto self(shared_from_this());
        socket_.async_read_some(boost::asio::buffer(input_data, max_length), [this, self](boost::system::error_code ec, size_t length) {
            if (!ec) {
                string response = string(input_data);
                memset(input_data, '\0', max_length);
                /* print the response from the shell on the screen */
                output_shell(response);
                /* if np_single_golden sends a %, do the next command */
                if(response.find("%") != std::string::npos) {
                    do_write();
                }
                /* something needs to be read */
                else{
                    do_read();
                }
            }
        });
    }

    void do_write() {
        auto self(shared_from_this());
        string command = file_content.front();
        file_content.pop();
        char output_data[max_length];
        strcpy(output_data, command.c_str());
        /* print the command on the screen */
        output_command(command);

        boost::asio::async_write(socket_, boost::asio::buffer(output_data, strlen(output_data)), [this, self](boost::system::error_code ec, size_t /*length*/) {
            if (!ec) {
                do_read();
            }
        });
    }

    void output_shell(string shell_response) {
        html_escape(shell_response);
        cout << "<script>document.getElementById(\'s" + index_ + "\').innerHTML += \'" + shell_response + "\';</script>&NewLine;" << flush;
    }

    void output_command(string command) {
        html_escape(command);
        cout << "<script>document.getElementById(\'s" + index_ + "\').innerHTML += \'<b>" + command + "</b>\';</script>&NewLine;" << flush;
    }

    void html_escape(string& str) {
        boost::replace_all(str, "&", "&amp;");
        boost::replace_all(str, "\"", "&quot;");
        boost::replace_all(str, "\'", "&apos;");
        boost::replace_all(str, "<", "&lt;");
        boost::replace_all(str, ">", "&gt;");
        boost::replace_all(str, "\n", "<br>");
        boost::replace_all(str, "\r", "");
    }

    queue<string> get_all_command(string filename) {
        queue<string> all_lines;
        string line;
        ifstream inFile;
        inFile.open("test_case/" + filename);
        while (getline (inFile, line)) {
            all_lines.push(line+"\n");
        }
        inFile.close(); 
        return all_lines;
    }
};


int main() {
    try {
        string html_thead = "", html_tbody = "";
        vector<string> query_token = parse_query_string(); 
        client_connect_server(query_token, html_thead, html_tbody);
        print_html(html_thead, html_tbody);
        io_context.run();
    }
    catch (exception& e) {
        cerr << e.what() << endl;
    }
}

vector<string> parse_query_string() {
    vector<string> query_vec;
    string query_string = string(getenv("QUERY_STRING"));
    boost::split(query_vec, query_string, boost::is_any_of("&"), boost::token_compress_on);

    return query_vec;
}

void client_connect_server(vector<string> query_vec, string &html_thead, string &html_tbody) {
    /* socks server */

    if (query_vec.size() % 3 != 0) {
        string socks_server_host = query_vec[query_vec.size()-2].substr(3);
        string socks_server_port = query_vec[query_vec.size()-1].substr(3);
        query_vec.erase(query_vec.end()-2, query_vec.end());
        tcp::resolver resolver(io_context);
        tcp::resolver::query query(socks_server_host, socks_server_port);
        tcp::resolver::iterator iter = resolver.resolve(query);
        proxy_endpoint = iter->endpoint();
        need_socks_server = true;
    }

    for (int i = 0; i < 5; ++i) {
        string hostname = "", port = "", file = "";
        /* get the hostname, port and file of the client*/
        hostname = query_vec[i*3].substr(3);
        port = query_vec[i*3+1].substr(3);
        file = query_vec[i*3+2].substr(3);

        /* create a client and push it into the vector */
        if (hostname.length() && port.length() && file.length()) {
            concat_html(html_thead, html_tbody, i, hostname, port);
            
            tcp::resolver resolver(io_context);
            tcp::resolver::query query(hostname, port);
            tcp::resolver::iterator iter = resolver.resolve(query);
            tcp::endpoint endpoint = iter->endpoint();
            tcp::socket socket(io_context);
            make_shared<Session>(move(socket), endpoint, to_string(i), file)->start();
        }
        else{
            break;
        }
    }
}

void concat_html(string &thead, string &tbody, int idx, string hostname, string port){
    thead  += "<th scope=\"col\">" + hostname + ":" + port + "</th>";
    tbody += "<td><pre id = \"s" + to_string(idx) + "\" class=\"mb-0\"></pre></td>";
}

void print_html(string thead, string tbody) {
    cout << "Content-type: text/html\r\n\r\n";
    cout << "<!DOCTYPE html>\
            <html lang=\"en\">\
            <head>\
                <meta charset=\"UTF-8\" />\
                <title>NP Project 4 Sample Console</title>\
                <link\
                rel=\"stylesheet\"\
                href=\"https://cdn.jsdelivr.net/npm/bootstrap@4.5.3/dist/css/bootstrap.min.css\"\
                integrity=\"sha384-TX8t27EcRE3e/ihU7zmQxVncDAy5uIKz4rEkgIXeMed4M0jlfIDPvg6uqKI2xXr2\"\
                crossorigin=\"anonymous\"\
                />\
                <link\
                href=\"https://fonts.googleapis.com/css?family=Source+Code+Pro\"\
                rel=\"stylesheet\"\
                />\
                <link\
                rel=\"icon\"\
                type=\"image/png\"\
                href=\"https://cdn0.iconfinder.com/data/icons/small-n-flat/24/678068-terminal-512.png\"\
                />\
                <style>\
                * {\
                    font-family: 'Source Code Pro', monospace;\
                    font-size: 1rem !important;\
                }\
                body {\
                    background-color: #212529;\
                }\
                pre {\
                    color: #cccccc;\
                }\
                b {\
                    color: #95c4ce;\
                }\
                </style>\
            </head>";
    cout << "<body>\
            <table class=\"table table-dark table-bordered\">\
                <thead>\
                    <tr>";
    cout << thead;
    cout <<         "</tr>\
                </thead>\
                <tbody>\
                    <tr>";
    cout << tbody;
    cout <<         "</tr>\
                </tbody>\
            </table>\
        </body>\
    </html>";
}