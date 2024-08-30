#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <stdbool.h>
#include <unistd.h>
#include <fcntl.h>
#include <libgen.h>
#include <arpa/inet.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>

const char* MAIN_PREFIX = "~smain/";

const char* UPLOAD   = "ufile";
const char* DOWNLOAD = "dfile";
const char* DELETE   = "rmfile";
const char* ARCHIVE  = "dtar";
const char* DISPLAY  = "display";
const char* EXIT     = "exit";

const char* EXT_PDF   = ".pdf";
const char* EXT_TEXT   = ".txt";
const char* EXT_C     = ".c";

const char* TRANSFER_ERROR   = "E";
const char* TRANSFER_SUCCESS = "S";

const char* UPLOAD_REQUEST   = "U";
const char* DOWNLOAD_REQUEST = "D";

const char* ACKNOWLEDGED     = "A";
const char* FILE_MISSING     = "M";


const char* SERVER_IP = "127.0.0.1";

const int MAIN_PORT = 12500;
const int PDF_PORT  = 12501;
const int TEXT_PORT = 12502;

const int MAX_CLIENTS = 256;

const int MAX_ARGUMENTS = 10;

const int BUFFER_SIZE = 4096;

const char *validatedExt = NULL;

char *fileName;
char *destinationPath;
char *pathName;

void stripLeadingPrefix(const char* input, char* output, const char* prefix) {
    size_t prefixLen = strlen(prefix);
    // printf("debug : ip : %s, prefix : %s\n", input, prefix);
    if (strncmp(input, prefix, prefixLen) == 0) {
        // printf("debug : prefix match\n");
        strcpy(output, input + prefixLen);
    } else {
        // printf("debug : no prefix match\n");
        strcpy(output, input);
    }

    if(strlen(output) == 0){
        strcpy(output, "/");
    }
}


int validateExtension(const char *fileName, const char *validatedExt[]) {
    const char *ext = strrchr(fileName, '.');
    if (ext == NULL) {
        printf("validation error : invalid extension\n");
        return -1;
    }

    switch (ext[1]) { 
        case 'c':
            if (strcasecmp(ext, EXT_C) == 0) {
                *validatedExt = EXT_C;
                return 0;
            }
            break;
        case 'p':
            if (strcasecmp(ext, EXT_PDF) == 0) {
                *validatedExt = EXT_PDF;
                return 0;
            }
            break;
        case 't':
            if (strcasecmp(ext, EXT_TEXT) == 0) {
                *validatedExt = EXT_TEXT;
                return 0;
            }
            break;
        default:
            printf("validation error : invalid extension\n");
            return -1;
    }

    printf("validation error : invalid extension\n");
    return -1;
}


int validateFile(char *fileName) {
    int n = open(fileName, O_RDONLY);
    if(n < 0) {
        printf("validation error : file does not exist \n");
        return -1;
    } else {
        close(n);
        return 0;
    }
}

