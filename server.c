#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <sys/sendfile.h>
#include <openssl/md5.h>
#include <json-c/json.h>


#define PORT 8888
#define BUFFER_SIZE 1024

void calculate_md5(char *filename, char *md5_str) {
    unsigned char c[MD5_DIGEST_LENGTH];
    // char *filename="file.txt";
    int i;
    FILE *inFile = fopen (filename, "rb");
    MD5_CTX mdContext;
    int bytes;
    unsigned char data[1024];

    if (inFile == NULL) {
        printf ("%s can't be opened.\n", filename);
        //return 0;
    }

    MD5_Init (&mdContext);
    while ((bytes = fread (data, 1, 1024, inFile)) != 0)
    {
        MD5_Update (&mdContext, data, bytes);
    }
        
    MD5_Final (c,&mdContext);

    for(i = 0; i < MD5_DIGEST_LENGTH; i++) {
        sprintf(&md5_str[i*2], "%02x", c[i]); // chuyển đổi từng phần tử unsigned char sang chuỗi hexa và nối chúng lại
    }
    md5_str[MD5_DIGEST_LENGTH*2] = '\0'; // thêm kí tự null terminator

    //printf ("%s %s\n", md5_str, filename); // xuất chuỗi MD5 và tên file
    md5_str[i*2] = '\0';
    
    fclose (inFile);
}
int create_socket() {
    int server_fd;
    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
        perror("socket failed");
        exit(EXIT_FAILURE);
    }
    return server_fd;
}
void set_socket_options(int socket_fd) {
    int opt = 1;
    if (setsockopt(socket_fd, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt, sizeof(opt))) {
        perror("setsockopt");
        exit(EXIT_FAILURE);
    }
}
void bind_socket(int socket_fd, struct sockaddr_in address) {
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(PORT);

    if (bind(socket_fd, (struct sockaddr *)&address, sizeof(address))<0) {
        perror("bind failed");
        exit(EXIT_FAILURE);
    }
}
void listen_for_connections(int socket_fd) {
    if (listen(socket_fd, 3) < 0) {
        perror("listen");
        exit(EXIT_FAILURE);
    }
}
void send_file_info(int new_socket, char* filename) {
    char md5_str[MD5_DIGEST_LENGTH*2 +1];
    int file_descriptor = open(filename, O_RDONLY);
    if (file_descriptor == -1) {
        perror("Failed to open file");
        exit(EXIT_FAILURE);
    }

    int file_size = lseek(file_descriptor, 0, SEEK_END);
    lseek(file_descriptor, 0, SEEK_SET);
    calculate_md5(filename, md5_str);       
    printf("md5: %s\n",md5_str);
    json_object *info_file = json_object_new_object();
    // thêm thông tin của file vào đối tượng JSON
    json_object_object_add(info_file, "filename", json_object_new_string(filename));
    json_object_object_add(info_file, "file_size", json_object_new_int(file_size));
    json_object_object_add(info_file, "md5_str", json_object_new_string(md5_str));

    // gửi Json tới client
    const char *json_str = json_object_to_json_string(info_file);
    int bytes = send(new_socket, json_str, strlen(json_str), 0);
    json_object_put(info_file);
}

void send_file_to_client(int new_socket, const char *filename) {
    // Mở file
    int file_descriptor = open(filename, O_RDONLY);
    if (file_descriptor == -1) {
        perror("Failed to open file");
        exit(EXIT_FAILURE);
    }

    // Gửi dữ liệu từ file tới client
    off_t offset = 0;
    ssize_t sent_bytes = 0;
    while ((sent_bytes = sendfile(new_socket, file_descriptor, &offset, BUFFER_SIZE)) > 0) {
        printf("Sent %ld bytes of file\n", sent_bytes);
    }
    
    // Đóng file descriptor
    close(file_descriptor);
}

int main(int argc, char const *argv[]) {
    int server_fd, new_socket;
    struct sockaddr_in address;
    int opt = 1;
    int addrlen = sizeof(address);
    char buffer[1024] = {0};
    char message[BUFFER_SIZE];
    int num_requests = 0;
    // Tạo socket
    server_fd = create_socket();
    // Đặt các tùy chọn cho socket
    set_socket_options(server_fd);
    // Gán địa chỉ cho socket
    bind_socket(server_fd, address);
    // Lắng nghe kết nối từ Client
    listen_for_connections(server_fd);
    printf("Server is listening on port %d...\n", PORT);
    while(1) {
        // Chấp nhận kết nối mới
        if ((new_socket = accept(server_fd, (struct sockaddr *)&address, (socklen_t*)&addrlen))<0) {
            perror("accept");
            exit(EXIT_FAILURE);
        }
        printf("Handling request #%d from %s:%d\n", ++num_requests, inet_ntoa(address.sin_addr), ntohs(address.sin_port));

        // Xử lý request từ Client
        int num_bytes_received = recv(new_socket, buffer, BUFFER_SIZE, 0);
        if(num_bytes_received == -1 ) {
            printf("Failed");
            exit(EXIT_FAILURE);
        }
        else {
            printf("%s\n", buffer);
        }
        // clear response buffer
        memset(buffer, 0, BUFFER_SIZE);

        // Đọc dữ liệu JSON từ tập tin
        FILE *fp;
        fp = fopen("data.json", "r");
        char message[1024];
        fread(message, 1024, 1, fp);
        fclose(fp);
        // Chuyển đổi chuỗi JSON thành đối tượng JSON
        json_object *parsed_json = json_tokener_parse(message);
        const char *request = json_object_get_string(json_object_object_get(parsed_json, "fileame"));
        //gửi request tới client
        send(new_socket, request, strlen(request), 0);
        // nhận tên file mà client gửi đến
        char filename[BUFFER_SIZE];
        memset(filename, 0, BUFFER_SIZE);
        recv(new_socket, filename, BUFFER_SIZE, 0);
        printf("File is requested to send: %s\n",filename);
        memset(buffer, 0, BUFFER_SIZE);

        // gửi thông tin file cho Client
        send_file_info(new_socket, filename);

        // nhận message : "Please send file";
        memset(buffer, 0, BUFFER_SIZE);
        recv(new_socket, buffer, BUFFER_SIZE, 0);
        printf("Client response: %s\n",buffer);
        memset(buffer, 0, BUFFER_SIZE);
        
        //gửi file đến client
        send_file_to_client(new_socket, filename);

        //nhận phản hồi từ Client xem đã dowload File thành công hay chưa
        recv(new_socket, buffer, BUFFER_SIZE, 0);
        printf("Client response: %s\n",buffer);
        memset(buffer, 0, BUFFER_SIZE);
        if(strcmp(buffer,"Dowload Done") == 0) {
                close(server_fd);
        }
        close(new_socket);
    }
    return 0;
}
