#include <stdlib.h>
#include <iostream>
#include <string>
#include <string.h>
#include <vector>
#include <queue>
#include <unordered_map>
#include <algorithm>
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <semaphore.h>
#include <fcntl.h>
#include <sys/stat.h>

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
    ~CommandInfo(){}
};

/* A class that stores some information about each client */
class ClientInfo {
public:    
    pid_t pid;
    int nUserID;
    int nFd;
    char cUserName[21];    // maximum length of user name is 20 
    char cUserIP[16];  // maximum length of user ip address is 3*4+3 = 15
    int nUserPort;
    char cMessage[1025];
    int nFifoSenderID;
    pair <bool, int> userPipeMap[31];
    sem_t MessageSemaphore;
    sem_t UserPipeSemaphore;
};

int shm_id;
int client_id;
ClientInfo *clients;


queue<queue<CommandInfo>> qAllLines;
queue<string> qWholeLines;
int nLineCount = 0;
unordered_map<int, int[2]> numberedPipeMap;


ClientInfo *SharedMemory() {
     /* get shared memory space */
    shm_id = shmget(IPC_PRIVATE, sizeof(ClientInfo)*31, IPC_CREAT | 0666);
    while (shm_id == -1) {
        shm_id= shmget(IPC_PRIVATE, sizeof(ClientInfo)*31, IPC_CREAT | 0666);
    }
    /* attach shared memory	*/
    void *shm_addr = shmat(shm_id, NULL, 0);
    ClientInfo *client = (ClientInfo *) shm_addr;
    return client;
}

