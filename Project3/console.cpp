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

class Session
 : public enable_shared_from_this<Session> {
   public:
    tcp::resolver resolver_;
    tcp::socket socket_;
    tcp::resolver::query query_;
    string index_;
    queue<string> file_content;
    enum { max_length = 10240 };
    char input_data[max_length];
    
    Session(tcp::resolver::query query, tcp::socket socket, string index, string file)
    : resolver_(io_context), socket_(move(socket)), query_(move(query)), index_(index) {
        file_content = get_all_command(file);
        memset(input_data, '\0', max_length);
    }

    void start() {
        auto self(shared_from_this());
        resolver_.async_resolve(query_, [this, self](boost::system::error_code ec, tcp::resolver::iterator iter) {
            if(!ec) {
                do_connect(iter);
            }
        });
    }

    void do_connect(tcp::resolver::iterator iter) {
        auto self(shared_from_this());
        socket_.async_connect(*iter, [this, self](boost::system::error_code ec) {
            if(!ec){
                do_read();
            }      
        });
    }

    void do_read() {
        auto self(shared_from_this());
        socket_.async_read_some( boost::asio::buffer(input_data, max_length), [this, self](boost::system::error_code ec, size_t length) {
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
        cout << "<script>document.getElementById(\'s" + index_ + "\').innerHTML += \'" + shell_response + "\';</script>&NewLine;";
    }

    void output_command(string command) {
        html_escape(command);
        cout << "<script>document.getElementById(\'s" + index_ + "\').innerHTML += \'<b>" + command + "</b>\';</script>&NewLine;";
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

void ConcatHTML(string &thead, string &tbody, int idx, string hostname, string port){
    thead  += "<th scope=\"col\">" + hostname + ":" + port + "</th>";
    tbody += "<td><pre id = \"s" + to_string(idx) + "\" class=\"mb-0\"></pre></td>";
}

vector<string> ParseQueryString() {
    vector<string> query_vec;
    string query_string = string(getenv("QUERY_STRING"));
    boost::split(query_vec, query_string, boost::is_any_of("&"), boost::token_compress_on);

    return query_vec;
}

void ClientConnectServer(vector<string> query_vec, string &html_thead, string &html_tbody) {
    for (int i = 0; i < 5; ++i) {
        string hostname = "", port = "", file = "";
        /* get the hostname, port and file of the client*/
        hostname = query_vec[i*3].substr(3);
        port = query_vec[i*3+1].substr(3);
        file = query_vec[i*3+2].substr(3);

        /* create a client and push it into the vector */
        if (hostname.length() && port.length() && file.length()) {
            ConcatHTML(html_thead, html_tbody, i, hostname, port);
            tcp::resolver::query query(hostname, port);
            tcp::socket socket(io_context);
            make_shared<Session>(move(query), move(socket), to_string(i), file)->start();
        }
        else{
            break;
        }
    }

}

void PrintHTML(string thead, string tbody) {
    cout << "Content-type: text/html\r\n\r\n";
    cout << "<!DOCTYPE html>\
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

int main() {
    try {
        string html_thead = "", html_tbody = "";
        vector<string> query_token = ParseQueryString(); 
        ClientConnectServer(query_token, html_thead, html_tbody);
        PrintHTML(html_thead, html_tbody);
        io_context.run();
    }
    catch (exception& e) {
        cerr << e.what() << endl;
    }
}