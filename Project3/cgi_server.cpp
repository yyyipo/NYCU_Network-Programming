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

using boost::asio::ip::tcp;
using namespace std;

boost::asio::io_context io_context;

std::string status_str = "HTTP/1.0 200 OK\r\nContent-type: text/html\r\n\r\n";

class Client
 : public enable_shared_from_this<Client> {
   public:
    tcp::resolver resolver_;
    tcp::resolver::query query_;
    tcp::socket socket_;
    shared_ptr<tcp::socket> web_socket_;
    string index_;
    queue<string> file_content;
    enum { max_length = 10240 };
    char input_data[max_length];

    
    Client(tcp::resolver::query query, tcp::socket socket, shared_ptr<tcp::socket> web_socket, string index, string file)
    : resolver_(io_context), query_(move(query)), socket_(move(socket)), web_socket_(web_socket), index_(index) {
        file_content = get_all_command(file);
        memset(input_data, '\0', max_length);

    }

    void start() {
        // cout << "client start" << endl;
        auto self(shared_from_this());
        resolver_.async_resolve(query_, [this, self](boost::system::error_code ec, tcp::resolver::iterator iter) {
            if(!ec) {
                do_connect(iter);
            }
        });
    }

    void do_connect(tcp::resolver::iterator iter) {
        // cout << "client do connect" << endl;
        auto self(shared_from_this());
        socket_.async_connect(*iter, [this, self](boost::system::error_code ec) {
            if(!ec){
                do_read();
            }      
        });
    }

    void do_read() {
        // cout << "do read" << endl;
        auto self(shared_from_this());        
        socket_.async_read_some(boost::asio::buffer(input_data, max_length), [this, self](boost::system::error_code ec, size_t length) {
            if (!ec) {
                string response = string(input_data);
                memset(input_data, '\0', max_length);
                /* print the response from the shell on the screen */
                output_shell(response);
                // cout << response << endl;
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
        boost::asio::async_write(socket_, boost::asio::buffer(output_data, strlen(output_data)), [this, self, command](boost::system::error_code ec, size_t /*length*/) {
            if (!ec) {
                do_read();
                if (command.find("exit") != string::npos) {
                    socket_.close();
                    cout << "web_socket_.use_count: " << web_socket_.use_count() << endl;
                    if(web_socket_.use_count() == 2){
                        web_socket_->close();
                    }
                }
                else {
                    do_read();
                }
            }
        });
    }

    void output_shell(string shell_response) {
        // cout << "client output shell" << endl;
        html_escape(shell_response);
        string output_str =  "<script>document.getElementById(\'s" + index_ + "\').innerHTML += \'" + shell_response + "\';</script><br>";
        auto self(shared_from_this());
        boost::asio::async_write(*web_socket_, boost::asio::buffer(output_str, output_str.length()), [this, self](boost::system::error_code ec, size_t /*length*/) {
        });
    }

    void output_command(string command) {
        // cout << "client output command" << endl;
        html_escape(command);
        string output_str =  "<script>document.getElementById(\'s" + index_ + "\').innerHTML += \'<b>" + command + "</b>\';</script><br>";
        auto self(shared_from_this());
        boost::asio::async_write(*web_socket_, boost::asio::buffer(output_str, output_str.length()), [this, self](boost::system::error_code ec, size_t /*length*/) {
        });
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

class Session
 : public std::enable_shared_from_this<Session> {
public:
    /* constructor */
    Session(tcp::socket socket)
     : socket_(move(socket)) {
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
    string html_thead = "", html_tbody = "";
    struct client_info {
        string hostname;
        string port;
        string file;
    };
    vector<client_info> clients;

    /* functions */
    void do_read() {
        auto self(shared_from_this()); // auto self = std::make_shared<Session>(shared_from_this());
        socket_.async_read_some(boost::asio::buffer(data_, max_length), [this, self](boost::system::error_code ec, std::size_t length) {
                if (!ec) { 
                    parse_http_request();
                    /* if the uri is panel.cgi */
                    if(request_uri.find("panel.cgi") != string::npos) {
                        print_panel_html();
                    }
                    /* if the uri is console.cgi */
                    else if(request_uri.find("console.cgi") != string::npos) {
                        do_console();
                    }
                }
            });
    }

    void print_panel_html() {
        auto self(shared_from_this());
        boost::asio::async_write(socket_, boost::asio::buffer(status_str, status_str.length()), [this, self](boost::system::error_code ec, size_t /*length*/) {
            if (!ec) {
                string html = R""""(<!DOCTYPE html>
                                    <html lang="en">
                                    <head>
                                        <title>NP Project 3 Panel</title>
                                        <link
                                        rel="stylesheet"
                                        href="https://cdn.jsdelivr.net/npm/bootstrap@4.5.3/dist/css/bootstrap.min.css"
                                        integrity="sha384-TX8t27EcRE3e/ihU7zmQxVncDAy5uIKz4rEkgIXeMed4M0jlfIDPvg6uqKI2xXr2"
                                        crossorigin="anonymous"
                                        />
                                        <link
                                        href="https://fonts.googleapis.com/css?family=Source+Code+Pro"
                                        rel="stylesheet"
                                        />
                                        <link
                                        rel="icon"
                                        type="image/png"
                                        href="https://cdn4.iconfinder.com/data/icons/iconsimple-setting-time/512/dashboard-512.png"
                                        />
                                        <style>
                                        * {
                                            font-family: 'Source Code Pro', monospace;
                                        }
                                        </style>
                                    </head>
                                    <body class="bg-secondary pt-5">
                                    <form action="console.cgi" method="GET">
                                        <table class="table mx-auto bg-light" style="width: inherit">
                                            <thead class="thead-dark">
                                            <tr>
                                                <th scope="col">#</th>
                                                <th scope="col">Host</th>
                                                <th scope="col">Port</th>
                                                <th scope="col">Input File</th>
                                            </tr>
                                            </thead>
                                            <tbody>
                                            <tr>
                                                <th scope="row" class="align-middle">Session 1</th>
                                                <td>
                                                <div class="input-group">
                                                    <select name="h0" class="custom-select">
                                                    <option></option><option value="nplinux1.cs.nctu.edu.tw">nplinux1</option><option value="nplinux2.cs.nctu.edu.tw">nplinux2</option><option value="nplinux3.cs.nctu.edu.tw">nplinux3</option><option value="nplinux4.cs.nctu.edu.tw">nplinux4</option><option value="nplinux5.cs.nctu.edu.tw">nplinux5</option><option value="nplinux6.cs.nctu.edu.tw">nplinux6</option><option value="nplinux7.cs.nctu.edu.tw">nplinux7</option><option value="nplinux8.cs.nctu.edu.tw">nplinux8</option><option value="nplinux9.cs.nctu.edu.tw">nplinux9</option><option value="nplinux10.cs.nctu.edu.tw">nplinux10</option><option value="nplinux11.cs.nctu.edu.tw">nplinux11</option><option value="nplinux12.cs.nctu.edu.tw">nplinux12</option>
                                                    </select>
                                                    <div class="input-group-append">
                                                    <span class="input-group-text">.cs.nctu.edu.tw</span>
                                                    </div>
                                                </div>
                                                </td>
                                                <td>
                                                <input name="p0" type="text" class="form-control" size="5" />
                                                </td>
                                                <td>
                                                <select name="f0" class="custom-select">
                                                    <option></option>
                                                    <option value="t1.txt">t1.txt</option><option value="t2.txt">t2.txt</option><option value="t3.txt">t3.txt</option><option value="t4.txt">t4.txt</option><option value="t5.txt">t5.txt</option>
                                                </select>
                                                </td>
                                            </tr>
                                            <tr>
                                                <th scope="row" class="align-middle">Session 2</th>
                                                <td>
                                                <div class="input-group">
                                                    <select name="h1" class="custom-select">
                                                    <option></option><option value="nplinux1.cs.nctu.edu.tw">nplinux1</option><option value="nplinux2.cs.nctu.edu.tw">nplinux2</option><option value="nplinux3.cs.nctu.edu.tw">nplinux3</option><option value="nplinux4.cs.nctu.edu.tw">nplinux4</option><option value="nplinux5.cs.nctu.edu.tw">nplinux5</option><option value="nplinux6.cs.nctu.edu.tw">nplinux6</option><option value="nplinux7.cs.nctu.edu.tw">nplinux7</option><option value="nplinux8.cs.nctu.edu.tw">nplinux8</option><option value="nplinux9.cs.nctu.edu.tw">nplinux9</option><option value="nplinux10.cs.nctu.edu.tw">nplinux10</option><option value="nplinux11.cs.nctu.edu.tw">nplinux11</option><option value="nplinux12.cs.nctu.edu.tw">nplinux12</option>
                                                    </select>
                                                    <div class="input-group-append">
                                                    <span class="input-group-text">.cs.nctu.edu.tw</span>
                                                    </div>
                                                </div>
                                                </td>
                                                <td>
                                                <input name="p1" type="text" class="form-control" size="5" />
                                                </td>
                                                <td>
                                                <select name="f1" class="custom-select">
                                                    <option></option>
                                                    <option value="t1.txt">t1.txt</option><option value="t2.txt">t2.txt</option><option value="t3.txt">t3.txt</option><option value="t4.txt">t4.txt</option><option value="t5.txt">t5.txt</option>
                                                </select>
                                                </td>
                                            </tr>
                                            <tr>
                                                <th scope="row" class="align-middle">Session 3</th>
                                                <td>
                                                <div class="input-group">
                                                    <select name="h2" class="custom-select">
                                                    <option></option><option value="nplinux1.cs.nctu.edu.tw">nplinux1</option><option value="nplinux2.cs.nctu.edu.tw">nplinux2</option><option value="nplinux3.cs.nctu.edu.tw">nplinux3</option><option value="nplinux4.cs.nctu.edu.tw">nplinux4</option><option value="nplinux5.cs.nctu.edu.tw">nplinux5</option><option value="nplinux6.cs.nctu.edu.tw">nplinux6</option><option value="nplinux7.cs.nctu.edu.tw">nplinux7</option><option value="nplinux8.cs.nctu.edu.tw">nplinux8</option><option value="nplinux9.cs.nctu.edu.tw">nplinux9</option><option value="nplinux10.cs.nctu.edu.tw">nplinux10</option><option value="nplinux11.cs.nctu.edu.tw">nplinux11</option><option value="nplinux12.cs.nctu.edu.tw">nplinux12</option>
                                                    </select>
                                                    <div class="input-group-append">
                                                    <span class="input-group-text">.cs.nctu.edu.tw</span>
                                                    </div>
                                                </div>
                                                </td>
                                                <td>
                                                <input name="p2" type="text" class="form-control" size="5" />
                                                </td>
                                                <td>
                                                <select name="f2" class="custom-select">
                                                    <option></option>
                                                    <option value="t1.txt">t1.txt</option><option value="t2.txt">t2.txt</option><option value="t3.txt">t3.txt</option><option value="t4.txt">t4.txt</option><option value="t5.txt">t5.txt</option>
                                                </select>
                                                </td>
                                            </tr>
                                            <tr>
                                                <th scope="row" class="align-middle">Session 4</th>
                                                <td>
                                                <div class="input-group">
                                                    <select name="h3" class="custom-select">
                                                    <option></option><option value="nplinux1.cs.nctu.edu.tw">nplinux1</option><option value="nplinux2.cs.nctu.edu.tw">nplinux2</option><option value="nplinux3.cs.nctu.edu.tw">nplinux3</option><option value="nplinux4.cs.nctu.edu.tw">nplinux4</option><option value="nplinux5.cs.nctu.edu.tw">nplinux5</option><option value="nplinux6.cs.nctu.edu.tw">nplinux6</option><option value="nplinux7.cs.nctu.edu.tw">nplinux7</option><option value="nplinux8.cs.nctu.edu.tw">nplinux8</option><option value="nplinux9.cs.nctu.edu.tw">nplinux9</option><option value="nplinux10.cs.nctu.edu.tw">nplinux10</option><option value="nplinux11.cs.nctu.edu.tw">nplinux11</option><option value="nplinux12.cs.nctu.edu.tw">nplinux12</option>
                                                    </select>
                                                    <div class="input-group-append">
                                                    <span class="input-group-text">.cs.nctu.edu.tw</span>
                                                    </div>
                                                </div>
                                                </td>
                                                <td>
                                                <input name="p3" type="text" class="form-control" size="5" />
                                                </td>
                                                <td>
                                                <select name="f3" class="custom-select">
                                                    <option></option>
                                                    <option value="t1.txt">t1.txt</option><option value="t2.txt">t2.txt</option><option value="t3.txt">t3.txt</option><option value="t4.txt">t4.txt</option><option value="t5.txt">t5.txt</option>
                                                </select>
                                                </td>
                                            </tr>
                                            <tr>
                                                <th scope="row" class="align-middle">Session 5</th>
                                                <td>
                                                <div class="input-group">
                                                    <select name="h4" class="custom-select">
                                                    <option></option><option value="nplinux1.cs.nctu.edu.tw">nplinux1</option><option value="nplinux2.cs.nctu.edu.tw">nplinux2</option><option value="nplinux3.cs.nctu.edu.tw">nplinux3</option><option value="nplinux4.cs.nctu.edu.tw">nplinux4</option><option value="nplinux5.cs.nctu.edu.tw">nplinux5</option><option value="nplinux6.cs.nctu.edu.tw">nplinux6</option><option value="nplinux7.cs.nctu.edu.tw">nplinux7</option><option value="nplinux8.cs.nctu.edu.tw">nplinux8</option><option value="nplinux9.cs.nctu.edu.tw">nplinux9</option><option value="nplinux10.cs.nctu.edu.tw">nplinux10</option><option value="nplinux11.cs.nctu.edu.tw">nplinux11</option><option value="nplinux12.cs.nctu.edu.tw">nplinux12</option>
                                                    </select>
                                                    <div class="input-group-append">
                                                    <span class="input-group-text">.cs.nctu.edu.tw</span>
                                                    </div>
                                                </div>
                                                </td>
                                                <td>
                                                <input name="p4" type="text" class="form-control" size="5" />
                                                </td>
                                                <td>
                                                <select name="f4" class="custom-select">
                                                    <option></option>
                                                    <option value="t1.txt">t1.txt</option><option value="t2.txt">t2.txt</option><option value="t3.txt">t3.txt</option><option value="t4.txt">t4.txt</option><option value="t5.txt">t5.txt</option>
                                                </select>
                                                </td>
                                            </tr>
                                            <tr>
                                                <td colspan="3"></td>
                                                <td>
                                                <button type="submit" class="btn btn-info btn-block">Run</button>
                                                </td>
                                            </tr>
                                            </tbody>
                                        </table>
                                        </form>
                                    </body>
                                    </html>)"""";
                boost::asio::async_write(socket_, boost::asio::buffer(html, html.length()), [this, self](boost::system::error_code ec_, size_t /*length*/) { });
            }
        });
    }

    void print_console_html() {
        auto self(shared_from_this());
        boost::asio::async_write(socket_, boost::asio::buffer(status_str, status_str.length()), [this, self](boost::system::error_code ec, size_t /*length*/) {
            if (!ec) {
                string html =  "<!DOCTYPE html>\
                                <html lang=\"en\">\
                                <head>\
                                    <meta charset=\"UTF-8\" />\
                                    <title>NP Project 3 Sample Console</title>\
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
                                </head>\
                                <body>\
                                <table class=\"table table-dark table-bordered\">\
                                    <thead>\
                                        <tr>";
    html += html_thead;
    html +=                            "</tr>\
                                    </thead>\
                                    <tbody>\
                                        <tr>";
    html += html_tbody;
    html +=                            "</tr>\
                                    </tbody>\
                                    </table>\
                                </body>\
                                </html>"; 
                boost::asio::async_write(socket_, boost::asio::buffer(html, html.length()), [this, self](boost::system::error_code ec_, size_t /*length*/) { });
            }
        });
    }

    vector<string> parse_query() {
        vector<string> query_vec;
        boost::split(query_vec, query_string, boost::is_any_of("&"), boost::token_compress_on);
        return query_vec;
    }

    void concat_html(string &thead, string &tbody, int idx, string hostname, string port){
        thead  += "<th scope=\"col\">" + hostname + ":" + port + "</th>";
        tbody += "<td><pre id = \"s" + to_string(idx) + "\" class=\"mb-0\"></pre></td>";
    }

    void set_client(vector<string> query_vec) {
        for (int i = 0; i < 5; ++i) {
            string hostname = "", port = "", file = "";
            hostname = query_vec[i*3].substr(3);
            port = query_vec[i*3+1].substr(3);
            file = query_vec[i*3+2].substr(3);

            if (hostname.length() && port.length() && file.length()) {
                cout << "======== uri ========" << endl;
                cout << "hostname: " << hostname << endl;
                cout << "port: " << port << endl;
                cout << "file: " << file << endl << endl;

                concat_html(html_thead, html_tbody, i, hostname, port);
                client_info new_client;
                new_client.hostname = hostname;
                new_client.port = port;
                new_client.file = file;
                clients.push_back(new_client);
            }
            else {
                break;
            }
        }
    }

    void do_console() { 
        try {
            vector<string> query_token = parse_query();
            set_client(query_token);
            print_console_html();
            // clients_connect_server();
            shared_ptr<tcp::socket> web_socket(&socket_);
            for (long unsigned int i = 0; i < clients.size(); ++i) {
                tcp::resolver::query query(clients[i].hostname, clients[i].port);
                tcp::socket socket(io_context);
                make_shared<Client>(move(query), move(socket), web_socket, to_string(i), clients[i].file)->start();
            }
            io_context.run();
        }
        catch (exception& e) {
            cerr << "Exception: " << e.what() << "\n";
        }
    }

    void parse_http_request() {
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
};

class HTTPServer {
public:
    HTTPServer(boost::asio::io_context& io_context, short port)
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
                make_shared<Session>(move(socket))->start();  
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