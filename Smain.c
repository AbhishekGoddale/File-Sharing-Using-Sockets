#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <stdbool.h>
#include <unistd.h>
#include <fcntl.h>
#include <libgen.h>
#include <errno.h>

#include <sys/wait.h>
#include <arpa/inet.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>


char HOME_PATH[4096];
char TEMP_PATH[4096];

const char* SERVER_IP = "127.0.0.1";

const int MAIN_PORT = 12500;
const int PDF_PORT  = 12501;
const int TEXT_PORT = 12502;

const int BUFFER_SIZE   = 4096;
const int MAX_CLIENTS   = 256;
const int MAX_ARGUMENTS = 3;

const char COMMAND_DELIMITER = ';';

const char* TRANSFER_ERROR   = "E";
const char* TRANSFER_SUCCESS = "S";

const char* UPLOAD_REQUEST   = "U";
const char* DOWNLOAD_REQUEST = "D";

const char* ACKNOWLEDGED     = "A";
const char* FILE_MISSING     = "M";

const char* UPLOAD   = "ufile";
const char* DOWNLOAD = "dfile";
const char* DELETE   = "rmfile";
const char* ARCHIVE  = "dtar";
const char* DISPLAY  = "display";
const char* EXIT     = "exit";

const char* EXT_PDF  = ".pdf";
const char* EXT_TEXT = ".txt";
const char* EXT_C    = ".c";

const int SAVE_TEMPORARY = 0;
const int SAVE_PERMANENT = 1;

void setHomePath() {
    char* homePath = getenv("HOME");
    if (homePath == NULL) {
        homePath = "/home/jaseel";
    }

    char* homeDir = "/smain";

    off_t tempLen = strlen(homePath) + strlen(homeDir);
    char temp[tempLen+1];
    strcpy(temp, homePath);
    strcat(temp, homeDir);
    temp[tempLen+1] = '\0';

    strcpy(HOME_PATH, temp);
}

void setTempPath() {
    char* homePath = getenv("HOME");
    if (homePath == NULL) {
        homePath = "/home/jaseel";
    }
    char* tempDir = "/temp";

    off_t tempLen = strlen(homePath) + strlen(tempDir);
    char tempPath[tempLen+1];
    strcpy(tempPath, homePath);
    strcat(tempPath, tempDir);
    tempPath[tempLen+1] = '\0';

    strcpy(TEMP_PATH, tempPath);
}

char* toLowerCase(char *str) {
    size_t len = strlen(str);
    char *lowerStr = (char *)malloc(len + 1);

    if(lowerStr == NULL) {
        return NULL;
    }

    for(size_t i = 0; i < len; i++) {
        lowerStr[i] = tolower(str[i]);
    }

    lowerStr[len] = '\0';
    return lowerStr;
}

