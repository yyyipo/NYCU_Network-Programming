#include <vector>
#include <string>
#include <boost/asio.hpp>

using namespace std;

class Client {
public:
    /* constructor */
    Client(boost::asio::io_context &io_context, string hostname, int port, string file);

    /* variables */
    boost::asio::ip::tcp::socket socket;
	string server_addr;
    int server_port;
	string test_file;
};

class Console {
public:
    // /* constructor */
	Console();
	
    /* fuctions */
	void ParseQueryString();
    void LinkServer();
    void Run();
	

private:
    /* functions */
	void PrintHTML();

    /* vairables */
	string query_string;
	vector<Client> clients;
};