void InitializeClient(int id) {
    clients[id].nUserID = id;
    clients[id].nFd = 0;
    strcpy(clients[id].cUserName, "(no name)");
    memset(clients[id].cUserIP, '\0', 16); 
    clients[id].nUserPort = 0;
    memset(clients[id].cMessage, '\0', 1025); 
    clients[id].nFifoSenderID = 0;
    for (int i = 0; i <=30; ++i) {
        clients[id].userPipeMap[i].first = false;
        clients[id].userPipeMap[i].second = -1;
    }
    sem_init(&clients[id].MessageSemaphore, 1, 1);
    sem_init(&clients[id].UserPipeSemaphore, 1, 1);

}

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
            exit(-1);
        }
    }

    /* map protocol name to protocol number */
    /* The getprotobynumber() function returns a protoent structure for the entry from the database that matches the protocol number number. */
    struct protoent *ppe = getprotobyname(protocol);   // pointer to protocol info entry
    if (!ppe) {
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

/* accept a client, set its file descriptor, IP and port, and return its id */
int AcceptClient(int master_socket_fd) {
    struct sockaddr_in client_addr;
    int addr_len = sizeof(client_addr);

    /* accept clients and store their information in client_addr */ 
    int socket_fd = accept(master_socket_fd, (sockaddr *)&client_addr, (socklen_t*) &addr_len);
    while (socket_fd < 0) {
        socket_fd = accept(master_socket_fd, (sockaddr *)&client_addr, (socklen_t*) &addr_len);
    }

    int id;
    for (id = 1; id <= 30; ++id) {
        if(clients[id].nFd == 0) {
            clients[id].nFd = socket_fd;
            strcpy(clients[id].cUserIP, inet_ntoa(client_addr.sin_addr));
            clients[id].nUserPort = (int) ntohs(client_addr.sin_port);
            break;
        }
    }
    // if return 31 means the client should not connect to the server, since there exists 30 clients already.
    if (id == 31) {
        close(socket_fd);
    }
    return id;
}

/* write message into the receiver's message space and send a signal to handle it */
void SendMessage(int receiver_id, string message) {
    sem_wait(&clients[receiver_id].MessageSemaphore);

    strcpy(clients[receiver_id].cMessage, message.c_str());
    kill(clients[receiver_id].pid, SIGUSR1);
}

/* broadcast message to every existing clients */
void BroadcastMessage(string message) {
    for (int id = 1; id <= 30; ++id) {
        if (clients[id].nFd) {
            SendMessage(id, message);
        }
    }
}

void SendPrompt() {
    string shell_prompt = "% ";
    SendMessage(client_id, shell_prompt);
}

void ClientLogIn() {
    /* send welcome message to the client */
    string welcome_message = "****************************************\n** Welcome to the information server. **\n****************************************\n";
    SendMessage(client_id, welcome_message);
    
    /* broadcast login message */
    string login_message = "*** User \'(no name)\' entered from " + string(clients[client_id].cUserIP)  + ":" + to_string(clients[client_id].nUserPort) + ". ***\n";
    BroadcastMessage(login_message);

    SendPrompt();
}

void ClientLogout() {
    /* close the file descriptor of the client */
    close(clients[client_id].nFd);
    string client_name = clients[client_id].cUserName;
    InitializeClient(client_id);

    /* broadcast logout message */
    string sMessage = "*** User \'" + client_name + "\' left. ***\n";
    BroadcastMessage(sMessage);

    /* detatch shared memory */
    shmdt(clients);
}

/* remove "\n" and "\r" from a line */
string RemoveNewLineCharacter(char *buffer) {
    char *bp;
    if((bp = strchr(buffer, '\n')) != NULL)
        *bp = '\0';
    if((bp = strchr(buffer, '\r')) != NULL)
        *bp = '\0';
    string str = buffer;
    return str;
}

/* parse a string by whitespaces, 
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

/* delete the first token of a string */
string DeleteFirstToken(string strInput) {
    size_t white_space = strInput.find(" ");
    strInput = strInput.substr(white_space + 1);
    return strInput;
}

/* signal handler */
void HandleSignal(int signo) {
    /* someone sent a message to the client */
    if (signo == SIGUSR1) {
        send(clients[client_id].nFd, clients[client_id].cMessage, strlen(clients[client_id].cMessage), 0);
        memset(clients[client_id].cMessage, '\0', 1025);
        sem_post(&clients[client_id].MessageSemaphore);
    }
    /* someone use a usser pipe to send somthing to the client */
    else if (signo == SIGUSR2) {
        /* create FIFO */
        string FIFO_path = "user_pipe/fifo._" + to_string(clients[client_id].nFifoSenderID) + "_" + to_string(client_id);
        mkfifo(FIFO_path.c_str(), 0666);

        /* open the created FIFO */
        int fifo_read_fd = open(FIFO_path.c_str(), O_RDONLY);
        clients[client_id].userPipeMap[clients[client_id].nFifoSenderID].second = fifo_read_fd;
    }
    /* ctrl-C */
    else if (signo == SIGINT) {
        /* detatch shared memory */
        shmdt(clients);
        /* set shared memory as to be deleted */
		shmctl(shm_id, IPC_RMID, 0);
        exit(0);
    }
}

/* Check if the line of input is a built-in command. 
   If it is, handle it and return true;
   if not, return false */ 
bool HandleBuiltInCommands(vector<char*> &vecToken, string msg, bool &client_exit) {
    bool isBulitInCommand = false;

    /* exit */
    if (strcmp(vecToken[0], "exit") == 0) {
        client_exit = true;
        isBulitInCommand =  true;
    }
    /* setenv */
    else if (strcmp(vecToken[0], "setenv") == 0) {
        setenv(vecToken[1], vecToken[2], 1);
        isBulitInCommand =  true;
    }
    /* printenv */
    else if (strcmp(vecToken[0], "printenv") == 0) {
        if (const char* cEnvironment = getenv(vecToken[1])) {
            string sMessage = string(cEnvironment) + "\n";
            SendMessage(client_id, sMessage);
        }
        isBulitInCommand =  true;
    }
    else if (strcmp(vecToken[0], "who") == 0) {
        string sMessage = "<ID>\t<nickname>\t<IP:port>\t<indicate me>\n";

        for (int id = 1; id <= 30; ++id) {
            if (clients[id].nFd) {
                sMessage += to_string(id) + "\t" + clients[id].cUserName + "\t" + string(clients[id].cUserIP) + ":" + to_string(clients[id].nUserPort);
                if (id == client_id) {
                    sMessage += "\t<-me";
                }
                sMessage += "\n";
            }
        }
        SendMessage(client_id, sMessage);
        isBulitInCommand =  true;
    }
    else if (strcmp(vecToken[0], "name") == 0) {
        bool name_repeat = false;
        for (int id = 1; id <= 30; ++id) {
            if (strcmp(clients[id].cUserName, vecToken[1]) == 0) {
                name_repeat = true;
                break;
            }
        }

        string sMessage;
        /* if the the name is used already */
        if (name_repeat) {
            sMessage = "*** User \'" + string(vecToken[1]) + "\' already exists. ***\n";
            SendMessage(client_id, sMessage);
        }
        /* if the the name is available */
        else {
            strcpy(clients[client_id].cUserName, vecToken[1]);
            sMessage = "*** User from " + string(clients[client_id].cUserIP) + ":" + to_string(clients[client_id].nUserPort) + " is named \'" + string(vecToken[1]) + "\'. ***\n";
            BroadcastMessage(sMessage);
        }
        
        isBulitInCommand =  true;
    }
    else if (strcmp(vecToken[0], "tell") == 0) {       
        msg = DeleteFirstToken(msg);

        int receiver_id= atoi(vecToken[1]);
        string sMessage;
        /* if the receiver exists */
        if (clients[receiver_id].nFd) {
            sMessage = "*** " + string(clients[client_id].cUserName) + " told you ***: " + msg + "\n";
            SendMessage(receiver_id, sMessage);
        }
        /* if the receiver does not exist */
        else {
            sMessage = "*** Error: user #" + string(vecToken[1]) + " does not exist yet. ***\n";
            SendMessage(client_id, sMessage);
        }
        
        isBulitInCommand = true;
    }
    else if (strcmp(vecToken[0], "yell") == 0) {
        string sMessage = "*** " + string(clients[client_id].cUserName) + " yelled ***: " + msg + "\n";
        BroadcastMessage(sMessage);

        isBulitInCommand = true;
    }

    return isBulitInCommand;
}

/* Store the information of each commands into a class, 
   and also split commands into lines by numbered pipe and store them into a queue.
   Then return a queue that stores commands in it */
void PreprocessCommands(vector<char*> &vecToken, string strWholeLine) {
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
                qAllLines.push(qLineOfCommands);
                qLineOfCommands = queue<CommandInfo>();
                qWholeLines.push(strWholeLine);
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
        qAllLines.push(qLineOfCommands);
        qWholeLines.push(strWholeLine);
    }
}

