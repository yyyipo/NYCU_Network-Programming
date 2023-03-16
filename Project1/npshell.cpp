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
};


/* read the input string and parse it by whitespaces, 
   then return a vector with tokens in it */
vector<char*> ReadInputandParse() {
    vector<char*> vecToken;
    string str = "";    
    
    while (str == "") {
        cout << "% ";
        getline (cin, str); 
        if(cin.eof()) {
            exit(0);
        }
    }
    char *cToken = strtok((char*)str.c_str(), " ");
    while (cToken != NULL) {
        char *cTokenCpy = new char[strlen(cToken)];
        strcpy(cTokenCpy, cToken);	
        vecToken.push_back(cTokenCpy);
        cToken = (char*)strtok(NULL, " ");
    }
    return vecToken;
}

/* Check if the line of input is a built-in command. 
   If it is, handle it and return true;
   if not, return false */ 
bool HandleBuiltInCommands(vector<char*> &vecToken) {
    bool isBulitInCommand = false;
    /* exit */
    if (strcmp(vecToken[0], "exit") == 0) {
        exit(0);
    }
    /* setenv */
    else if (strcmp(vecToken[0], "setenv") == 0) {
        setenv(vecToken[1], vecToken[2], 1);
        isBulitInCommand =  true;
    }
    /* printenv */
    else if (strcmp(vecToken[0], "printenv") == 0) {
        if (const char* cEnvironment = getenv(vecToken[1])) {
            cout << cEnvironment << endl;
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
        else if(strcmp(token, ">>") == 0){
            readCommand->nFileRedirection = 2;   // set nFileRedirection as 2 to know that the output should be redirected to a file
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


int main() {
    /* set the environment variable to bin:. */
    setenv("PATH", "bin:.", 1);

    queue<queue<CommandInfo>> qAllLines;
    int nLineCount = 0;
    unordered_map<int, int[2]> numberedPipeMap;    
    while (1) {
        vector<char*> vecInputToken = ReadInputandParse();
        if (HandleBuiltInCommands(vecInputToken)) {
            continue;
        }
        PreprocessCommands(qAllLines, nLineCount, vecInputToken);
        ExecuteCommands(qAllLines, nLineCount, numberedPipeMap);          
    }

    return 0;
}