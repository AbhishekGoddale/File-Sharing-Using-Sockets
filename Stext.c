#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>
#include <stdbool.h>
#include <unistd.h>
#include <fcntl.h>
#include <libgen.h>
#include <errno.h>

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

    char* homeDir = "/stext";

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

void extractDirectoryPath(const char *filePath, char *dirPath) {
    const char *lastSlash = strrchr(filePath, '/');
    if (lastSlash != NULL) {
        size_t dirPathLength = lastSlash - filePath;
        strncpy(dirPath, filePath, dirPathLength);
        dirPath[dirPathLength] = '\0';  // null terminate dirPath
        
    } else {
        // no '/' found in the path, set dirPath to empty
        dirPath[0] = '\0';
    }
}

int createParentDirectories(const char *dirPath) {
    char fullPath[BUFFER_SIZE];
    memset(fullPath, 0, BUFFER_SIZE);
    strcpy(fullPath, dirPath);
    // printf("debug : CPD : dirpath %s\n", dirPath);
    // printf("debug : CPD : fullpath %s\n", fullPath);

    char *currentPos = fullPath;

    // if path starts with /, move past it
    if (fullPath[0] == '/') {
        currentPos++;
    }

    while ((currentPos = strchr(currentPos, '/')) != NULL) {
        *currentPos = '\0'; // Temporarily terminate the string at the current '/'
        // printf("debug : CPD : step full path : %s\n", fullPath);

        // check if directory exists and create it if it doesn't
        struct stat st = {0};
        if (stat(fullPath, &st) == -1) {
            if (mkdir(fullPath, 0700) != 0 && errno != EEXIST) {
                printf("internal error : failed to create directory -> %s <- : %s\n", fullPath, strerror(errno));
                return -1;
            }
        }

        *currentPos = '/'; // Restore the '/' and move to the next directory level
        currentPos++; // Move past the '/'
    }

    // Handle the last directory level (or if dirPath didn't have any '/')
    struct stat st = {0};
    if (stat(fullPath, &st) == -1) {
        if (mkdir(fullPath, 0700) != 0 && errno != EEXIST) {
            printf("internal error : failed to create directory %s: %s\n", fullPath, strerror(errno));
            return -1;
        }
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

int sendFileToSocket(char *filePath, int destSocket) {
    char localFilePath[BUFFER_SIZE];
    getLocalFilePath(SAVE_PERMANENT, filePath, localFilePath);

    int fd = open(localFilePath, O_RDONLY);
    if(fd < 0) {
        printf("internal error : failed to open file %s\n", filePath);
        write(destSocket, FILE_MISSING, strlen(FILE_MISSING));
        return -2;
    } else {
        write(destSocket, ACKNOWLEDGED, strlen(ACKNOWLEDGED));
    }

    usleep(20000);

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
        // write(destSocket, TRANSFER_ERROR, strlen(TRANSFER_ERROR));
        close(fd);
        return -1;
    }

    close(fd);

    if(read(destSocket, ack, 1) <= 0 || ack[0] != 'S') {
        printf("transfer error : failed to confirm successful file upload\n");
        return -1;
    }

    return 0;
}

int receiveFileFromSocket(int srcSocket, int saveMode, char* destFilePath) {
    char ack[1];
    char receivedFilePath[BUFFER_SIZE];
    // ssize_t pathLen = 0;

    if(read(srcSocket, ack, 1) <= 0) {
        printf("internal error : failed to read file existence info from the socket\n");
        write(srcSocket, TRANSFER_ERROR, strlen(TRANSFER_ERROR));
        return -1;
    } else {
        if(ack[0] == 'M'){
            printf("validation error : file %s doesn't exist\n", destFilePath);
            return -2;
        }
    }

    // if(read(srcSocket, &receivedFilePath[pathLen], 1) <= 0) {
    if(read(srcSocket, receivedFilePath, BUFFER_SIZE) <= 0) {
        printf("transfer error : failed to receive metadata : path\n");
        write(srcSocket, TRANSFER_ERROR, 1);
        return -1;
    }

    // printf("debug : received file path : %s\n", receivedFilePath);
    strcpy(destFilePath, receivedFilePath);
    
    char finalFilePath[BUFFER_SIZE];
    const char* basePath;
    if(saveMode == SAVE_TEMPORARY) {
        basePath = TEMP_PATH;
        strncpy(finalFilePath, TEMP_PATH, BUFFER_SIZE);
        size_t homePathLen = strlen(TEMP_PATH);
        if(homePathLen > 0 && TEMP_PATH[homePathLen - 1] != '/' && receivedFilePath[0] != '/') {
            // if HOME_PATH does not end in a / and received path doesnt begin with a /, add / in between the two
            strncat(finalFilePath, "/", BUFFER_SIZE - strlen(finalFilePath) - 1);
        }
        strncat(finalFilePath, receivedFilePath, BUFFER_SIZE - strlen(finalFilePath) - 1);
        // printf("debug : temp save path : %s\n", finalFilePath);
    } else {
        basePath = HOME_PATH;
        strncpy(finalFilePath, HOME_PATH, BUFFER_SIZE);
        size_t homePathLen = strlen(HOME_PATH);
        if(homePathLen > 0 && HOME_PATH[homePathLen - 1] != '/' && receivedFilePath[0] != '/') {
            // if HOME_PATH does not end in a / and received path doesnt begin with a /, add / in between the two
            strncat(finalFilePath, "/", BUFFER_SIZE - strlen(finalFilePath) - 1);
        }
        strncat(finalFilePath, receivedFilePath, BUFFER_SIZE - strlen(finalFilePath) - 1);
        // printf("debug : perma save path : %s\n", finalFilePath);
    }

    // create parent directories if they don't exist
    char dirPath[4096];
    extractDirectoryPath(finalFilePath, dirPath);
    // printf("debug : dirpath : %s\n", dirPath);

    if(createParentDirectories(dirPath) < 0){
        write(srcSocket, TRANSFER_ERROR, strlen(TRANSFER_ERROR));
        return -1;
    }

    int fileFd = open(finalFilePath, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if(fileFd < 0) {
        printf("internal error : file create/open failed\n");
        write(srcSocket, TRANSFER_ERROR, strlen(TRANSFER_ERROR));
        return -1;
    }

    // receive file size
    off_t fileSize;
    if(read(srcSocket, &fileSize, sizeof(off_t)) <= 0) {
        printf("transfer error : file size receive failed\n");
        write(srcSocket, TRANSFER_ERROR, 1);
        close(fileFd);
        return -1;
    }
    // printf("debug : received file size : %ld\n", fileSize);
    // receive the file content from the socket and write it to the file
    ssize_t bytesReceived;
    off_t totalReceived = 0;
    char buffer[BUFFER_SIZE];

    while(totalReceived < fileSize) {
        bytesReceived = read(srcSocket, buffer, BUFFER_SIZE);
        if(bytesReceived <= 0) {
            printf("transfer error : file content receive failed\n");
            write(srcSocket, TRANSFER_ERROR, 1);
            close(fileFd);
            return -1;
        }

        if(write(fileFd, buffer, bytesReceived) != bytesReceived) {
            printf("internal error : file write failed\n");
            write(srcSocket, TRANSFER_ERROR, 1);
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

int createTar() {
      size_t command_size = strlen("tar --exclude=*.tar -cf %s/text.tar -C %s .") + 2 * strlen(HOME_PATH)  + 1;
    char *command = malloc(command_size);
    snprintf(command, command_size, "tar --exclude=*.tar -cf %s/text.tar -C %s .", HOME_PATH, HOME_PATH);
    int result = system(command);
    free(command);  
    if (result == -1) {
        printf("Tar file creation unsuccessful\n");
        return -1;
    }
    printf("Tar file created successfully: c.tar\n");
    return 0;
}

void upload(char* fileName, char* destPath, int clientSocket){
    // inform the client that the server is ready to accept files
    write(clientSocket, ACKNOWLEDGED, strlen(ACKNOWLEDGED));
    // printf("debug : server ack-ed : %s\n", ACKNOWLEDGED);

    char destFilePath[BUFFER_SIZE];
    /*
        read file from client socket using receiveFileFromSocket with save mode permanent
        if successfully received, inform the client 
    */
    if(receiveFileFromSocket(clientSocket, SAVE_PERMANENT, destFilePath) < 0){
        printf("transfer error : could not receive file from main\n");
        write(clientSocket, TRANSFER_ERROR, strlen(TRANSFER_ERROR));
    } else {
        printf("info : sending transfer success\n");
        write(clientSocket, TRANSFER_SUCCESS, strlen(TRANSFER_SUCCESS));
    }
}

void download(char* filePath, int clientSocket){
    // printf("debug : sending file over to main\n");
    sendFileToSocket(filePath, clientSocket);
}

void delete(char* filePath, int clientSocket){
    deleteLocalFile(filePath, clientSocket, SAVE_PERMANENT);
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

void sendDisplayToSocket(char* dirPath, int clientSocket){
    char localDirPath[BUFFER_SIZE];
    getLocalFilePath(SAVE_PERMANENT, dirPath, localDirPath);

    // ack if directory exists or not
    if (!directoryExists(localDirPath)) {
        printf("info : directory %s doesn't exist\n", localDirPath);
        write(clientSocket, FILE_MISSING, strlen(FILE_MISSING));
        return;
    }
    write(clientSocket, ACKNOWLEDGED, strlen(ACKNOWLEDGED));

    int pipefd[2];
    pid_t pid;
    char buffer[BUFFER_SIZE];

    // find's output goes into this pipe
    if (pipe(pipefd) == -1) {
        printf("internal error : pipe failed\n");
        exit(EXIT_FAILURE);
    }

    pid = fork();
    if (pid == -1) {
        printf("internal error : fork failed\n");
        exit(EXIT_FAILURE);
    }

    if (pid == 0) {  // child : write output of find to pipe
        close(pipefd[0]);
        dup2(pipefd[1], STDOUT_FILENO);
        close(pipefd[1]);
        execlp("find", "find", localDirPath, "-type", "f", NULL);

        // If execlp fails
        printf("internal error : execlp failed\n");
        exit(EXIT_FAILURE);
    } else {  // parent : read and send results of find over socket
        close(pipefd[1]);
        ssize_t bytesRead;
        size_t totalBytes = 0;
        char *output = NULL;

        while ((bytesRead = read(pipefd[0], buffer, BUFFER_SIZE)) > 0) {
            output = realloc(output, totalBytes + bytesRead);
            if (!output) {
                printf("internal error : realloc failed\n");
                exit(EXIT_FAILURE);
            }
            memcpy(output + totalBytes, buffer, bytesRead);
            totalBytes += bytesRead;
        }

        close(pipefd[0]);
        wait(NULL); 
        // printf("debug : display size : %ld\n", totalBytes);
        if (send(clientSocket, &totalBytes, sizeof(size_t), 0) == -1) {  
            printf("transfer error : failed to send meta data - size\n");
            free(output);
            exit(EXIT_FAILURE);
        }

        // printf("debug : sending results to smain\n");
        if (send(clientSocket, output, totalBytes, 0) == -1) {
            printf("internal error : failed to send display results\n");
            free(output);
            exit(EXIT_FAILURE);
        }
        // printf("debug : send complete\n");
        free(output);
    }   
}
void archive(char* filePath, int clientSocket){
        /*
            read file from client socket using receiveFileFromSocket with save mode permanent
            if successfully received, inform the client 
        */
        if(createTar() < 0){
            printf("transfer error : could not receive file from client\n");
        } else {
            char *receivedFilePath = "text.tar";
            sendFileToSocket(receivedFilePath, clientSocket);
        }
}

void prcclient(int mainSocket){
    char buffer[BUFFER_SIZE];
    while(true){
        // read command from client into buffer
        memset(buffer, 0, BUFFER_SIZE);
        int n = read(mainSocket, buffer, BUFFER_SIZE-1); // -1 to give space for \0
        if (n <= 0) {
            printf("internal error : failed to read from socket, exiting client handler...\n");
            return;
        }
        // printf("debug : read command in Stext %s\n", buffer);
        // fill command tokens by splitting buffer using COMMAND_DELIMITER
        char *tokens[MAX_ARGUMENTS];
        int numTokens = 0;
        tokens[numTokens] = strtok(buffer, " ");
        while (tokens[numTokens] != NULL) {
            numTokens++;
            tokens[numTokens] = strtok(NULL, " ");
        }

        if(strcmp(tokens[0], UPLOAD) == 0){
            upload(tokens[1], tokens[2], mainSocket);
        } 
        else if(strcmp(tokens[0], DELETE) == 0){
            delete(tokens[1], mainSocket);
        }else if(strcmp(tokens[0], ARCHIVE) == 0){
            archive(tokens[1], mainSocket);
        }
        else if(strcmp(tokens[0], DOWNLOAD) == 0){
            download(tokens[1], mainSocket);
        } 
        else if(strcmp(tokens[0], DISPLAY) == 0){
            sendDisplayToSocket(tokens[1], mainSocket);
        } 
        else if(strcmp(tokens[0], EXIT) == 0){
            printf("info : exiting stext client handler...\n");
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
    serverAddress.sin_port = htons(TEXT_PORT);                                           
    serverAddress.sin_addr.s_addr = inet_addr(SERVER_IP);

    if (bind(listeningSocket, (struct sockaddr *)&serverAddress, sizeof(serverAddress)) < 0) {
        printf("internal error : failed to bind to socket on port %d, exiting...\n", TEXT_PORT);
        close(listeningSocket);
        exit(1);
    }

    if (listen(listeningSocket, MAX_CLIENTS) < 0) {
        printf("internal error : failed to listen on port %d, exiting...\n", TEXT_PORT);
    }
    printf("info : listening on port %d...\n", TEXT_PORT);

    while(true){
        int connectionSocket = accept(listeningSocket, (struct sockaddr*)NULL, NULL);
        printf("info : accepted connection from a new client\n");

        int pid = fork();
        if (pid < 0) {
            printf("internal error : failed to fork client handler, try again...\n");
        } else if (pid > 0){  // handle new client
            close(listeningSocket);
            prcclient(connectionSocket);
            close(connectionSocket);
            exit(0);
        } else { // close parent side socket fd as it is no longer needed
            close(connectionSocket);
        }
    }
}