bool endsWith(const char *str, const char *suffix) {
    size_t strLength, suffixLength;

    strLength = strlen(str); suffixLength  = strlen(suffix);
    if(suffixLength > strLength) {
        return false;
    }
    return strcmp(str + strLength - suffixLength, suffix) == 0;
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

void extractDirectoryPath(const char *filePath, char *dirPath) {
    const char *lastSlash = strrchr(filePath, '/');
    if (lastSlash != NULL) {
        size_t dirPathLength = lastSlash - filePath;
        strncpy(dirPath, filePath, dirPathLength);
        dirPath[dirPathLength] = '\0';  // null terminate dirPath
        
    } else {
        // No '/' found in the path
        dirPath[0] = '\0'; // empty string
    }
}

int createParentDirectories(const char *dirPath) {
    char fullPath[BUFFER_SIZE];
    memset(fullPath, 0, BUFFER_SIZE);
    strcpy(fullPath, dirPath);
    // printf("debug : CPD : dirpath %s\n", dirPath);
    // printf("debug : CPD : fullpath %s\n", fullPath);

    char *currentPos = fullPath;

    if (fullPath[0] == '/') {
        currentPos++; 
    }

    while ((currentPos = strchr(currentPos, '/')) != NULL) {
        *currentPos = '\0'; // temporarily terminate the string at the current '/'
        // printf("debug : CPD : step full path : %s\n", fullPath);

        // create directory if it doesn't exist
        struct stat st = {0};
        if (stat(fullPath, &st) == -1) {
            if (mkdir(fullPath, 0700) != 0 && errno != EEXIST) {
                printf("internal error : failed to create directory -> %s <- : %s\n", fullPath, strerror(errno));
                return -1;
            }
        }

        *currentPos = '/'; // undo temporary \0 change
        currentPos++;
    }

    // handle the last directory level
    struct stat st = {0};
    if (stat(fullPath, &st) == -1) {
        if (mkdir(fullPath, 0700) != 0 && errno != EEXIST) {
            printf("internal error : failed to create directory %s: %s\n", fullPath, strerror(errno));
            return -1;
        }
    }

    return 0;
}

void getLocalFilePath(int saveMode, char* inputPath, char* localFilePath){
    char finalFilePath[BUFFER_SIZE];
    const char* basePath;
    if(saveMode == SAVE_TEMPORARY) {
        basePath = TEMP_PATH;
        strncpy(finalFilePath, TEMP_PATH, BUFFER_SIZE);
        size_t homePathLen = strlen(TEMP_PATH);
        if(homePathLen > 0 && TEMP_PATH[homePathLen - 1] != '/' && inputPath[0] != '/') {
            // if HOME_PATH does not end in a / and received path doesnt begin with a /, add / in between the two
            strncat(finalFilePath, "/", BUFFER_SIZE - strlen(finalFilePath) - 1);
        }
        strncat(finalFilePath, inputPath, BUFFER_SIZE - strlen(finalFilePath) - 1);
        // printf("debug : temp save path : %s\n", finalFilePath);
    } else {
        basePath = HOME_PATH;
        strncpy(finalFilePath, HOME_PATH, BUFFER_SIZE);
        size_t homePathLen = strlen(HOME_PATH);
        if(homePathLen > 0 && HOME_PATH[homePathLen - 1] != '/' && inputPath[0] != '/') {
            // if HOME_PATH does not end in a / and received path doesnt begin with a /, add / in between the two
            strncat(finalFilePath, "/", BUFFER_SIZE - strlen(finalFilePath) - 1);
        }
        strncat(finalFilePath, inputPath, BUFFER_SIZE - strlen(finalFilePath) - 1);
        // printf("debug : perma save path : %s\n", finalFilePath);
    }
    strcpy(localFilePath, finalFilePath); // save directory for the received file
}

int sendFileToSocket(char *filePath, int destSocket, int saveMode) {
    // printf("debug : SFS filepath : %s\n", filePath);

    char localFilePath[BUFFER_SIZE];
    getLocalFilePath(saveMode, filePath, localFilePath);

    int fd = open(localFilePath, O_RDONLY);
    if(fd < 0) {
        printf("internal error : failed to open file %s\n", filePath);
        write(destSocket, FILE_MISSING, strlen(FILE_MISSING));
        return -1;
    } else {
        write(destSocket, ACKNOWLEDGED, strlen(ACKNOWLEDGED));
    }

    if(write(destSocket, filePath, BUFFER_SIZE) < 0) {
        printf("internal error : failed to send metadata : path\n");
        close(fd);
        return -1;
    }

    // get file size
    struct stat fileStat;
    if(fstat(fd, &fileStat) < 0) {
        printf("internal error : failed to find file size\n");
        write(destSocket, TRANSFER_ERROR, strlen(TRANSFER_ERROR));
        close(fd);
        return -1;
    }

    off_t fileSize = fileStat.st_size;
    if(write(destSocket, &fileSize, sizeof(off_t)) < 0) {
        printf("internal error : failed to send metadata : size\n");
        close(fd);
        return -1;
    }

    char buffer[BUFFER_SIZE];
    ssize_t bytesRead;
    ssize_t bytesSent;
    char ack[1];

    while((bytesRead = read(fd, buffer, BUFFER_SIZE)) > 0) {
        if(recv(destSocket, ack, 1, MSG_DONTWAIT) > 0 && ack[0] == 'E') {
            printf("transfer error : receiver error, stopping file transfer\n");
            close(fd);
            return -1;
        }

        bytesSent = write(destSocket, buffer, bytesRead);
        if(bytesSent < 0) {
            printf("internal error : failed to send chunk\n");
            close(fd);
            return -1;
        }
    }

    if(bytesRead < 0) {
        printf("internal error : failed to read file %s\n", filePath);
        close(fd);
        return -1;
    }

    close(fd);

    if(read(destSocket, ack, 1) <= 0 || ack[0] != 'S') {
        printf("transfer error : failed to confirm successful file upload\n");
        return -1;
    }
    // printf("debug : file send ack received\n");
    return 0;
}

int receiveFileFromSocket(int srcSocket, int saveMode, char* localFilePath, char* recvPath) {
    // printf("debug : receiving...\n");
    char ack[1];
    char receivedFilePath[BUFFER_SIZE];

    // printf("debug : waiting for existence ack\n");
    if(read(srcSocket, ack, 1) <= 0) {
        printf("internal error : failed to read file existence info from the socket\n");
        return -1;
    } else {
        // printf("debug : processing ack ->%s<-\n", ack);
        if(ack[0] == 'M'){
            printf("validation error : file %s doesn't exist\n", receivedFilePath);
            return -2;
        }
    }

    if(read(srcSocket, receivedFilePath, BUFFER_SIZE) <= 0) {
        printf("transfer error : failed to receive metadata : path\n");
        return -1;
    }

    // printf("debug : received file path : %s\n", receivedFilePath);
    strcpy(recvPath, receivedFilePath);
    
    // char localFilePath[BUFFER_SIZE];
    getLocalFilePath(saveMode, receivedFilePath, localFilePath);

    // create parent directories if they don't exist
    char dirPath[4096];
    extractDirectoryPath(localFilePath, dirPath);
    // printf("debug : dirpath : %s\n", dirPath);

    if(createParentDirectories(dirPath) < 0){
        return -1;
    }


    int fileFd = open(localFilePath, O_WRONLY | O_CREAT | O_TRUNC, 0644);
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


int deleteLocalFile(char *filePath, int destSocket, int saveMode){
    char localFilePath[BUFFER_SIZE];
    getLocalFilePath(saveMode, filePath, localFilePath);

    int fd = open(localFilePath, O_RDONLY);
    if(fd < 0) {
        printf("internal error : failed to open file %s\n", localFilePath);
        write(destSocket, FILE_MISSING, strlen(FILE_MISSING));
        return -2;
    } else {
        if (remove(localFilePath) < 0){
            printf("internal error : failed to delete file %s\n", localFilePath);
            write(destSocket, TRANSFER_ERROR, strlen(TRANSFER_ERROR));
            close(fd);
            return -1;
        }
        write(destSocket, ACKNOWLEDGED, strlen(ACKNOWLEDGED));
        close(fd);
        return 0;
    }
}

int displayFile(int srcSocket, int saveMode, char* localFilePath, char* recvPath) {
    char ack[1];
    char receivedFilePath[BUFFER_SIZE];

    if(read(srcSocket, ack, 1) <= 0) {
        printf("internal error : failed to read file existence info from the socket\n");
        return -1;
    } else {
        if(ack[0] == 'M'){
            printf("validation error : file %s doesn't exist\n", receivedFilePath);
            return -1;
        }
    }

    if(read(srcSocket, receivedFilePath, BUFFER_SIZE) <= 0) {
        printf("transfer error : failed to receive metadata : path\n");
        return -1;
    }

    // printf("debug : received file path : %s\n", receivedFilePath);
    strcpy(recvPath, receivedFilePath);
    
    // char localFilePath[BUFFER_SIZE];
    getLocalFilePath(saveMode, receivedFilePath, localFilePath);

    long pid = fork();
    if(pid == 0)
    {
        char *command1[] = {"ls", "-R", localFilePath, NULL};
        execvp("ls", command1);
    }
    else{
        wait(NULL);
    }
    return 0;
}

int createTar() {
    size_t command_size = strlen("tar --exclude=*.tar -cf %s/c.tar -C %s .") + 2 * strlen(HOME_PATH)  + 1;
    char *command = malloc(command_size);
    snprintf(command, command_size, "tar --exclude=*.tar -cf %s/c.tar -C %s .", HOME_PATH, HOME_PATH);
    int result = system(command);
    free(command);  
    if (result == -1) {
        printf("internal error : tar file creation unsuccessful\n");
        return -1;
    }
    printf("info : tar file created successfully: c.tar\n");
    return 0;
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

void upload(char* fileName, char* destPath, int clientSocket, int spdfSocket, int stextSocket, char *command){
    // printf("debug : sending ack\n");
    // inform the client that the server is ready to accept files
    if(write(clientSocket, ACKNOWLEDGED, strlen(ACKNOWLEDGED)) < 0){
        printf("internal error : failed to write ACK to socket\n");
    }

    char localFilePath[BUFFER_SIZE], receivedFilePath[BUFFER_SIZE];
    if(endsWith(fileName, EXT_C)){
        /*
            read file from client socket using receiveFileFromSocket with save mode permanent
            if successfully received, inform the client 
        */
        if(receiveFileFromSocket(clientSocket, SAVE_PERMANENT, localFilePath, receivedFilePath) < 0){
            printf("transfer error : could not receive file from client\n");
            write(clientSocket, TRANSFER_ERROR, strlen(TRANSFER_ERROR));
        } else {
            write(clientSocket, TRANSFER_SUCCESS, strlen(TRANSFER_SUCCESS)); //----------------------------------
        }
    } else if(endsWith(fileName, EXT_PDF)){
        /*
            read file from client socket using receiveFileFromSocket with save mode temporary
            send file to spdf using sendFileToSocket
            delete temporary file
        */
        int res;
        if((res = receiveFileFromSocket(clientSocket, SAVE_TEMPORARY, localFilePath, receivedFilePath)) < 0){
            printf("transfer error: could not receive file from client\n");
            write(clientSocket, TRANSFER_ERROR, strlen(TRANSFER_ERROR));
        } else { // after receiving file from client, send it to spdf
            if(sendCommand(spdfSocket, command) < 0){
                printf("transfer error : failed to send command to Spdf\n");
                write(clientSocket, TRANSFER_ERROR, strlen(TRANSFER_ERROR));
            }
            else {
                char ack[1];
                if(read(spdfSocket, ack, 1) > 0 && ack[0] == 'A'){ // spdf ready to accept file
                    if(sendFileToSocket(receivedFilePath, spdfSocket, SAVE_TEMPORARY) < 0){ // sending failed
                        printf("transfer error : could not send file to stext\n");
                        write(clientSocket, TRANSFER_ERROR, strlen(TRANSFER_ERROR)); // let client know of failure
                        remove(localFilePath);
                    } else {
                        write(clientSocket, TRANSFER_SUCCESS, strlen(TRANSFER_SUCCESS));
                        remove(localFilePath);
                    }
                } else {
                    printf("transfer error : spdf did not ACK : ->%s<-\n", ack);
                    return;
                }
            }
        }
    } else if(endsWith(fileName, EXT_TEXT)){
        /*
            read file from client socket using receiveFileFromSocket with save mode temporary
            send file to stext using sendFileToSocket
            delete temporary file
        */
        int res;
        if((res = receiveFileFromSocket(clientSocket, SAVE_TEMPORARY, localFilePath, receivedFilePath)) < 0){
            printf("transfer error: could not receive file from client\n");
            write(clientSocket, TRANSFER_ERROR, strlen(TRANSFER_ERROR));
        } else { // after receiving file from client, send it to spdf
            if(sendCommand(stextSocket, command) < 0){
                printf("transfer error : failed to send command to Stext\n");
                write(clientSocket, TRANSFER_ERROR, strlen(TRANSFER_ERROR));
            } else {
                char ack[1];
                if(read(stextSocket, ack, 1) > 0 && ack[0] == 'A'){ // stext ready to accept file
                    if(sendFileToSocket(receivedFilePath, stextSocket, SAVE_TEMPORARY) < 0){ // sending failed
                        printf("transfer error : could not send file to stext\n");
                        write(clientSocket, TRANSFER_ERROR, strlen(TRANSFER_ERROR)); // let client know of failure
                        remove(localFilePath);
                    } else {
                        write(clientSocket, TRANSFER_SUCCESS, strlen(TRANSFER_SUCCESS));
                        remove(localFilePath);
                    }
                } else {
                    printf("transfer error : spdf did not ACK : ->%s<-\n", ack);
                    return;
                }
            }
        }
    }
}

void download(char* filePath, int clientSocket, int spdfSocket, int stextSocket, char *command){
    char localFilePath[BUFFER_SIZE];
    char recvPath[BUFFER_SIZE];
    if(endsWith(filePath, EXT_C)){ // send file to client
        sendFileToSocket(filePath, clientSocket, SAVE_PERMANENT);
    } else if(endsWith(filePath, EXT_PDF)){
        /*
            forward dfile command to spdf, receive file and save it temporarily
            then send the file to the client and delete the downloaded file from the temp directory
        */
        if(sendCommand(spdfSocket, command) < 0){
            printf("transfer error : failed to send command to Spdf\n");
            write(clientSocket, TRANSFER_ERROR, strlen(TRANSFER_ERROR));
        } else {
            int res;
            if((res = receiveFileFromSocket(spdfSocket, SAVE_TEMPORARY, localFilePath, recvPath)) < 0){
                if(res == -2){ // file doesn't exist
                    printf("validation error : could not find file in Spdf\n");
                    write(clientSocket, FILE_MISSING, strlen(FILE_MISSING));
                } else { // transfer error
                    printf("transfer error : Spdf file receive\n");
                    write(clientSocket, TRANSFER_ERROR, strlen(TRANSFER_ERROR));
                }
            } else { // send file downloaded from spdf to client
                write(spdfSocket, TRANSFER_SUCCESS, strlen(TRANSFER_SUCCESS)); // close stext transfer
                sendFileToSocket(recvPath, clientSocket, SAVE_TEMPORARY);
            }
        }
    } else if(endsWith(filePath, EXT_TEXT)){
        /*
            forward dfile command to stext, receive file and save it temporarily
            then send the file to the client and delete the downloaded file from the temp directory
        */
        if(sendCommand(stextSocket, command) < 0){
            printf("transfer error : failed to send command to Stext\n");
            write(clientSocket, TRANSFER_ERROR, strlen(TRANSFER_ERROR));
        } else {
            int res;
            if((res = receiveFileFromSocket(stextSocket, SAVE_TEMPORARY, localFilePath, recvPath)) < 0){
                if(res == -2){ // file doesn't exist
                    printf("validation error : could not find file in Stext\n");
                    write(clientSocket, FILE_MISSING, strlen(FILE_MISSING));
                } else { // transfer error
                    printf("transfer error : Stext file receive\n");
                    write(clientSocket, TRANSFER_ERROR, strlen(TRANSFER_ERROR));
                }
            } else { // send file downloaded from spdf to client
                write(stextSocket, TRANSFER_SUCCESS, strlen(TRANSFER_SUCCESS)); // close stext transfer
                sendFileToSocket(recvPath, clientSocket, SAVE_TEMPORARY);
            }
        }
    }
}

/*
deelte the file if it is a c c file and it exists or forwards the clients to aux servers to pdf and txt files
*/
void delete(char* filePath, int clientSocket, int spdfSocket, int stextSocket, char *command){
    char ack[1];
    if(endsWith(filePath, EXT_C)){
        deleteLocalFile(filePath, clientSocket, SAVE_PERMANENT);
    } else if(endsWith(filePath, EXT_PDF)){
        if(sendCommand(spdfSocket, command) < 0){
            return;
        }else{ 
            if(read(spdfSocket, ack, 1) > 0){
                if(ack[0] == 'M'){
                    printf("validation error : file doesn't exist\n");
                    write(clientSocket, FILE_MISSING, strlen(FILE_MISSING));                    
                    return;
                } else if(ack[0] == 'E'){
                    printf("transfer error : delete failed\n");
                    write(clientSocket, TRANSFER_ERROR, strlen(TRANSFER_ERROR));
                    return;
                }  
                printf("info : deleted!\n");
                return;
            }
        }
    } else if(endsWith(filePath, EXT_TEXT)){
        if(sendCommand(stextSocket, command) < 0){
            return;
        }else{ 
            if(read(stextSocket, ack, 1) > 0){
                if(ack[0] == 'M'){
                    printf("validation error : file doesn't exist\n");
                    write(clientSocket, FILE_MISSING, strlen(FILE_MISSING));                    
                    return;
                } else if(ack[0] == 'E'){
                    printf("transfer error : delete failed\n");
                    write(clientSocket, TRANSFER_ERROR, strlen(TRANSFER_ERROR));
                    return;
                }  
                printf("info : deleted!\n");
                return;
            }
        }
    }
}

/*
for c files : creates a tar of the files and sends it to the client
for pdf and txt : forwards the command to the aux servers, receives the tar and then forwards the tar to the client
*/
void archive(char* filePath, int clientSocket, int spdfSocket, int stextSocket, char *command){
    char localFilePath[BUFFER_SIZE];
    char recvPath[BUFFER_SIZE];
    if(endsWith(filePath, EXT_C)){
        /*
            create tar and send it to the client
        */
        if(createTar() < 0){
            printf("info : tar-ing failed\n");
        } else {
            char *receivedFilePath = "c.tar";
            sendFileToSocket(receivedFilePath, clientSocket, SAVE_PERMANENT);
        }
        }else if(endsWith(filePath, EXT_PDF)){
            /*
            receive tar from spdf and forwards it to the client
            */
            if(sendCommand(spdfSocket, command) < 0){
                printf("transfer error : failed to send command to Stext\n");
                write(clientSocket, TRANSFER_ERROR, strlen(TRANSFER_ERROR));
            } else {
                int res;
                if((res = receiveFileFromSocket(spdfSocket, SAVE_TEMPORARY, localFilePath, recvPath)) < 0){
                    if(res == -2){ // file doesnt exist
                        printf("validation error : could not find file in Stext\n");
                        write(clientSocket, FILE_MISSING, strlen(FILE_MISSING));
                    } else { // transfer error
                        printf("transfer error : Stext file receive\n");
                        write(clientSocket, TRANSFER_ERROR, strlen(TRANSFER_ERROR));
                    }
                } else { // send file downloaded from spdf to client
                    write(spdfSocket, TRANSFER_SUCCESS, strlen(TRANSFER_SUCCESS)); // close stext transfer
                    sendFileToSocket(recvPath, clientSocket, SAVE_TEMPORARY);
                }
            }
    } else if(endsWith(filePath, EXT_TEXT)){
        /*
            receive tar from stext and forwards it to the client
        */
        if(sendCommand(stextSocket, command) < 0){
            printf("transfer error : failed to send command to Stext\n");
            write(clientSocket, TRANSFER_ERROR, strlen(TRANSFER_ERROR));
        } else {
            int res;
            if((res = receiveFileFromSocket(stextSocket, SAVE_TEMPORARY, localFilePath, recvPath)) < 0){
                if(res == -2){ // file doesn't exist
                    printf("validation error : could not find file in Stext\n");
                    write(clientSocket, FILE_MISSING, strlen(FILE_MISSING));
                } else { // transfer error
                    printf("transfer error : Stext file receive\n");
                    write(clientSocket, TRANSFER_ERROR, strlen(TRANSFER_ERROR));
                }
            } else { // send file downloaded from spdf to client
                write(stextSocket, TRANSFER_SUCCESS, strlen(TRANSFER_SUCCESS)); // close stext transfer
                sendFileToSocket(recvPath, clientSocket, SAVE_TEMPORARY);
            }
        }
    }
}

int directoryExists(const char *path) {
    struct stat info;
    if (stat(path, &info) != 0) {
        return 0;
    } else if (info.st_mode & S_IFDIR) {
        return 1;
    } else {
        return 0;
    }
}

// uses find to get file paths and returns a char array with all the paths
char* saveDisplayToString(char* dirPath, bool* missing) {
    if (!directoryExists(dirPath)) {
        *missing = true;
        return "";
    }

    int pipefd[2];
    pid_t pid;
    char buffer[BUFFER_SIZE];
    char* output = NULL;
    size_t totalBytes = 0;

    if (pipe(pipefd) == -1) {
        printf("internal error : pipe failed - save display to string\n");
        return "";
    }

    pid = fork();
    if (pid == -1) {
        printf("internal error : fork failed - save display to string\n");
        return "";
    }

    if (pid == 0) {  // child process: write output of find to pipe
        close(pipefd[0]);
        dup2(pipefd[1], STDOUT_FILENO);
        close(pipefd[1]);

        execlp("find", "find", dirPath, "-type", "f", NULL);

        printf("internal error : find with execlp failed - save display to string\n");
        exit(EXIT_FAILURE);
    } else {  // parent process: read and store results of find to buffer
        close(pipefd[1]);

        ssize_t bytesRead;
        while ((bytesRead = read(pipefd[0], buffer, BUFFER_SIZE)) > 0) {
            output = realloc(output, totalBytes + bytesRead + 1);  // +1 for null-terminator
            if (!output) {
                printf("internal error : realloc failed - save display to string\n");
                close(pipefd[0]);
                wait(NULL);
                return "";
            }
            memcpy(output + totalBytes, buffer, bytesRead);
            totalBytes += bytesRead;
        }
        // printf("debug : bytes : %ld, %ld\n", bytesRead, totalBytes);

        close(pipefd[0]);
        wait(NULL);  // wait for the find command to run to completion

        if (output) {
            output[totalBytes] = '\0';
        } else {
            output = malloc(sizeof(char*)*2); //"" and \0
            strcpy(output,"");
        }
    }
    // printf("debug : output : ->%s<-\n", output);

    return output;  //caller must free this ?
}

// receives display results from the auz server sockets and saves it to a char array
char* receiveDisplayFromSocket(int srcSocket) {
    size_t size;
    char *buffer;

    if (read(srcSocket, &size, sizeof(size_t)) < 0) {
        printf("internal error : failed to receive metadata : display results size %s\n", strerror(errno));
        return "";
    }

    if(size == 0){
        printf("info : find returned nothing\n");
        return "";
    }

    buffer = malloc(size + 1);  // +1 for string termination
    if (!buffer) {
        printf("internal error : malloc failed - receive display from socket\n");
        return "";
    }
    if (recv(srcSocket, buffer, size, 0) == -1) {
        printf("transfer error : failed to receive display results \n");
        free(buffer);
        return "";
    }
    buffer[size] = '\0';  
    return buffer;
}

// strips irrelevant parts from the input paths
char* abstractLocalPaths(const char* mergedResults) {
    char* resultBuffer = NULL;
    size_t totalLength = 0;

    char* tempMergedResults = strdup(mergedResults); // avoid modifying the original
    if (!tempMergedResults) {
        printf("internal error : strdup failed \n");
        return NULL;
    }

    char* line = strtok(tempMergedResults, "\n");
    char buffer[BUFFER_SIZE];

    while (line != NULL) {
        char *spdfPtr = strstr(line, "/spdf");
        char *stextPtr = strstr(line, "/stext");
        char *smainPtr = strstr(line, "/smain");

        char *replacePtr = NULL;
        if (spdfPtr) {
            replacePtr = spdfPtr;
        } else if (stextPtr) {
            replacePtr = stextPtr;
        } else if (smainPtr) {
            replacePtr = smainPtr;
        }

        if (replacePtr) {
            snprintf(buffer, BUFFER_SIZE, "~smain%s", replacePtr + 6); // skip the "/spdf", "/stext", or "/smain"
        } else {
            snprintf(buffer, BUFFER_SIZE, "%s", line);  // no replacement, copy the line as is
        }

        size_t bufferLen = strlen(buffer);

        resultBuffer = realloc(resultBuffer, totalLength + bufferLen + 2); // +1 for '\n', +1 for '\0'
        if (!resultBuffer) {
            printf("internal error : realloc failed \n");
            free(tempMergedResults);
            return NULL;
        }

        // copy the buffer content into the resultBuffer
        strcpy(resultBuffer + totalLength, buffer);
        totalLength += bufferLen;
        resultBuffer[totalLength] = '\n';
        totalLength++;
        resultBuffer[totalLength] = '\0';

        line = strtok(NULL, "\n");
    }

    free(tempMergedResults);

    return resultBuffer;
}

void display(char* dirPath, int clientSocket, int spdfSocket, int stextSocket, char *command) {
    bool smainMissing = false, stextMissing = false, spdfMissing = false;
    char localFilePath[BUFFER_SIZE];
    getLocalFilePath(SAVE_PERMANENT, dirPath, localFilePath);

    // printf("debug : smain saving display\n");
    char* smainResults = saveDisplayToString(localFilePath, &smainMissing); // find on smain
    // printf("debug : smain results : %s\n", smainResults);

    char* stextResults;
    if(sendCommand(stextSocket, command) < 0){
        free(smainResults);
        return;
    }else{ // results of find on stext
        char ack[1];
        if(read(stextSocket, ack, 1) > 0){
            if(ack[0] == 'A'){
                // printf("debug : receiving display from socket\n");
                stextResults = receiveDisplayFromSocket(stextSocket); // get text files
                // printf("debug : receiving complete : %s\n", stextResults);
            } else {
                stextMissing = true;
                stextResults = "";
            }
        } else {
            printf("transfer error : failed to read display from stext\n");
            return;
        }
    }

    char* spdfResults;
    if(sendCommand(spdfSocket, command) < 0){
        free(smainResults);
        return;
    }else{ // results of find on spdf
        char ack[1];
        if(read(spdfSocket, ack, 1) > 0){
            if(ack[0] == 'A'){
                // printf("debug : receiving display from socket\n");
                spdfResults = receiveDisplayFromSocket(spdfSocket); // get text files
                // printf("debug : receiving complete : %s\n", stextResults);
            } else {
                spdfMissing = true;
                spdfResults = "";
            }
        } else {
            printf("transfer error : failed to read display from stext\n");
            return;
        }
    }

    if(smainMissing && stextMissing && spdfMissing) { // path is missing in all three servers
        printf("info : directory doesn't exist\n");
        write(clientSocket, FILE_MISSING, strlen(FILE_MISSING));
        return;
    }
    write(clientSocket, ACKNOWLEDGED, strlen(ACKNOWLEDGED));

    // Calculate the total length needed for the merged string
    // printf("debug : finding length\n");
    size_t totalLength = strlen(smainResults) + strlen(stextResults) + strlen(spdfResults) + 1; // +1 for null terminator
    // printf("debug : length found %ld\n", totalLength);

    char* mergedResults = malloc(totalLength);
    if (!mergedResults) {
        printf("internal error : malloc failed \n");
        return;
    }

    // merge results into one
    // printf("debug : copy main results\n");
    strcpy(mergedResults, smainResults); // c
    // printf("debug : copy pdf results\n");
    strcat(mergedResults, spdfResults); // pdf
    // printf("debug : copy text results\n");
    strcat(mergedResults, stextResults); // txt

    // printf("debug : abstracting paths\n");
    if(strlen(mergedResults) <= 1){
        off_t displaySize = 0;
        if (write(clientSocket, &displaySize, sizeof(size_t)) < 0) {
            printf("internal error : failed to send metadata : display size \n");
            free(mergedResults);
            return;
        }
        printf("info : display size : 0");
        return;
    }
    char* abstractedPaths = abstractLocalPaths(mergedResults);
    // printf("debug : abstraction complete\n");


    /*
    strips details not needed by the client from the paths returned from the three servers
    and sends it to the client
    */
    size_t abstractedLength = strlen(abstractedPaths);
    // printf("debug : abs len : %ld\n", abstractedLength);
    if (write(clientSocket, &abstractedLength, sizeof(size_t)) < 0) {
        printf("internal error : failed to send metadata : display size \n");
        free(mergedResults);
        free(abstractedPaths);
        return;
    }

    if (write(clientSocket, abstractedPaths, abstractedLength) < 0) {
        printf("internal error : failed to send display results\n");
        free(mergedResults);
        free(abstractedPaths);
        return;
    }

    // printf("debug : merged results:\n%s\n", abstractedPaths);

    free(mergedResults);
    free(abstractedPaths);
    // printf("debug : freed memory");
}

/*
connects to aux servers
reads commands from the client in a loop
calls respective command handler functions
*/
void prcclient(int clientSocket){
    char buffer[BUFFER_SIZE];

    int spdfSocket  = connectToSocket(SERVER_IP, PDF_PORT);
    int stextSocket = connectToSocket(SERVER_IP, TEXT_PORT);

    if (spdfSocket < 0 || stextSocket < 0){
        printf("internal error : handler couldn't connect to auxillary server/s\n");
        return;
    }

    while(true){
        // read command from client into buffer
        memset(buffer, 0, BUFFER_SIZE);
        int n = read(clientSocket, buffer, BUFFER_SIZE-1); // -1 to give space for \0
        if (n <= 0) {
            printf("internal error : failed to read from socket, exiting client handler...\n");
            return;
        }
        char bufferCopy[BUFFER_SIZE];
        strcpy(bufferCopy, buffer);
        // fill command tokens by splitting buffer using COMMAND_DELIMITER
        char *tokens[MAX_ARGUMENTS];
        int numTokens = 0;
        tokens[numTokens] = strtok(buffer, " ");
        while (tokens[numTokens] != NULL) {
            numTokens++;
            tokens[numTokens] = strtok(NULL, " ");
        }
        if(strcmp(tokens[0], UPLOAD) == 0){
            upload(tokens[1], tokens[2], clientSocket, spdfSocket, stextSocket, bufferCopy);
        } else if(strcmp(tokens[0], DOWNLOAD) == 0){
            download(tokens[1], clientSocket, spdfSocket, stextSocket, bufferCopy);
        } else if(strcmp(tokens[0], DELETE) == 0){
            delete(tokens[1], clientSocket, spdfSocket, stextSocket, bufferCopy);
        } else if(strcmp(tokens[0], ARCHIVE) == 0){
            archive(tokens[1], clientSocket, spdfSocket, stextSocket, bufferCopy);
        } else if(strcmp(tokens[0], DISPLAY) == 0){
            display(tokens[1], clientSocket, spdfSocket, stextSocket, bufferCopy);
        } else if(strcmp(tokens[0], EXIT) == 0){
            printf("info : exiting client handler...\n");
            close(spdfSocket);
            close(stextSocket);
            return;
        }
    }
}


int main(){
    setHomePath();
    setTempPath();
    printf("info : home directory : %s\n", HOME_PATH);
    printf("info : temp directory : %s\n", TEMP_PATH);

    mkdir(HOME_PATH, 0777);
    mkdir(TEMP_PATH, 0777);
    // create socket, bind to a port and listen for connections 
    int listeningSocket = socket(AF_INET, SOCK_STREAM, 0);
    if (listeningSocket < 0) {
        printf("internal error : failed to create socket, exiting...\n");
        exit(1);
    }

    struct sockaddr_in serverAddress;
    memset(&serverAddress, 0, sizeof(serverAddress));
    serverAddress.sin_family = AF_INET;
    serverAddress.sin_port = htons(MAIN_PORT);                                           
    serverAddress.sin_addr.s_addr = inet_addr(SERVER_IP);

    if (bind(listeningSocket, (struct sockaddr *)&serverAddress, sizeof(serverAddress)) < 0) {
        printf("internal error : failed to bind to socket on port %d, exiting...\n", MAIN_PORT);
        close(listeningSocket);
        exit(1);
    }

    if (listen(listeningSocket, MAX_CLIENTS) < 0) {
        printf("internal error : failed to listen on port %d, exiting...\n", MAIN_PORT);
    }
    printf("info : listening on port %d...\n", MAIN_PORT);

    // fork a new handler for eac h new client
    while(true){
        int connectionSocket = accept(listeningSocket, (struct sockaddr*)NULL, NULL);
        printf("info : accepted connection from a new client\n");

        int pid = fork();
        if (pid < 0) {
            printf("internal error : failed to fork client handler, try again...\n");
        } else if (pid > 0){  // handle new client
            setsid();
            close(listeningSocket);
            prcclient(connectionSocket);
            close(connectionSocket);
            exit(0);
        } else { // close parent side socket fd as it is no longer needed
            close(connectionSocket);
        }
    }
}