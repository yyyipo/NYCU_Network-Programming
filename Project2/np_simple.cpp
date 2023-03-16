#include <stdlib.h>
#include <iostream>
#include <string>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <signal.h>
#include <vector>
#include <queue>
#include <unordered_map>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/types.h>
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
    ~CommandInfo(){}
};


/* parse the input line by whitespaces, 
   then return a vector with tokens in it */
vector<char*> ParseInput(string str) {
    vector<char*> vecToken;
    char *cToken = strtok((char*)str.c_str(), " ");
    while (cToken != NULL) {
        char *cTokenCpy = new char[strlen(cToken)];
        strcpy(cTokenCpy, cToken);	
        vecToken.push_back(cTokenCpy);
        cToken = (char*)strtok(NULL, " ");
    }
    return vecToken;
}

int new_socket_fd;

/* Check if the line of input is a built-in command. 
   If it is, handle it and return true;
   if not, return false */ 
bool HandleBuiltInCommands(vector<char*> &vecToken, bool &client_exit) {
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
            strcat((char *)cEnvironment, "\n");
            send(new_socket_fd, cEnvironment, strlen(cEnvironment), 0);
        }
        isBulitInCommand =  true;
    }
    return isBulitInCommand;
}

/* Store the information of each commands into a class, 
   and also split commands into lines by numbered pipe and store them into a queue.
   Then return a queue that stores commands in it */
void PreprocessCommands(queue<queue<CommandInfo>> &qAllLines, int &nLineCount, vector<char*> &vecToken) {
    CommandInfo *readCommand = new CommandInfo();
    queue<CommandInfo> qLineOfCommands;
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
        /* command */
        else {
            readCommand->vArguments.push_back(token);  // put the token into vArguments
        }
    }
    /* the last command in a line */
    if (readCommand->vArguments.size()) {
        qLineOfCommands.push(*readCommand);
        qAllLines.push(qLineOfCommands);
    }
}

/* Get a line of commands out from the queue, 
   set every thing and execute them */
void ExecuteCommands(queue<queue<CommandInfo>> &qAllLines, int &nLineCount, unordered_map<int, int[2]> &numberedPipeMap) {
    while(qAllLines.size()) {
        queue<CommandInfo> qALine = qAllLines.front();  // take a line of commands out
        qAllLines.pop();

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

            /* fork a child process */
            int status;
            pid_t pid = fork();
            while (pid < 0) {
                wait(&status);
                pid = fork();
            }

            /* if it is a child process */
            if (pid == 0) {
                dup2(new_socket_fd, STDOUT_FILENO);
                dup2(new_socket_fd, STDERR_FILENO);

                /* if there is a pipe before this command, duplicate the file descriptor, and deallocate the file descriptor */
                if (command.inPipe[0] != 0) {
                    dup2(command.inPipe[0], STDIN_FILENO);
                    close(command.inPipe[0]);
                    close(command.inPipe[1]);
                }
                /* if there is a pipe after this command duplicate the file descriptor, and deallocate the file descriptor */
                if (command.outPipe[1] != 0) {
                    /* if there is a pipe for stderr after this command, duplicate the file descriptor */
                    if (command.errOutPipe[1] != 0)
                        dup2(command.errOutPipe[1], STDERR_FILENO);
                    dup2(command.outPipe[1], STDOUT_FILENO);
                    close(command.outPipe[0]);
                    close(command.outPipe[1]);
                }
                /* deallocate the file descriptors stored in numberedPipeMap */
                for (auto pipe: numberedPipeMap) {
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
                    close(command.inPipe[0]);
                    close(command.inPipe[1]);
                }

                /* if there is any pipe (including ordinary pipe and numbered pipe) after the command, don't wait */
                if (command.nPipe) {
                    signal(SIGCHLD,SIG_IGN);
                }
                /* if there is no pipe after the command, which means this is the last command in the line, wait */
                else {
                    wait(&status);
                }
            }
        }

        ++nLineCount;
    }
}


int main(int argc, const char *argv[]) {
    /* get the tcp port of the server */
    int SERVER_TCP_PORT = stoi(argv[1], nullptr);

    /* set the environment variable to bin:. */
    setenv("PATH", "bin:.", 1);

    /* Open a TCP socket (an Internet stream socket). */
    int socket_fd = socket(AF_INET, SOCK_STREAM, 0);    // socket(domain, type , protocal); AF_INET: use internet(IPV4) to transfer data between server and client; SOCK_STREAM: TCP 
    if (socket_fd < 0) { 
        cerr << "server: cannot open stream socket\n";
        exit(-1);
    }
    const int enable = 1;
    setsockopt(socket_fd, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(int));

    /* Bind our local address so that the client can send to us. */
    struct sockaddr_in server_addr;
    bzero(&server_addr, sizeof(server_addr));
    /* assign IP and PORT */
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(SERVER_TCP_PORT);
    /* bind */
    if (bind(socket_fd, (struct sockaddr *) &server_addr, sizeof(server_addr)) < 0){
        cerr << "server: cannot bind local address\n";
        exit(-1);
    }

    /* listen */
    if(listen(socket_fd, 1) < 0) {
        cerr << "server: listen socket failed\n";
        exit(-1);
    }    

    while(1) {
        /* accept */
        struct sockaddr_in client_addr;
        int client_len = sizeof(client_addr);
        new_socket_fd = accept(socket_fd, (struct sockaddr *) &client_addr, (socklen_t*) &client_len);
        if (new_socket_fd < 0) {
            cerr << "server: accept error\n";
            continue;
        }

        

        queue<queue<CommandInfo>> qAllLines;
        int nLineCount = 0;
        unordered_map<int, int[2]> numberedPipeMap;    
        while (1) {
            string shell_prompt = "% ";
            send(new_socket_fd, shell_prompt.c_str(), strlen(shell_prompt.c_str()), 0);

            char recieve_buffer[15001];
            memset(recieve_buffer, '\0', 15001);
            int readcount = read(new_socket_fd, recieve_buffer, sizeof(recieve_buffer));
            
            string strInput;
            /* the input is eof */
            if (readcount == 0) {
                break;
            }
            /* if readcount > 0*/
            else if (readcount > 0){
                char *bp;
                if((bp = strchr(recieve_buffer, '\n')) != NULL)
                    *bp = '\0';
                if((bp = strchr(recieve_buffer, '\r')) != NULL)
                    *bp = '\0';
            
                strInput = recieve_buffer;
                if (strInput == "") {
                    continue;
                }
            }
            
            vector<char*> vecInputToken = ParseInput(strInput);
            bool client_exit = false;
            if (HandleBuiltInCommands(vecInputToken, client_exit)) {
                if (client_exit) {
                    break;
                }  
                else {
                    continue;
                }   
            }
            PreprocessCommands(qAllLines, nLineCount, vecInputToken);
            ExecuteCommands(qAllLines, nLineCount, numberedPipeMap);          
        }
        close(new_socket_fd);
    }
    
    close(socket_fd);
    return 0;
}