void ClosePipe(int pipe[2]) {
    close(pipe[0]);
    close(pipe[1]);
}

/* Get a line of commands out from the queue, 
   set every thing and execute them */
void ExecuteCommands() {
    while(qAllLines.size()) {
        queue<CommandInfo> qALine = qAllLines.front();  // take a line of commands out
        qAllLines.pop();
        string commandLine = qWholeLines.front();
        qWholeLines.pop();

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
                if (numberedPipeMap.find(nLineCount+command.nPipe) ==  numberedPipeMap.end()) {
                    /* declare file descriptor and create a pipe */
                    int pfd[2];
                    int create_pipe = pipe(pfd);
                    while (create_pipe < 0) {
                        create_pipe = pipe(pfd);
                    }

                    /* save who should attach to the pipe and the information of the pipe in numberedPipeMap */
                    numberedPipeMap[nLineCount+command.nPipe][0] = pfd[0];
                    numberedPipeMap[nLineCount+command.nPipe][1] = pfd[1];
                }

                /* set errOutPipe */
                if (command.bError) {   // error should transfer to other command as well, i.e. !
                    command.errOutPipe[0] = numberedPipeMap[nLineCount+command.nPipe][0];
                    command.errOutPipe[1] = numberedPipeMap[nLineCount+command.nPipe][1];
                }

                /* set outPipe */
                command.outPipe[0] = numberedPipeMap[nLineCount+command.nPipe][0];
                command.outPipe[1] = numberedPipeMap[nLineCount+command.nPipe][1];
            }

            /* check if any numbered pipe should attach to this command */
            if (numberedPipeMap.find(nLineCount) !=  numberedPipeMap.end()) {
                command.inPipe[0] = numberedPipeMap[nLineCount][0];
                command.inPipe[1] = numberedPipeMap[nLineCount][1];
            }

            /* check if any user pipe should attach to this command */
            if (command.nUserPipeInID) {
                /* if the sender does not exist */
                if (command.nUserPipeInID > 30 || clients[command.nUserPipeInID].nFd == 0) {
                    string sMessage = "*** Error: user #" + to_string(command.nUserPipeInID) + " does not exist yet. ***\n";
                    SendMessage(client_id, sMessage);
                    command.nUserPipeInID = -1;
                }
                /* if the send user pipe does not exist */
                else if (clients[client_id].userPipeMap[command.nUserPipeInID].first == false) {
                    string sMessage = "*** Error: the pipe #" + to_string(command.nUserPipeInID) + "->#" + to_string(clients[client_id].nUserID) + " does not exist yet. ***\n";
                    SendMessage(client_id, sMessage);
                    command.nUserPipeInID = -1;
                }
                /* if the send user pipe exists */
                else {
                    /* broadcast message */
                    string sMessage = "*** " + string(clients[client_id].cUserName) + " (#" + to_string(clients[client_id].nUserID) + ") just received from " + string(clients[command.nUserPipeInID].cUserName) + " (#" + to_string(command.nUserPipeInID) + ") by '" + commandLine + "\' ***\n";
                    BroadcastMessage(sMessage);              
                }
            }
            
            /* check if a user pipe after this command */
            if (command.nUserPipeOutID) {
                int receiver_id = command.nUserPipeOutID;
                /* if the receiver does not exist */ 
                if (receiver_id > 30 || clients[receiver_id].nFd == 0) {
                    string sMessage = "*** Error: user #" + to_string(receiver_id) + " does not exist yet. ***\n";
                    SendMessage(client_id, sMessage);
                    command.nUserPipeOutID = -1;
                }
                else {
                    /* if the user pipe exists already */
                    if (clients[receiver_id].userPipeMap[client_id].first == true) {
                        string sMessage = "*** Error: the pipe #" + to_string(client_id) + "->#" + to_string(receiver_id) + " already exists. ***\n";
                        SendMessage(client_id, sMessage);
                        command.nUserPipeOutID = -1;
                    }
                    else {
                        /* broadcast user pipe sent message */
                        string sMessage = "*** " + string(clients[client_id].cUserName) + " (#" + to_string(client_id) + ") just piped \'" +  commandLine + "\' to " + string(clients[command.nUserPipeOutID].cUserName) + " (#" + to_string(command.nUserPipeOutID) + ") ***\n";
                        BroadcastMessage(sMessage);

                        /* change the receiver's userpipe receiver's information */
                        sem_wait(&clients[receiver_id].UserPipeSemaphore);
                        clients[receiver_id].nFifoSenderID = client_id;
                        clients[receiver_id].userPipeMap[client_id].first = true;
                        // sem_post(&clients[receiver_id].UserPipeSemaphore);

                        /* send a signal to the receiver */
                        kill(clients[receiver_id].pid, SIGUSR2);
                        sem_post(&clients[receiver_id].UserPipeSemaphore);

                        /* create FIFO */
                        string FIFO_path = "user_pipe/fifo._" + to_string(client_id) + "_" + to_string(receiver_id);
                        mkfifo(FIFO_path.c_str(), 0666);
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
                
                if (command.nUserPipeInID > 0) {
                    /* change input to FIFO */
                    int fifo_read_fd = clients[client_id].userPipeMap[command.nUserPipeInID].second;
                    dup2(fifo_read_fd, STDIN_FILENO);

                    sem_wait(&clients[client_id].UserPipeSemaphore);
                    for(int i = 0; i <= 30; ++i) {
                        if (clients[client_id].userPipeMap[i].first == true) {
                            close(clients[client_id].userPipeMap[i].second);
                        }
                    }
                    clients[client_id].userPipeMap[command.nUserPipeInID].first = false;
                    sem_post(&clients[client_id].UserPipeSemaphore); 
                }
                /* if the user pipe does not exists*/
                if (command.nUserPipeInID == -1) {
                    int read_fd = open("/dev/null", O_RDONLY);
                    dup2(read_fd, STDIN_FILENO);
                    close(read_fd);
                }

                /* duplicate file descriptor of stdout as the file descriptor of the client socket */
                if (command.nUserPipeOutID == 0) {
                    dup2(clients[client_id].nFd, STDOUT_FILENO);
                    dup2(clients[client_id].nFd, STDERR_FILENO);
                }
                else if (command.nUserPipeOutID > 0) {
                    /* change output to FIFO */
                    string FIFO_path = "user_pipe/fifo._" + to_string(client_id) + "_" + to_string(command.nUserPipeOutID);
                    int fifo_write_fd = open(FIFO_path.c_str(), O_WRONLY);
                    dup2(fifo_write_fd, STDOUT_FILENO);
                    close(fifo_write_fd);
                }
                /* if the user pipe does not exist */
                else if (command.nUserPipeOutID == -1) {
                    int write_fd = open("/dev/null", O_WRONLY);
                    dup2(write_fd, STDOUT_FILENO);
                    dup2(clients[client_id].nFd, STDERR_FILENO);
                    close(write_fd);
                }


                /* if there is a pipe before this command, duplicate the file descriptor, and deallocate the file descriptor */
                if (command.inPipe[0] != 0) {
                    dup2(command.inPipe[0], STDIN_FILENO);
                    ClosePipe(command.inPipe);
                }
                /* if there is a pipe after this command duplicate the file descriptor, and deallocate the file descriptor */
                if (command.outPipe[1] != 0) {
                    /* if there is a pipe for stderr after this command, duplicate the file descriptor */
                    if (command.errOutPipe[1] != 0) {
                        dup2(command.errOutPipe[1], STDERR_FILENO);
                    }
                    dup2(command.outPipe[1], STDOUT_FILENO);
                    ClosePipe(command.outPipe);
                }
                /* deallocate the file descriptors stored in numberedPipeMap */
                for (auto pipe: numberedPipeMap) {
                    ClosePipe(pipe.second);
                }

                /* if the output should be write into a file */
                if (command.nFileRedirection == 1) {
                    freopen(command.cWriteFile, "w", stdout);
                }

                /* detatch shared memory */
                shmdt(clients);
                
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
                numberedPipeMap.erase(nLineCount);

                /* if there is a input pipe, deallocate the file descriptor */
                if (command.inPipe[0] != 0) { 
                    ClosePipe(command.inPipe);
                }

                if (command.nUserPipeInID > 0) {
                    close(clients[client_id].userPipeMap[command.nUserPipeInID].second);
                }

                /* if there is any pipe (including ordinary pipe, numbered pipe and user pipe) after the command, don't wait */
                if (command.nPipe || command.nUserPipeOutID > 0) {
                    signal(SIGCHLD,SIG_IGN);
                }
                /* if there is no pipe after the command, which means this is the last command in the line, wait */
                else {
                    wait(&status);
                }
                // ~command();
            }
        }
        ++nLineCount;
    }
}


