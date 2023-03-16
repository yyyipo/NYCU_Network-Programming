#include <stdlib.h>
#include <iostream>
#include <string>
#include <string.h>
#include <vector>
#include <queue>
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>
#include <unordered_map>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <algorithm>
#include <fcntl.h>


using namespace std;

/* A class that stores some information about each command */
class CommandInfo {
public: 
    vector<char*> vArguments;
    int nPipe = 0;     /* 0: no pipe after this command; -1: an ordinary pipe;  other numbers: a numbered pipe */
    int inPipe[2] = {0, 0}, outPipe[2] = {0, 0}, errOutPipe[2] = {0, 0};
    bool bError = false;
    int nFileRedirection = 0;
    char *cWriteFile;
    int nUserPipeInID = 0;
    int nUserPipeOutID = 0;
    int userInPipe[2] = {0, 0};
    int userOutPipe[2] = {0, 0};
    ~CommandInfo(){}
};

/* A class that stores some information about each client */
class ClientInfo {
public: 
    queue<queue<CommandInfo>> qAllLines;
    queue<string> qWholeLines;
    int nLineCount = 0;
    unordered_map<int, int[2]> numberedPipeMap;
    unordered_map<int, int[2]> userPipeMap; // first: sender id; second: pipe fd
    
    int nUserID;
    int nFd;
    string sUserName;
    string sUserIP;
    int nUserPort;
    unordered_map<string, string> envVar; 
};

/* an array that records the ClientInfo of each client */
ClientInfo clients[31];

void InitializeClient(int id) {
    clients[id].qAllLines = queue<queue<CommandInfo>>();
    clients[id].qWholeLines = queue<string>();
    clients[id].nLineCount = 0;
    clients[id].numberedPipeMap.clear();
    clients[id].userPipeMap.clear();

    clients[id].nUserID = id;
    clients[id].nFd = 0;
    clients[id].sUserName = "(no name)";
    clients[id].sUserIP = ""; 
    clients[id].nUserPort = 0;
    clients[id].envVar["PATH"] = "bin:.";
}

/* accept a client, set its file descriptor, IP and port, and return its id */
int AcceptClient(int master_socket_fd, fd_set *afds) {
    struct sockaddr_in client_addr;
    int addr_len = sizeof(client_addr);
    /* accept clients and store their information in client_addr */ 
    int socket_fd = accept(master_socket_fd, (sockaddr *)&client_addr, (socklen_t*) &addr_len);
    while (socket_fd < 0) {
        socket_fd = accept(master_socket_fd, (sockaddr *)&client_addr, (socklen_t*) &addr_len);
    }

    FD_SET(socket_fd, afds);

    int id ;
    for (id = 1; id <= 30; ++id) {
        /* the client id is available */
        if (clients[id].nFd == 0) {  // if no client use the id
            clients[id].nFd = socket_fd;
            clients[id].sUserIP = inet_ntoa(client_addr.sin_addr);
            clients[id].nUserPort = (int) ntohs(client_addr.sin_port);
            break;
        }
    }
    /* too many clients */
    if (id == 31) {
        close(socket_fd);
    }

    return id;
}

void BroadcastMessage(string message) {
    for (int id = 1; id <= 30; ++id) {
        if (clients[id].nFd) {
            send(clients[id].nFd, message.c_str(), message.length(), 0);
        }
    }
}

void SendPrompt(int fd) {
    string shell_prompt = "% ";
    send(fd, shell_prompt.c_str(), shell_prompt.length(), 0);  
}

string RemoveNewLineCharacter(char *buffer) {
    char *bp;
    if((bp = strchr(buffer, '\n')) != NULL)
        *bp = '\0';
    if((bp = strchr(buffer, '\r')) != NULL)
        *bp = '\0';
    string str = buffer;
    return str;
}

/* delete the first token from the input string */
string DeleteFirstToken(string strInput) {
    size_t white_space = strInput.find(" ");
    strInput = strInput.substr(white_space + 1);
    return strInput;
}

/* parse the input line by whitespaces, 
   then return a vector with tokens in it */
