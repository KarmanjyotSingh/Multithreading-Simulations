#include "header.h"
using namespace std;

#define max_clients 100
#define port_number 8001
#define buff_sz 1048576
#define MAX_DICT_SIZE 100
int num_worker_threads;

struct dictionary_node
{
    string str;
    int id;
    int is_active;
    pthread_mutex_t mutex;
};

vector<struct dictionary_node> dict(MAX_DICT_SIZE);
queue<int *> q;
pthread_mutex_t queue_lock = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t cond_var = PTHREAD_COND_INITIALIZER;

int send_string_on_socket(int fd, const string &s)
{
    string str = to_string(gettid()) + " : " + s;
    int bytes_sent = write(fd, str.c_str(), str.length());
    if (bytes_sent < 0)
    {
        cerr << "Failed to SEND DATA via socket.\n";
    }

    return bytes_sent;
}

void function_handler(string s, int client_socket_fd)
{
    char *cpy_in = (char *)malloc((s.length() + 1) * sizeof(char));
    strcpy(cpy_in, s.c_str());
    cpy_in[s.length()] = '\0';
    char *str = strtok_r(cpy_in, " ", &cpy_in);
    int len = s.length();
    char *arguments[len];
    int arg_count = 0;
    while (str != NULL)
    {
        arguments[arg_count] = (char *)malloc((strlen(str) + 1) * sizeof(char));
        strcpy(arguments[arg_count], str);
        arg_count++;
        str = strtok_r(cpy_in, " ", &cpy_in);
    }

    if (arg_count == 0)
        return;
    arguments[arg_count] = NULL;
    if (strcmp(arguments[0], "insert") == 0)
    {
        if (arg_count != 3)
        {
            cout << "Invalid Arguments for insert" << endl;
            return;
        }
        int key = atoi(arguments[1]);
        pthread_mutex_lock(&dict[key].mutex);
        if (dict[key].is_active == 0)
        {
            dict[key].str = arguments[2];
            dict[key].is_active = 1;
            cout << "Insertion Succesful" << arguments[2] << " at " << key << endl;
            send_string_on_socket(client_socket_fd, "Insertion Succesful");
        }
        else
        {
            cout << "Key already present" << endl;
            send_string_on_socket(client_socket_fd, "Key already present");
        }
        pthread_mutex_unlock(&dict[key].mutex);
    }
    else if (strcmp(arguments[0], "concat") == 0)
    {
        if (arg_count != 3)
        {
            cout << "Invalid Arguments for concat" << endl;
            return;
        }
        int key1 = atoi(arguments[1]);
        int key2 = atoi(arguments[2]);
        pthread_mutex_lock(&dict[key1].mutex);
        pthread_mutex_lock(&dict[key2].mutex);
        if (dict[key1].is_active == 1 && dict[key2].is_active == 1)
        {
            string tmp = dict[key1].str;
            dict[key1].str = dict[key1].str + dict[key2].str;
            dict[key2].str += tmp;
            cout << "Concatenation Succesful" << endl;
            send_string_on_socket(client_socket_fd, dict[key2].str);
        }
        else
        {
            cout << "Either one of the keys is not present" << endl;
            send_string_on_socket(client_socket_fd, "Conact Failed as Either one of the keys is not present");
        }
        pthread_mutex_unlock(&dict[key1].mutex);
        pthread_mutex_unlock(&dict[key2].mutex);
    }
    else if (strcmp(arguments[0], "fetch") == 0)
    {
        if (arg_count != 2)
        {
            cout << "Invalid Arguments for fetch" << endl;
            return;
        }
        int key = atoi(arguments[1]);
        pthread_mutex_lock(&dict[key].mutex);
        if (dict[key].is_active == 1)
        {
            cout << "Fetch Succesful" << endl;
            send_string_on_socket(client_socket_fd, dict[key].str);
        }
        else
        {
            cout << "Key doesn't exist" << endl;
            send_string_on_socket(client_socket_fd, "Key Doesn't Exist");
        }
        pthread_mutex_unlock(&dict[key].mutex);
    }
    else if (strcmp(arguments[0], "update") == 0)
    {
        if (arg_count != 3)
        {
            cout << "Invalid Arguments for update" << endl;
            return;
        }
        int key = atoi(arguments[1]);
        pthread_mutex_lock(&dict[key].mutex);
        if (dict[key].is_active == 1)
        {
            dict[key].str = arguments[2];
            cout << "Update Succesful" << endl;
            send_string_on_socket(client_socket_fd, dict[key].str);
        }
        else
        {
            cout << "Key doesn't exist" << endl;
            send_string_on_socket(client_socket_fd, "Key Doesn't Exist");
        }
        pthread_mutex_unlock(&dict[key].mutex);
    }
    else if (strcmp(arguments[0], "delete") == 0)
    {
        if (arg_count != 2)
        {
            cout << "Invalid Arguments for delete" << endl;
            return;
        }
        int key = atoi(arguments[1]);
        pthread_mutex_lock(&dict[key].mutex);
        if (dict[key].is_active == 1)
        {
            dict[key].is_active = 0;
            dict[key].str = "";
            cout << "Deletion Succesful" << endl;
            send_string_on_socket(client_socket_fd, "Deletion Succesful");
        }
        else
        {
            cout << "Key doesn't exist" << endl;
            send_string_on_socket(client_socket_fd, "No such Key");
        }
        pthread_mutex_unlock(&dict[key].mutex);
    }
}