int validateInput(int numTokens, char *inputTokens[]) {
    if(strcmp(inputTokens[0], EXIT) == 0){
        return 0;
    }

    if(numTokens < 2 || numTokens > 3) {
        printf("validation error : invalid number of arguments\n");
        return -1;
    }

    if(strcmp(inputTokens[0], UPLOAD) == 0) {
        if(numTokens == 3) {
            fileName = malloc(strlen(inputTokens[1]) + 1);
            strcpy(fileName, inputTokens[1]);
            destinationPath = malloc(strlen(inputTokens[2]) + 1);
            strcpy(destinationPath, inputTokens[2]);
            // printf("debug : file Name : %s\n", fileName);
            // printf("debug : destination Path : %s\n", destinationPath);
            if(validateExtension(fileName, &validatedExt) < 0) {
                free(fileName);
                free(destinationPath);
                return -1;
            }
            if(validateFile(fileName) < 0) {
                free(fileName);
                free(destinationPath);
                return -1; 
            }
        } else {
            printf("validation error : invalid user format\n");
            printf("format : ufile [filename] [destination Path]\n");
            return -1;
        }
        free(fileName);
        free(destinationPath);
    }

    else if(strcmp(inputTokens[0], DOWNLOAD) == 0) {
        if(numTokens == 2) {
            fileName = malloc(strlen(inputTokens[1]) + 1);
            strcpy(fileName, inputTokens[1]);
            if(validateExtension(fileName, &validatedExt) < 0) {
                free(fileName);
                return -1;
            }
        } else {
            printf("validation error : invalid user format\n");
            printf("format : dfile [filename]\n");
            return -1;
        }
        free(fileName);
    }

    else if(strcmp(inputTokens[0], DELETE) == 0) {
        if(numTokens == 2) {
        fileName = malloc(strlen(inputTokens[1]) + 1);
        strcpy(fileName, inputTokens[1]);
        if(validateExtension(fileName, &validatedExt) < 0) {
                free(fileName);
                return -1;
            }
        } else {
            printf("validation error : invalid user format\n");
            printf("format : rmfile [filename]\n");
            return -1;
        }
        free(fileName);
    }

    else if(strcmp(inputTokens[0], ARCHIVE) == 0) {
        if(numTokens == 2) {
        fileName = malloc(strlen(inputTokens[1]) + 1);
        strcpy(fileName, inputTokens[1]);
        if(validateExtension(fileName, &validatedExt) < 0) {
                free(fileName);
                return -1;
            }
        } else {
            printf("validation error : invalid user format\n");
            printf("format : dfile [filetype]\n");
            return -1;
        }
        free(fileName);
    }
    else if(strcmp(inputTokens[0], DISPLAY) == 0) {
        if(numTokens == 2) {
        pathName = malloc(strlen(inputTokens[1]) + 1);
        strcpy(pathName, inputTokens[1]);
        } else {
            free(pathName);
            printf("validation error : invalid user format\n");
            printf("format : display [pathname]\n");
            return -1;
        }
        free(pathName);
    } else {
        printf("validation error : invalid user format\n");
        return -1;
    }
    return 0;
}