vector<char*> ParseInput(string strInput) {
    vector<char*> vecToken;
    char *str;
    strcpy(str, strInput.c_str());
    char *cToken = strtok(str, " ");
    while (cToken != NULL) {
        char *cTokenCpy = new char[strlen(cToken)];
        strcpy(cTokenCpy, cToken);	
        vecToken.push_back(cTokenCpy);
        cToken = (char*)strtok(NULL, " ");
    }
    return vecToken;
}

void SetClientEnv(int id) {
    unordered_map<string, string> envMap = clients[id].envVar;
    /* set all the environment variables of the client */
    for(auto env: envMap) {
        setenv(env.first.c_str(), env.second.c_str(), 1);
    }
};

void UnsetClientEnv(int id) {
    unordered_map<string, string> envMap = clients[id].envVar;
    /* unset all the environment variables of the client */
    for(auto env: envMap) {
        unsetenv(env.first.c_str());
    }
};


int PassiveSock(const char *service, const char *protocol, int qlen) {
    struct sockaddr_in server_addr;   // an internet endpoint addr
    bzero((char *)&server_addr, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;

    //map service name to port number
    /* The getservbyname() function returns a servent structure for the entry from the database that matches the service name using protocol proto. */
    struct servent *pse = getservbyname(service, protocol);   // pointer to service info entry
    if (pse) {  // if pse is not a null pointer
        server_addr.sin_port = htons(ntohs((u_short) pse -> s_port));
    }
    else {
        server_addr.sin_port = htons((u_short)atoi(service));
        if (server_addr.sin_port == 0) {
            // perror("service entry");
            exit(-1);
        }
    }

    /* map protocol name to protocol number */
    /* The getprotobynumber() function returns a protoent structure for the entry from the database that matches the protocol number number. */
    struct protoent *ppe = getprotobyname(protocol);   // pointer to protocol info entry
    if (!ppe) {
        // perror("protocol entry");
        exit(-1);
    }

    /* use protocol to choose a socket type */
    int socket_type;    // socket type
    /* tcp */
    if (strcmp(protocol, "tcp") == 0) {
        socket_type = SOCK_STREAM;
    }
    /* udp */
    else {
        socket_type = SOCK_DGRAM;
    } 

    /* allocate a socket */
    int socket_fd = socket(PF_INET, socket_type, ppe->p_proto);
    if (socket_fd < 0) {
        exit(-1);
    }
    const int enable = 1;
    setsockopt(socket_fd, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(int));

    /* bind the socket */
    if (bind(socket_fd, (struct sockaddr*) &server_addr, sizeof(server_addr)) < 0) {
        // perror("bind");
        exit(-1);
    }

    /* listen */
    if (socket_type == SOCK_STREAM && listen(socket_fd, qlen) < 0) {
        // perror("listen");
        exit(-1);
    }

    return socket_fd;
}

/* Check if the line of input is a built-in command. 
   If it is, handle it and return true;
   if not, return false */ 
bool HandleBuiltInCommands(vector<char*> &vecToken, string msg, bool &client_exit, int client_id) {
    bool isBulitInCommand = false;
    int client_fd = clients[client_id].nFd;

    /* exit */
    if (strcmp(vecToken[0], "exit") == 0) {
        client_exit = true;
        isBulitInCommand =  true;
    }
    /* setenv */
    else if (strcmp(vecToken[0], "setenv") == 0) {
        setenv(vecToken[1], vecToken[2], 1);
        clients[client_id].envVar[string(vecToken[1])] = string(vecToken[2]);
        isBulitInCommand =  true;
    }
    /* printenv */
    else if (strcmp(vecToken[0], "printenv") == 0) {
        if (const char* cEnvironment = getenv(vecToken[1])) {
            string sMessage = string(cEnvironment) + "\n";
            send(client_fd, sMessage.c_str(), sMessage.length(), 0);
        }
        isBulitInCommand =  true;
    }
    else if (strcmp(vecToken[0], "who") == 0) {
        string sMessage = "<ID>\t<nickname>\t<IP:port>\t<indicate me>\n";

        for (int id = 1; id <= 30; ++id) {
            /* if the client exists */
            if (clients[id].nFd) {
                sMessage += to_string(id) + "\t" + clients[id].sUserName + "\t" + clients[id].sUserIP + ":" + to_string(clients[id].nUserPort);
                if (id == client_id) {
                    sMessage += "\t<-me";
                }
                sMessage += "\n";
            }
        }
        send(client_fd, sMessage.c_str(), sMessage.length(), 0);
        isBulitInCommand =  true;
    }
    else if (strcmp(vecToken[0], "name") == 0) {
        string new_name = vecToken[1];
        bool name_repeat = false;
        for (int id = 1; id <= 30; ++id) {
            if (clients[id].nFd && clients[id].sUserName == new_name) {
                name_repeat = true;
                break;
            }
        }

        string sMessage;
        /* if the the name is used already */
        if (name_repeat) {
            sMessage = "*** User \'" + new_name + "\' already exists. ***\n";
            send(client_fd, sMessage.c_str(), sMessage.length(), 0);
        }
        /* if the the name is available */
        else {
            clients[client_id].sUserName = new_name;
            sMessage = "*** User from " + clients[client_id].sUserIP + ":" + to_string(clients[client_id].nUserPort) + " is named \'" + new_name + "\'. ***\n";
            BroadcastMessage(sMessage);
        }
        
        isBulitInCommand =  true;
    }
    else if (strcmp(vecToken[0], "tell") == 0) {
        int nReceiverID = atoi(vecToken[1]);
        string sMessage;
        /* if the receiver exists */
        if (clients[nReceiverID].nFd) {
            msg = DeleteFirstToken(msg);
            sMessage = "*** " + clients[client_id].sUserName + " told you ***: " + msg + "\n";
            send(clients[nReceiverID].nFd, sMessage.c_str(), sMessage.length(), 0);
        }
        /* if the receiver does not exist */
        else {
            sMessage = "*** Error: user #" + string(vecToken[1]) + " does not exist yet. ***\n";
            send(clients[client_id].nFd, sMessage.c_str(), sMessage.length(), 0);
        }
        
        isBulitInCommand = true;
    }
    else if (strcmp(vecToken[0], "yell") == 0) {
        int nReceiverID= atoi(vecToken[1]);
        string sMessage;
        sMessage = "*** " + clients[client_id].sUserName + " yelled ***: " + msg + "\n";
        BroadcastMessage(sMessage);

        isBulitInCommand = true;
    }

    return isBulitInCommand;
}

/* Store the information of each commands into a class, 
   and also split commands into lines by numbered pipe and store them into a queue.
   Then return a queue that stores commands in it */
void PreprocessCommands(int client_id, vector<char*> &vecToken, string strWholeLine) {

    CommandInfo *readCommand = new CommandInfo();
    queue<CommandInfo> qLineOfCommands;
    string::iterator iter;
    for (auto token: vecToken) {
        /* if it is an ordinary pipe or a numbered pipe */
        if (token[0] == '|' || token[0] == '!') {
            /* ordinary pipe */
            if (strlen(token) == 1) {
                readCommand->nPipe = -1;    // set nPipe as -1 to represent there is an ordinary pipe after a command
                qLineOfCommands.push(*readCommand);
                readCommand = new CommandInfo();
            }
            /* numbered pipe */
            else {
                if (token[0] == '!') {
                    readCommand->bError = true; // set bError as true to know that stderr should be piped
                }
                readCommand->nPipe = atoi(token+1);    // set nPipe as the number of numbered pipe
                qLineOfCommands.push(*readCommand);
                readCommand = new CommandInfo();
                clients[client_id].qAllLines.push(qLineOfCommands);
                qLineOfCommands = queue<CommandInfo>();
                clients[client_id].qWholeLines.push(strWholeLine);
            } 
        }
        /* file redirection */
        else if(strcmp(token, ">") == 0) {
            readCommand->nFileRedirection = 1;   // set nFileRedirection as 1 to know that the output should be redirected to a file
        }
        /* the file name of file redirection */
        else if(readCommand->nFileRedirection) {
            readCommand->cWriteFile = token;    // set cWriteFile as the file name
        }
        /* user pipe out */
        else if (token[0] == '>') {
            int id = atoi(token+1);
            readCommand->nUserPipeOutID = id;
        }
         /* user pipe in */
        else if (token[0] == '<') {
            int id = atoi(token+1);
            readCommand->nUserPipeInID = id;
        }
        /* command */
        else {
            readCommand->vArguments.push_back(token);  // put the token into vArguments
        }
    }
    /* the last command in a line */
    if (readCommand->vArguments.size()) {
        qLineOfCommands.push(*readCommand);
        clients[client_id].qAllLines.push(qLineOfCommands);
        clients[client_id].qWholeLines.push(strWholeLine);
    }
}

/* Get a line of commands out from the queue, 
   set every thing and execute them */
void ExecuteCommands(int client_id) {
    while(clients[client_id].qAllLines.size()) {
        queue<CommandInfo> qALine = clients[client_id].qAllLines.front();  // take a line of commands out
        clients[client_id].qAllLines.pop();
        string commandLine = clients[client_id].qWholeLines.front();
        clients[client_id].qWholeLines.pop();

        while(qALine.size()) {
            CommandInfo command = qALine.front();   // take a command out of the line
            qALine.pop();

            /* if there is an ordinary pipe after the command */
            if (command.nPipe == -1) {  
                /* declare file descriptor and create a pipe */
                int pfd[2];
                int create_pipe = pipe(pfd);
                while (create_pipe < 0) {
                    create_pipe = pipe(pfd);
                }

                /* set outPipe */ 
                command.outPipe[0] = pfd[0];
                command.outPipe[1] = pfd[1];

                /* set inPipe of the next command */
                CommandInfo *nextCommand = &qALine.front();
                nextCommand->inPipe[0] = pfd[0];
                nextCommand->inPipe[1] = pfd[1];
            }
            /* if there is a numbered pipe after the command */
            else if (command.nPipe) {
                /* if the line does not exist in the map */
                if (clients[client_id].numberedPipeMap.find(clients[client_id].nLineCount+command.nPipe) ==  clients[client_id].numberedPipeMap.end()) {
                    /* declare file descriptor and create a pipe */
                    int pfd[2];
                    int create_pipe = pipe(pfd);
                    while (create_pipe < 0) {
                        create_pipe = pipe(pfd);
                    }

                    /* save who should attach to the pipe and the information of the pipe in numberedPipeMap */
                    clients[client_id].numberedPipeMap[clients[client_id].nLineCount+command.nPipe][0] = pfd[0];
                    clients[client_id].numberedPipeMap[clients[client_id].nLineCount+command.nPipe][1] = pfd[1];
                }

                /* set errOutPipe */
                if (command.bError) {   // error should transfer to other command as well, i.e. !
                    command.errOutPipe[0] = clients[client_id].numberedPipeMap[clients[client_id].nLineCount+command.nPipe][0];
                    command.errOutPipe[1] = clients[client_id].numberedPipeMap[clients[client_id].nLineCount+command.nPipe][1];
                }

                /* set outPipe */
                command.outPipe[0] = clients[client_id].numberedPipeMap[clients[client_id].nLineCount+command.nPipe][0];
                command.outPipe[1] = clients[client_id].numberedPipeMap[clients[client_id].nLineCount+command.nPipe][1];
            }

            /* check if any numbered pipe should attach to this command */
            if (clients[client_id].numberedPipeMap.find(clients[client_id].nLineCount) !=  clients[client_id].numberedPipeMap.end()) {
                command.inPipe[0] = clients[client_id].numberedPipeMap[clients[client_id].nLineCount][0];
                command.inPipe[1] = clients[client_id].numberedPipeMap[clients[client_id].nLineCount][1];
            }

            /* check if any user pipe should attach to this command */
            if (command.nUserPipeInID) {

                if (command.nUserPipeInID > 30 || clients[command.nUserPipeInID].nFd == 0) {
                    string sMessage = "*** Error: user #" + to_string(command.nUserPipeInID) + " does not exist yet. ***\n";
                    send(clients[client_id].nFd, sMessage.c_str(), sMessage.length(), 0);
                    command.nUserPipeInID = -1;
                }
                /* if the send user pipe does not exist */
                else if (clients[client_id].userPipeMap.find(command.nUserPipeInID) ==  clients[client_id].userPipeMap.end()) {
                    string sMessage = "*** Error: the pipe #" + to_string(command.nUserPipeInID) + "->#" + to_string(clients[client_id].nUserID) + " does not exist yet. ***\n";
                    send(clients[client_id].nFd, sMessage.c_str(), sMessage.length(), 0);
                    command.nUserPipeInID = -1;
                }
                /* if the send user pipe exists */
                else {
                    // ClientInfo *sender = clients[command.nUserPipeInID];
                    command.userInPipe[0] = clients[client_id].userPipeMap[command.nUserPipeInID][0];
                    command.userInPipe[1] = clients[client_id].userPipeMap[command.nUserPipeInID][1];

                    string sMessage = "*** " + clients[client_id].sUserName + " (#" + to_string(clients[client_id].nUserID) + ") just received from " + clients[command.nUserPipeInID].sUserName + " (#" + to_string(clients[command.nUserPipeInID].nUserID) + ") by '" + commandLine + "\' ***\n";
                    BroadcastMessage(sMessage);
                }
            }

            /* check if a user pipe after this command */
            if (command.nUserPipeOutID) {
                /* if the user does not exist */
                if (command.nUserPipeOutID > 30 || clients[command.nUserPipeOutID].nFd == 0) {
                    string sMessage = "*** Error: user #" + to_string(command.nUserPipeOutID) + " does not exist yet. ***\n";
                    send(clients[client_id].nFd, sMessage.c_str(), sMessage.length(), 0);
                    command.nUserPipeOutID = -1;
                }
                else {
                    /* if the pipe works */
                    if (clients[command.nUserPipeOutID].userPipeMap.find(clients[client_id].nUserID) ==  clients[command.nUserPipeOutID].userPipeMap.end()) {
                        /* declare file descriptor and create a pipe */
                        int pfd[2];
                        int create_pipe = pipe(pfd);
                        while (create_pipe < 0) {
                            create_pipe = pipe(pfd);
                        }

                        /* set userOutPipe as pfd[1] */
                        command.userOutPipe[0] = pfd[0];
                        command.userOutPipe[1] = pfd[1];
                        /* save the information of the pipe fd in userPipeMap of reciver */
                        clients[command.nUserPipeOutID].userPipeMap[clients[client_id].nUserID][0] = pfd[0];
                        clients[command.nUserPipeOutID].userPipeMap[clients[client_id].nUserID][1] = pfd[1];

                        /* broadcast user pipe sent message */
                        string sMessage = "*** " + clients[client_id].sUserName + " (#" + to_string(clients[client_id].nUserID) + ") just piped \'" +  commandLine + "\' to " + clients[command.nUserPipeOutID].sUserName + " (#" + to_string(clients[command.nUserPipeOutID].nUserID) + ") ***\n";
                        BroadcastMessage(sMessage);

                    }
                    /* if the user pipe exists already */
                    else {
                        string sMessage = "*** Error: the pipe #" + to_string(clients[client_id].nUserID) + "->#" + to_string(command.nUserPipeOutID) + " already exists. ***\n";
                        send(clients[client_id].nFd, sMessage.c_str(), sMessage.length(), 0);
                        command.nUserPipeOutID = -1;
                    } 
                }
            }

            /* fork a child process */
            int status;
            pid_t pid = fork();
            while (pid < 0) {
                wait(&status);
                pid = fork();
            }

            /* if it is a child process */
            if (pid == 0) {
                bool client_exit = false;

                /* duplicate file descriptor of stdout as the file descriptor of the client socket */
                dup2(clients[client_id].nFd, STDOUT_FILENO);
                dup2(clients[client_id].nFd, STDERR_FILENO);

                /* if there is a pipe before this command, duplicate the file descriptor, and deallocate the file descriptor */
                if (command.inPipe[0] != 0) {
                    dup2(command.inPipe[0], STDIN_FILENO);
                    close(command.inPipe[0]);
                    close(command.inPipe[1]);
                }
                /* if there is a pipe after this command duplicate the file descriptor, and deallocate the file descriptor */
                if (command.outPipe[1] != 0) {
                    /* if there is a pipe for stderr after this command, duplicate the file descriptor */
                    if (command.errOutPipe[1] != 0) {
                        dup2(command.errOutPipe[1], STDERR_FILENO);
                    }
                    dup2(command.outPipe[1], STDOUT_FILENO);
                    close(command.outPipe[0]);
                    close(command.outPipe[1]);
                }
                /* deallocate the file descriptors stored in numberedPipeMap */
                for (auto pipe: clients[client_id].numberedPipeMap) {
                    close(pipe.second[0]);
                    close(pipe.second[1]);
                }

                /* if the output should be write into a file */
                if (command.nFileRedirection == 1) {
                    freopen(command.cWriteFile, "w", stdout);
                }
                else if (command.nFileRedirection == 2) {
                    freopen(command.cWriteFile, "a", stdout);
                }

                /* if the output should be redirect to a user pipe */
                /* if the pipe does not exists*/
                if (command.nUserPipeOutID == -1) {
                    // freopen("/dev/null", "w", stdout);
                    int write_fd = open("/dev/null", O_WRONLY);
                    dup2(write_fd, STDOUT_FILENO);
                    dup2(clients[client_id].nFd, STDERR_FILENO);

                    close(write_fd);
                }
                /* if the pipe exists */
                if (command.userOutPipe[1]) {
                    dup2(command.userOutPipe[1], STDOUT_FILENO);
                    close(command.userOutPipe[0]);
                    close(command.userOutPipe[1]);
                }

                /* if the input should be redirect to a user pipe */
                /* if the pipe does not exists*/
                if (command.nUserPipeInID == -1) {
                    // freopen("/dev/null", "r", stdin);
                    int read_fd = open("/dev/null", O_RDONLY);
                    dup2(read_fd, STDIN_FILENO);
                    close(read_fd);
                }
                /* if the pipe exists */
                if (command.userInPipe[0]) {
                    dup2(command.userInPipe[0], STDIN_FILENO);
                    close(command.userInPipe[0]);
                    close(command.userInPipe[1]);
                }
                /* deallocate the file descriptors stored in userPipeMap */
                for (auto pipe: clients[client_id].userPipeMap) {
                    close(pipe.second[0]);
                    close(pipe.second[1]);
                }

                /* execute the command */
                command.vArguments.push_back(NULL);
                char **cArguments = command.vArguments.data();
                if (execvp(cArguments[0], cArguments) == -1) {
                    cerr << "Unknown command: [" << command.vArguments[0] << "]." << endl;
                    exit(0);
                }
            }
            /* if it is a parent process */
            else {
                /* erase the file descriptor of the pipe that is attached to this line from numberedPipeMap */
                clients[client_id].numberedPipeMap.erase(clients[client_id].nLineCount);
                if (command.userInPipe[0]) {
                    clients[client_id].userPipeMap.erase(command.nUserPipeInID);
                }

                /* if there is a input pipe, deallocate the file descriptor */
                if (command.inPipe[0] != 0) { 
                    close(command.inPipe[0]);
                    close(command.inPipe[1]);
                }

                /* if there is a user pipe, deallocate the file descriptor */
                if (command.userInPipe[0] != 0) { 
                    close(command.userInPipe[0]);
                    close(command.userInPipe[1]);
                }

                /* if there is any pipe (including ordinary pipe, numbered pipe and user pipe) after the command, don't wait */
                if (command.nPipe || command.nUserPipeOutID) {
                    signal(SIGCHLD,SIG_IGN);
                }
                /* if there is no pipe after the command, which means this is the last command in the line, wait */
                else {
                    wait(&status);
                }
            }
        }
        ++clients[client_id].nLineCount;
    }
}


int main(int argc, const char *argv[]) {
    /* set the environment variable to bin:. */
    setenv("PATH", "bin:.", 1);

    /* set sevice to the port */
    char *service = (char*) argv[1];
    /* master server socket */
    int master_socket_fd = PassiveSock(service, "tcp", 30);
    
    fd_set afds, rfds; /* active file descriptor set and read file descriptor set */
    /* clear afds and rfds*/
    FD_ZERO(&afds);
    FD_ZERO(&rfds);

    /* add master_socket_fd into afds*/
    FD_SET(master_socket_fd, &afds);

    /* initialize clients' informations */
    for (int id = 0; id <= 30; ++id) {
        InitializeClient(id);
    }

    struct sockaddr_in client_addr;
    int client_len = sizeof(client_addr);
    while(1) {
        /* getdtablesize() returns the maximum number of files a process can open, one more than the largest possible value for a file descriptor. */
        int nfds = getdtablesize();

        /* copy afds to rfds */
        memcpy(&rfds, &afds, sizeof(afds));
        if (select(nfds, &rfds, (fd_set *) 0, (fd_set *) 0, (struct timeval *) 0) < 0) {
            continue;
        }

        /* if master_socket_fd is in rfds, which means someone wants to connect to server */
        if (FD_ISSET(master_socket_fd, &rfds)) {
            int client_id = AcceptClient(master_socket_fd, &afds);
            /* too many clients */
            if (client_id == 31){
                continue;
            }
            
            string welcome_message = "****************************************\n** Welcome to the information server. **\n****************************************\n";
            send(clients[client_id].nFd, welcome_message.c_str(), welcome_message.length(), 0);

             /* broadcast login message */
            string login_message = "*** User \'(no name)\' entered from " + string(clients[client_id].sUserIP)  + ":" + to_string(clients[client_id].nUserPort) + ". ***\n";
            BroadcastMessage(login_message);
            
            /* send a promt to the client */
            SendPrompt(clients[client_id].nFd);
        }
        
        for (int curr_client_id = 1; curr_client_id <= 30; ++curr_client_id) {
            int curr_client_fd = clients[curr_client_id].nFd;
            /* if the user does not exists */
            if (curr_client_fd == 0) {
                continue;
            }
            /* if the user exists */
            /* test whether fd is in rfds or not */
            if (FD_ISSET(curr_client_fd, &rfds)) {
                // ClientInfo *currentClient = vUserIDRecord[curr_client_id];
                /* initialize the receive buffer*/
                char receive_buffer[15001];
                memset(receive_buffer, '\0', 15001);                

                /* read from the client */
                int readcount = read(curr_client_fd, receive_buffer, sizeof(receive_buffer));

                /* if the input from the client is eof */
                if (readcount == 0) {
                    /* close the file descriptor of the client */
                    close(curr_client_fd);
                    /* clear fd from afds */
                    FD_CLR(curr_client_fd, &afds);
                    // vUserIDRecord[currentUserId] = nullptr;
                    string user_name = clients[curr_client_id].sUserName;
                    InitializeClient(curr_client_id);

                    /* broadcast logout message */
                    string sMessage = "*** User \'" + user_name + "\' left. ***\n";
                    BroadcastMessage(sMessage);
                }
                else if (readcount > 0) {
                    /* set the environment of the client */
                    SetClientEnv(curr_client_id);

                    /* delete the new line character */                  
                    string strInput = RemoveNewLineCharacter(receive_buffer);
                    if (strInput == "") {
                        SendPrompt(curr_client_fd);
                        continue;
                    }

                    vector<char*> vecInputToken = ParseInput(strInput);
                    string strDeleteFirst = DeleteFirstToken(strInput);
                    bool client_exit = false;
                    if (HandleBuiltInCommands(vecInputToken, strDeleteFirst, client_exit, curr_client_id)) {
                        if (client_exit) {
                            /* close the file descriptor of the client */
                            close(curr_client_fd);
                            /* clear fd from afds */
                            FD_CLR(curr_client_fd, &afds);
                            // vUserIDRecord[currentUserId] = nullptr;
                            string user_name = clients[curr_client_id].sUserName;
                            InitializeClient(curr_client_id);

                            /* broadcast logout message */
                            string sMessage = "*** User \'" + user_name + "\' left. ***\n";
                            BroadcastMessage(sMessage);

                            break;
                        }    
                        else { 
                            SendPrompt(curr_client_fd);
                            continue;    
                        } 
                    }
                    PreprocessCommands(curr_client_id, vecInputToken, strInput);
                    ExecuteCommands(curr_client_id);

                    SendPrompt(curr_client_fd);
                    UnsetClientEnv(curr_client_id);   
                }        
            }
        }
    }
    close(master_socket_fd);
    FD_CLR(master_socket_fd, &afds);

    return 0;
}