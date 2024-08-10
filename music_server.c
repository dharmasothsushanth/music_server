#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <pthread.h>
#include <arpa/inet.h>
#include <stdbool.h>


#define BUFFER_SIZE 1024                 //setting the buffer size usually used in the below code

char root_dir[512];                     //root directory defining as global
int current_number_of_clients = 0;      //keeping track of the number of clients active
bool *is_free;                          // keeping track of the threads free

pthread_mutex_t mutex_lock;             // defining lock globally to be used

typedef struct {
    int client_socket; 
    int thread_used;                               //0 to max_number_streams-1
    char song_name[256];
    struct sockaddr_in client_addr;
}client_info_t;                                    //Structure sended as argument in each thread


void *client_thread_handling(void *arg) {

    client_info_t *client = (client_info_t *)arg;
    char client_ip[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &(client->client_addr.sin_addr), client_ip, INET_ADDRSTRLEN);

    client->song_name[strcspn(client->song_name, "\n")] = '\0';      //printing which client has requested which song
    printf("Client %s requested song: %s\n", client_ip, client->song_name);
    

    char song_path[1024];
    snprintf(song_path, sizeof(song_path), "%s/%s.mp3", root_dir, client->song_name);

    
    FILE *song_file = fopen(song_path, "rb");                  //opening the requested song
    if (song_file == NULL) {
        perror("Error opening song file. Killing Client thread\n");
        close(client->client_socket);
        free(client);
        return NULL;
    }

    char buffer[BUFFER_SIZE];
    ssize_t bytes_read;
    while ((bytes_read = fread(buffer, 1, BUFFER_SIZE, song_file)) > 0) {
        int bytes_sent = send(client->client_socket, buffer, bytes_read, 0);
        if(bytes_sent != bytes_read){
            perror("Not Sent Completely\n");            //if bytes sent is not equal to bytes read then the total data has not been recieved
            break;
        }
    }

    fclose(song_file);
    close(client->client_socket);
    printf("Finished streaming to %s\n", client_ip);

    pthread_mutex_lock(&mutex_lock);                 //critical condition so be performed atomically
    current_number_of_clients--;
    is_free[client->thread_used] = true;
    pthread_mutex_unlock(&mutex_lock);
    free(client);

    return NULL;
}


int get_first_free_thread(int max_streams){    //function which gives you the free thread available which can be used jzt an abstraction for the array of thread variables
    for(int i=0;i<max_streams;i++){
        if(is_free[i] == true){
            return i;
        }
    }
    return -1; //Maximum won't come till now
}



int main(int argc, char * argv[]){
    
    if(argc != 4){
        perror("Invalid Command line arguments\n");
        return 0;
    }

    pthread_mutex_init(&mutex_lock, NULL);         //initialising the lock before using it

    int port_number = atoi(argv[1]);            //server portnumber for tcp connection
    strncpy(root_dir, argv[2], sizeof(root_dir) - 1);    //file path for the songs folder
    root_dir[sizeof(root_dir) - 1] = '\0';
    int max_number_streams = atoi(argv[3]);               //maximum number of clients which can run parallel 
     
    is_free = malloc(sizeof(bool) * max_number_streams);
    for(int i=0;i<max_number_streams;i++){
        is_free[i] = true;                                   //initialising all threads to be free as starting
    }

    int server_socket;
    struct sockaddr_in server_addr;
    
    //Creating Socket
    server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if(server_socket == -1){
        perror("Error creating socket\n");
        return 0;
    }

    //Initializing server address structure
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port_number);
    server_addr.sin_addr.s_addr = INADDR_ANY;

    //Binding socket to address and port
    int binding_test = bind(server_socket, (struct sockaddr *)&server_addr, sizeof(server_addr));
    if(binding_test == -1){
        perror("Error in binding the server socket\n");
        return 0;
    }

    //Listening to the clients
    int listen_test = listen(server_socket, 100);        //Queue capacity 100
    if(listen_test == -1){
        perror("Unable to listen on socket\n");
        return 0;
    }

    printf("Server listening on port number %d\n", port_number);

    //Creating threads for our program to support multithreading
    pthread_t threads[max_number_streams];
    

    int client_socket;
    struct sockaddr_in client_addr;
    socklen_t client_addr_len;

    while(1){
        while(max_number_streams <= current_number_of_clients){}    //if maximum capacity is reached wait until vacancy is available

        client_addr_len = sizeof(client_addr);
        client_socket = accept(server_socket, (struct sockaddr *)&client_addr, &client_addr_len);
        if(client_socket == -1){
            perror("Error in accepting a client\n");
            continue;
        }
        char song_name[256];
        ssize_t bytes_received = recv(client_socket, song_name, sizeof(song_name), 0);  //receiving the song name from the client
        if(bytes_received <=0){
            perror("Error receiving song\n");
            close(client_socket);
            continue;
        }
        song_name[bytes_received] = '\0';

        client_info_t *client = (client_info_t *)malloc(sizeof(client_info_t));   //creating an structure which will be used to pass onto the thread
        client->client_socket = client_socket;

        strncpy(client->song_name, song_name, sizeof(client->song_name) - 1);
        client->song_name[sizeof(client->song_name) - 1] = '\0';
        memcpy(&(client->client_addr), &client_addr, sizeof(client_addr));

        pthread_mutex_lock(&mutex_lock);         //critical section should be performed atomically
        current_number_of_clients++;
        int thread_index = get_first_free_thread(max_number_streams);
        is_free[thread_index] = false;
        client->thread_used = thread_index;
        pthread_create(&threads[thread_index], NULL, client_thread_handling, (void *) client);   //sending as argument for threads
        pthread_mutex_unlock(&mutex_lock);
    }

    close(server_socket);      //closing the server socket
     
    return 0;
}