int receiveFileFromSocket(int srcSocket) {
    char ack[1];
    char receivedFilePath[BUFFER_SIZE];

    if(read(srcSocket, ack, sizeof(ack)) <= 0) {
        printf("internal error : failed to read file existence info from the socket\n");
        return -1;
    } else {
        if(ack[0] == 'M'){
            printf("validation error : file doesn't exist\n");
            return -1;
        }
    }

    if(read(srcSocket, receivedFilePath, BUFFER_SIZE) <= 0) {
        printf("transfer error : failed to receive metadata : path\n");
        return -1;
    }

    // only need filename since we are saving it to pwd
    // printf("debug : received file path : %s\n", receivedFilePath);
    char* fileName = strrchr(receivedFilePath, '/');
    if (fileName != NULL) {
        fileName++;     
    } else {
        fileName = receivedFilePath;
    }
    // printf("debug : filename : %s\n", fileName);

    int fileFd = open(fileName, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if(fileFd < 0) {
        printf("internal error : file create/open failed\n");
        return -1;
    }
    // printf("debug : opened file \n");
    // Receive the file size
    off_t fileSize;
    if(read(srcSocket, &fileSize, sizeof(off_t)) <= 0) {
        printf("transfer error : file size receive failed\n");
        close(fileFd);
        return -1;
    }

    // Receive the file content and write it to the file
    ssize_t bytesReceived;
    off_t totalReceived = 0;
    char buffer[BUFFER_SIZE];

    while(totalReceived < fileSize) {
        // printf("debug : reading chunk\n");
        bytesReceived = read(srcSocket, buffer, BUFFER_SIZE);
        if(bytesReceived <= 0) {
            printf("transfer error : file content receive failed\n");
            close(fileFd);
            return -1;
        }
        // printf("debug : chunk read\n");

        if(write(fileFd, buffer, bytesReceived) != bytesReceived) {
            printf("internal error : file write failed\n");
            close(fileFd);
            return -1;
        }

        totalReceived += bytesReceived;
    }

    close(fileFd);
    return 0;
}

int sendFileToSocket(const char *filePath, const char* destPath, int destSocket) {
    int fd = open(filePath, O_RDONLY);
    if(fd < 0) {
        printf("internal error : failed to open file %s\n", filePath);
        write(destSocket, FILE_MISSING, strlen(FILE_MISSING));
        return -2;
    } else {
        write(destSocket, ACKNOWLEDGED, strlen(ACKNOWLEDGED));
    }

    //append filename to dest dir path to get final dest file path
    char finalFilePath[BUFFER_SIZE];
    strncpy(finalFilePath, destPath, BUFFER_SIZE);
    size_t destPathLen = strlen(destPath);
    if(destPathLen > 0 && destPath[destPathLen - 1] != '/') {
        // if destPath does not end in a /, add one
        strncat(finalFilePath, "/", BUFFER_SIZE - strlen(finalFilePath) - 1);
    }
    strncat(finalFilePath, filePath, BUFFER_SIZE - strlen(finalFilePath) - 1);
    // printf("debug : final path %s,  len : %ld\n", finalFilePath, strlen(finalFilePath));
    finalFilePath[strlen(finalFilePath)] = '\0';

    // printf("debug : sending path\n");
    if(write(destSocket, finalFilePath, BUFFER_SIZE) < 0) {
        printf("internal error : failed to send metadata : path\n");
        close(fd);
        return -1;
    }
    // printf("debug : path sent\n");
    // get file size
    struct stat fileStat;
    if(fstat(fd, &fileStat) < 0) {
        printf("internal error : failed to find file size\n");
        write(destSocket, TRANSFER_ERROR, strlen(TRANSFER_ERROR));
        close(fd);
        return -1;
    }

    off_t fileSize = fileStat.st_size;
    // printf("debug : sending size %ld\n", fileSize);
    if(write(destSocket, &fileSize, sizeof(off_t)) < 0) {
        printf("internal error : failed to send metadata : size\n");
        close(fd);
        return -1;
    }
    // printf("debug : size sent\n");

    char buffer[BUFFER_SIZE];
    ssize_t bytesRead;
    ssize_t bytesSent;
    char ack[1];

    // read chunks from file and write to socket
    while((bytesRead = read(fd, buffer, BUFFER_SIZE)) > 0) {
        // printf("debug : chunk ack check\n");
        if(recv(destSocket, ack, 1, MSG_DONTWAIT) > 0 && ack[0] == 'E') {
            // printf("debug : receiver error, stopping file transfer\n");
            close(fd);
            return -1;
        }

        // printf("debug : sending chunk\n");
        bytesSent = write(destSocket, buffer, bytesRead);
        if(bytesSent < 0) {
            printf("internal error : failed to send chunk\n");
            close(fd);
            return -1;
        }
        // printf("debug : chunk sent\n");
    }

    if(bytesRead < 0) {
        printf("internal error : failed to read file %s\n", filePath);
        write(destSocket, TRANSFER_ERROR, strlen(TRANSFER_ERROR));
        close(fd);
        return -1;
    }

    close(fd);

    // printf("debug : waiting for success confirmation\n");
    // Wait for TRANSFER_SUCCESS confirmation
    if(read(destSocket, ack, 1) <= 0 || ack[0] != 'S') {
        printf("transfer error : failed to confirm successful file upload\n");
        return -1;
    }

    return 0;
}

int sendFilePathToSocket(const char *filePath, const char* destPath, int destSocket) {
    //append filename to dest dir path to get final dest file path
    char finalFilePath[BUFFER_SIZE];
    strncpy(finalFilePath, destPath, BUFFER_SIZE);
    size_t destPathLen = strlen(destPath);
    if(destPathLen > 0 && destPath[destPathLen - 1] != '/') {
        // if destPath does not end in a /, add one
        strncat(finalFilePath, "/", BUFFER_SIZE - strlen(finalFilePath) - 1);
    }
    strncat(finalFilePath, filePath, BUFFER_SIZE - strlen(finalFilePath) - 1);
    // printf("debug : final path %s,  len : %ld\n", finalFilePath, strlen(finalFilePath));
    finalFilePath[strlen(finalFilePath)] = '\0';

    // printf("debug : sending path\n");
    if(write(destSocket, finalFilePath, BUFFER_SIZE) < 0) {
        printf("internal error : failed to send metadata : path\n");
        return -1;
    }
    return 0;
}

// connects to a socket and returns the connection file descriptor
int connectToSocket(const char* serverIP, const int socketPort){
    int socketFd;
    struct sockaddr_in serverAddr;

    socketFd = socket(AF_INET, SOCK_STREAM, 0);
    if(socketFd < 0) {
        printf("internal error : failed to create socket\n");
        return -1;
    }

    memset(&serverAddr, 0, sizeof(serverAddr));
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(socketPort);

    if(inet_pton(AF_INET, serverIP, &serverAddr.sin_addr) <= 0) {
        printf("internal error : invalid IP address\n");
        close(socketFd);
        return -1;
    }

    if(connect(socketFd, (struct sockaddr *)&serverAddr, sizeof(serverAddr)) < 0) {
        printf("internal error : failed to connect to the socket\n");
        close(socketFd);
        return -1;
    }

    printf("info : connected to the socket at %s:%d\n", serverIP, socketPort);
    return socketFd;
}

// sends the command to the server socket and returns 0 if it succeeded
int sendCommand(int serverSocket, char* command){
    ssize_t bytesSent = send(serverSocket, command, strlen(command), 0); // +1 for null terminator?
    if (bytesSent < 0) {
        printf("internal error : failed to send command to server, exiting...\n");
        return -1;
    }
    return 0;
}

char* receiveDisplay(int srcSocket) {
    size_t size;
    char *buffer;

    if (read(srcSocket, &size, sizeof(size_t)) < 0) {
        printf("internal error : failed to receive metadata : display results size\n");
        return NULL;
    }

    if(size == 0){
        return "";
    }

    buffer = malloc(size + 1);
    if (!buffer) {
        printf("internal error : malloc failed\n");
        return NULL;
    }

    if (read(srcSocket, buffer, size) < 0) {
        printf("internal error : failed to receive display results\n");
        free(buffer);
        return NULL;
    }

    buffer[size] = '\0';
    return buffer;
}


//sends user input to the server and handle the server's response accordingly
void handleCommand(char** tokens, int numTokens, int serverSocket, char* userInput){
    char command[BUFFER_SIZE];
    char ack[1];
    memset(command, 0, sizeof(command));
    for (int i = 0; i < numTokens; i++) {
        // Concatenate the token into the command string
        strcat(command, tokens[i]);
        if (i < numTokens - 1) {
            strcat(command, " ");
        }
        // printf("debug : token : %s\n", tokens[i]); 
    }
    // return; //-----------------
    // printf("command: %s\n", command);

    if(strcmp(tokens[0], UPLOAD) == 0){
        // printf("debug : sending command : %s\n", command);
        if(sendCommand(serverSocket, command) < 0){
            return;
        } else { // if upload was acknowledged by server, start sending the file
            if(read(serverSocket, ack, 1) > 0 && ack[0] == 'A'){
                if(sendFileToSocket(tokens[1], tokens[2], serverSocket) == 0){
                    printf("info : upload complete!\n");
                } // send file, dest path to sendFileToSocket
            } else {
                printf("transfer error : server did not ACK ->%s<-\n", ack);
                return;
            }
        }
    }
    else if(strcmp(tokens[0], DOWNLOAD) == 0){
        if(sendCommand(serverSocket, command) < 0){
            return;
        }else{ // start receiving file from the server
            if(receiveFileFromSocket(serverSocket) == 0){
                printf("info : download complete!\n");
                write(serverSocket, TRANSFER_SUCCESS, strlen(TRANSFER_SUCCESS));
            } else {
                write(serverSocket, TRANSFER_ERROR, strlen(TRANSFER_ERROR));
            }
        }
    } else if(strcmp(tokens[0], DELETE) == 0){
        printf("info : delete complete : %s\n", command);
        if(sendCommand(serverSocket, command) < 0){
            return;
        }else{ 
            if(read(serverSocket, ack, 1) > 0){
                if(ack[0] == 'M'){
                    printf("validation error : file doesn't exist\n");
                    return;
                } else if(ack[0] == 'E'){
                    printf("transfer error : delete failed\n");
                    return;
                }  
                printf("info : deleted!\n");
                return;
            }        
        }
    } else if(strcmp(tokens[0], ARCHIVE) == 0){
        // printf("debug : sending archive command : %s\n", command);
        //TODO
        if(sendCommand(serverSocket, command) < 0){
            return;
        }else{ // start receiving file from the server
            if(receiveFileFromSocket(serverSocket) == 0){
                printf("info : archive complete!\n");
                write(serverSocket, TRANSFER_SUCCESS, strlen(TRANSFER_SUCCESS));
            } else {
                write(serverSocket, TRANSFER_ERROR, strlen(TRANSFER_ERROR));
            }
        }
    } else if(strcmp(tokens[0], DISPLAY) == 0){
        // printf("debug : sending command : %s\n", command);
        if(sendCommand(serverSocket, command) < 0){ // display failed
            return;
        } else{
            if(read(serverSocket, ack, 1) > 0 && ack[0] == 'A'){
                // printf("debug : receiving display\n");
                char* results = receiveDisplay(serverSocket);
                if(results != NULL){
                    printf("%s\n", results);
                } else {
                    printf("transfer error : failed to receive results\n");
                }
            } else if(ack[0] == 'M') {
                printf("validation error : directory doesn't exist\n");
                return;
            } else {
                printf("transfer error : server did not ACK\n");
            }
        }
    } else if(strcmp(tokens[0], EXIT) == 0){
        printf("exit : %s\n", command);
        sendCommand(serverSocket, command);
        close(serverSocket);
        free(userInput);
        exit(0);
    } else {
        return;
    }
}

int main() {
    // connect to server
    int serverSocket;
    if((serverSocket = connectToSocket(SERVER_IP, MAIN_PORT)) < 0){
        exit(0);
    }

    char *userInput = NULL;
    size_t inputSize = 0;
    int numTokens;
    char *inputTokens[MAX_ARGUMENTS];
    
    //take user input in a loop
    while (true) {
        printf(">> ");
        ssize_t inputLen = getline(&userInput, &inputSize, stdin);
        if(inputLen == -1) {
            printf("internal error : failed to read user input\n");
            break;
        }
        userInput[strcspn(userInput, "\n")] = '\0';

        /*
            tokenize user input and strip ~smain
            validate the input and call the handler
        */
        numTokens = 0;
        inputTokens[numTokens] = strtok(userInput, " ");
        while (inputTokens[numTokens] != NULL) {
            numTokens++;
            inputTokens[numTokens] = strtok(NULL, " ");
        }

        char *strippedTokens[MAX_ARGUMENTS];
        for (int i = 0; i < numTokens; i++) {
            strippedTokens[i] = malloc(BUFFER_SIZE);
            if (strippedTokens[i] == NULL) {
                printf("internal error : malloc failed\n");
                exit(EXIT_FAILURE);
            }
            stripLeadingPrefix(inputTokens[i], strippedTokens[i], MAIN_PREFIX);
            stripLeadingPrefix(inputTokens[i], strippedTokens[i], "~smain");
        }

        if(validateInput(numTokens, strippedTokens) < 0) {
            continue;
        }

        handleCommand(strippedTokens, numTokens, serverSocket, userInput);

        if(strcasecmp(strippedTokens[0], EXIT) == 0){
            break;
        }
    }
    
    close(serverSocket);
    free(userInput);
    return 0;
}


