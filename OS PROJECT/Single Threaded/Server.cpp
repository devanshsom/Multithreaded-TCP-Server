#include <iostream>
#include <string>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>

class Server {
public:
    void run() {
        int server_fd, new_socket;
        long valread;
        struct sockaddr_in address;
        int addrlen = sizeof(address);
        
        char buffer[30000] = {0};
        const char* hello = "Hello from the server!";
        
        // Creating socket file descriptor
        if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
            perror("socket failed");
            exit(EXIT_FAILURE);
        }
        
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = INADDR_ANY;
        address.sin_port = htons(8080);
        
        memset(address.sin_zero, '\0', sizeof address.sin_zero);
        
        if (bind(server_fd, (struct sockaddr *)&address, sizeof(address))<0) {
            perror("bind failed");
            exit(EXIT_FAILURE);
        }
        
        if (listen(server_fd, 10) < 0) {
            perror("listen");
            exit(EXIT_FAILURE);
        }
        
        while(true) {
            std::cout << "\n+++++++ Waiting for new connection ++++++++\n\n";
            if ((new_socket = accept(server_fd, (struct sockaddr *)&address,
                                      (socklen_t*)&addrlen))<0) {
                perror("accept");
                exit(EXIT_FAILURE);
            }
            
            valread = read(new_socket, buffer, 30000);
            std::cout << buffer << std::endl;
            write(new_socket, hello, strlen(hello));
            std::cout << "------------------Hello message sent-------------------" << std::endl;
            
            close(new_socket);
        }
    }
};

int main() {
    Server server;
    server.run();
    return 0;
}