int main (int argc, const char *argv[]) {
    /* create the directory for user pipes */
    mkdir("user_pipe", 0777);

    /* set the environment variable to bin:. */
    setenv("PATH", "bin:.", 1);

    /* set signal handler for signals */
    signal(SIGUSR1, HandleSignal);
    signal(SIGUSR2, HandleSignal);
    signal(SIGINT, HandleSignal);

    /* create shared memory */
    clients = SharedMemory();
    /* initialize clients' informations */
    for (int i = 0; i <= 30; ++i) {
        InitializeClient(i);
    }

    /* set sevice as the port number */
    char *service = (char*) argv[1];
    /* create master server socket */
    int master_socket_fd = PassiveSock(service, "tcp", 30);

    while (1) {
        /* accept a new client */
        client_id = AcceptClient(master_socket_fd);
        /* if there are too many clients */
        if (client_id == 31){
            continue;
        }

        /* Creates a child process */ 
        int status;
        pid_t child_pid = fork();
        while(child_pid < 0) {
            wait(&status);
            child_pid = fork();
        }

        /* if it is child process */
        if (child_pid == 0) {
            /* send welcome message and broadcast login message */
            clients[client_id].pid = getpid();
            ClientLogIn();

            /* handle the input from the client */
            while(1) {
                /* read from the client */
                char receive_buffer[15001];
                memset(receive_buffer, '\0', 15001);                
                int readcount = read(clients[client_id].nFd, receive_buffer, sizeof(receive_buffer));

                /* if the input from the client is eof */
                if (readcount == 0) {
                    ClientLogout();
                    exit(0);
                }
                else if (readcount > 0) {
                    /* delete the new line character */                  
                    string strInput = RemoveNewLineCharacter(receive_buffer);

                    if (strInput == "") {
                        SendPrompt();
                        continue;
                    }

                    vector<char*> vecInputToken = ParseInput(strInput);
                    string strDeleteFirst = DeleteFirstToken(strInput);

                    bool client_exit = false;
                    if (HandleBuiltInCommands(vecInputToken, strDeleteFirst, client_exit)) {
                        if (client_exit) {
                            ClientLogout();
                            exit(0);
                        }    
                        else {
                            SendPrompt();
                            continue;    
                        } 
                    }
                    PreprocessCommands(vecInputToken, strInput);
                    ExecuteCommands();

                    SendPrompt();
                }
            }
        }
        /* if it is parent process */ 
        else {
            /* close the file desciptor of the client */
            close(clients[client_id].nFd);
            signal(SIGCHLD,SIG_IGN);
        }
    }
    return 0;
}