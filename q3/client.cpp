#include "header.h"
using namespace std;

#define server_port 8001
#define buffer_max_sz 1048576

typedef struct client_request
{
    int i;
    pthread_t tid;
    int client_fd;
    int req_time;
    string command;
    pthread_mutex_t mutex;
} client_request;

vector<client_request> clients;

pthread_mutex_t terminal = PTHREAD_MUTEX_INITIALIZER;

int get_client_socket_fd()
{
    struct sockaddr_in server_obj;
    int socket_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (socket_fd < 0)
    {
        perror("Error in socket creation for CLIENT");
        exit(-1);
    }
    int port_num = server_port;
    memset(&server_obj, 0, sizeof(server_obj)); // Zero out structure
    server_obj.sin_family = AF_INET;
    server_obj.sin_port = htons(port_num); // convert to big-endian order
    if (connect(socket_fd, (struct sockaddr *)&server_obj, sizeof(server_obj)) < 0)
    {
        perror("Problem in connecting to the server");
        exit(-1);
    }
    return socket_fd;
}

void *client_thread(void *(arg))
{
    int idx = *(int *)arg;
    // cout << "CLIENT MEIN" << endl;
    clients[idx].client_fd = get_client_socket_fd();
    int client_fd = clients[idx].client_fd;
    // pthread_mutex_lock(&map_lock);
    // user_idx.insert({client_fd, idx});
    // pthread_mutex_unlock(&map_lock);
    int req_time = clients[idx].req_time;
    string command = clients[idx].command;
    // cout << "going_to_sleep" << endl;

    sleep(req_time);

    pthread_mutex_lock(&clients[idx].mutex);
    // send request to server
    int x = write(client_fd, command.c_str(), command.length());
    // cout << "DATA sent" << endl;
    // cout << "COmmand :  " << command << endl;
    if (x < 0)
    {
        cerr << "Failed To communicate with the server" << endl;
        pthread_mutex_unlock(&clients[idx].mutex);
        return NULL;
    }

    pthread_mutex_unlock(&clients[idx].mutex);

    // read response from the server

    pthread_mutex_lock(&clients[idx].mutex);

    string buffer;
    buffer.resize(buffer_max_sz);
    int byte_read = read(client_fd, &buffer[0], buffer_max_sz - 1);
    buffer[byte_read] = '\0';
    buffer.resize(byte_read);
    if (byte_read <= 0)
    {
        cerr << "Failed To communicate with the server" << endl;
        pthread_mutex_unlock(&clients[idx].mutex);
        return NULL;
    }
    pthread_mutex_lock(&terminal);
    cout << clients[idx].i << " : " << gettid() << " : " << buffer << endl;
    pthread_mutex_unlock(&terminal);

    pthread_mutex_unlock(&clients[idx].mutex);

    return NULL;
}

int main()
{
    int num_clients;
    cin >> num_clients;
    // map_lock = PTHREAD_MUTEX_INITIALIZER;
    //cout << "NUm clients " << num_clients << endl;
    // get the input
    string x;
    getline(cin, x);
    for (int i = 0; i < num_clients; i++)
    {
        string str, time_req = "";
        getline(cin, str);
        auto itr = str.begin();
        while (*itr != ' ')
        {
            time_req += *itr;
            itr++;
        }
        itr++;
        client_request tmp;
        tmp.req_time = stoi(time_req);
        string command(itr, str.end());
        tmp.command = command;
        clients.push_back(tmp);
    }
    // get the connection sockets
    for (auto i = 0; i < num_clients; i++)
    {
        clients[i].i = i;
        clients[i].mutex = PTHREAD_MUTEX_INITIALIZER;
    }
    for (auto i = 0; i < num_clients; i++)
    {
        int *idx = new int;
        *idx = i;
        pthread_create(&clients[i].tid, NULL, client_thread, (void *)idx);
    }

    for (auto i = 0; i < num_clients; i++)
    {
        pthread_join(clients[i].tid, NULL);
    }

    return 0;
}