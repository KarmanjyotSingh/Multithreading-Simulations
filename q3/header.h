#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <netinet/in.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <bits/stdc++.h>
#include <iostream>
#include <assert.h>
#include <tuple>
#include <pthread.h>
using namespace std;

pthread_mutex_t *client_lock;
pthread_mutex_t map_lock;
map<int, int> user_idx;