void handle_client(int client_fd)
{
    string buffer;
    buffer.resize(buff_sz);
    pthread_mutex_lock(&map_lock);
    // int idx = user_idx[client_fd];
    pthread_mutex_unlock(&map_lock);

    // pthread_mutex_lock(&client_lock[idx]);
    int byte_read = read(client_fd, &buffer[0], buff_sz - 1);
    // pthread_mutex_unlock(&client_lock[idx]);

    //cout << "Read Value" << buffer << endl;
    buffer[byte_read] = '\0';
    buffer.resize(byte_read);
    if (byte_read <= 0)
    {
        cerr << "Failed To communicate with the server" << endl;
        return;
    }

    function_handler(buffer, client_fd);
}

void *worker_thread(void *arg)
{
    // int sockfd = *((int *)arg);
    char buffer[buff_sz];
    int n;
    while (1)
    {
        pthread_mutex_lock(&queue_lock);
        while (q.empty())
        {
            pthread_cond_wait(&cond_var, &queue_lock);
        }
        int *client_sockfd = q.front();
        q.pop();
        pthread_mutex_unlock(&queue_lock);
        handle_client(*client_sockfd);
    }
    return NULL;
}

void init_server_socket()
{
    struct sockaddr_in serv_addr_obj, client_addr_obj;

    int server_socket_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_socket_fd < 0)
    {
        perror("ERROR creating welcoming socket");
        exit(-1);
    }
    bzero((char *)&serv_addr_obj, sizeof(serv_addr_obj));
    int PORT_NUMBER = port_number;
    serv_addr_obj.sin_family = AF_INET;
    serv_addr_obj.sin_addr.s_addr = INADDR_ANY;
    serv_addr_obj.sin_port = htons(PORT_NUMBER);

    if (bind(server_socket_fd, (struct sockaddr *)&serv_addr_obj, sizeof(serv_addr_obj)) < 0)
    {
        perror("Error on bind on welcome socket: ");
        exit(-1);
    }

    listen(server_socket_fd, max_clients);
    cout << "Server has started listening on the LISTEN PORT" << endl;
    socklen_t clilen = sizeof(client_addr_obj);

    while (1)
    {
        printf("Waiting for a new client to request for a connection\n");
        int client_socket_fd = accept(server_socket_fd, (struct sockaddr *)&client_addr_obj, &clilen);
        cout << "ok" << endl;
        if (client_socket_fd < 0)
        {
            perror("ERROR while accept() system call occurred in SERVER");
            exit(-1);
        }

        printf("New client connected from port number %d and IP %s \n", ntohs(client_addr_obj.sin_port), inet_ntoa(client_addr_obj.sin_addr));

        int *pclient = (int *)malloc(sizeof(int));
        *pclient = client_socket_fd;

        pthread_mutex_lock(&queue_lock);
        q.push(pclient);
        pthread_mutex_unlock(&queue_lock);
        pthread_cond_signal(&cond_var);
    }

    close(server_socket_fd);
}

int main(int argc, char *argv[])
{
    num_worker_threads = stoi(argv[1]);

    pthread_t wkr_thread[num_worker_threads];
    for (auto itr = 0; itr < MAX_DICT_SIZE; itr++)
    {
        dict[itr].id = itr;
        dict[itr].is_active = 0;
        dict[itr].str = "";
        pthread_mutex_init(&dict[itr].mutex, NULL);
    }
    for (auto i = 0; i < num_worker_threads; i++)
    {
        pthread_create(&wkr_thread[i], NULL, worker_thread, NULL);
    }
    cout << "ok bro " << endl;
    init_server_socket();
}
