/******* HEADERS ********/

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <errno.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <pthread.h>
#include <semaphore.h>
#include <time.h>

/********* #defines ***********/

// for status of the person

#define WAITING 0         // waiting for seat to be allocated
#define SEATED 1          // got the seat , watching the match
#define AT_EXIT_GATE 2    // at the exit gate
#define EXITS 3           // exits the stadium
#define BAD_PERFORMANCE 4 // leaves the seat due to poor performance

// for zone of the person ( home , away or neutral )

#define HOME 10    // home zone
#define AWAY 20    // away zone
#define NEUTRAL 30 // neutral zone
// just defined it this way , as it is easy to compare integers than strings

#define MAX_LEN 500 // max len for objects and stuff

/******** COLORS ********/

#define ANSI_RED "\033[1;31m"
#define ANSI_GREEN "\033[1;32m"
#define ANSI_YELLOW "\x1b[33m"
#define ANSI_BLUE "\x1b[34m"
#define ANSI_MAGENTA "\x1b[35m"
#define ANSI_CYAN "\x1b[36m"
#define ANSI_RESET "\x1b[0m"

/******** GLOBAL VARIABLES **********/

int capacity_zone_H;  //capacity for home zone
int capacity_zone_A;  // capacity for away zone
int capacity_zone_N;  // capacity for neutral zone
int spectate_time;    // the time for which a person spectates the match
int num_groups;       // number of groups
int num_goal_chances; // number of goal chances

sem_t H, A, N; // semaphore for respective seat count
// H is Home Stand capacity
// A is Away Stand capacity
// N is Neutral Stand capacity

int away_goals; // goals scored by away team
pthread_mutex_t a_goals = PTHREAD_MUTEX_INITIALIZER;
int home_goals; // goals scored by home team
pthread_mutex_t h_goals = PTHREAD_MUTEX_INITIALIZER;

/*********** STRUCT DEFINATIONS ***********/
// person object
struct person
{
    char name[MAX_LEN]; // name of the person
    int reach_time;     // time taken to reach the stadium
    char zone[2];       // "H" , "N" , "A"    // the side
    int patience;       // patience
    int num_goals;      // number of goals , before it leaves  the game

    int status;              // status , could be one of those #defines ( evident from their name what they mean )
                             // depict current status of the thread
    int group_id;            // group id of the people object
    int id;                  // id [0 - max_people_in group -1 ]
    int counter;             // counter variable  ( usage explained in README ) - check people object part
    int zone_allocated;      // the zone allocated
                             // if person is from Home team it could be HOME or NEUTRAL
                             // if A then could only be AWAY
                             // if N could be any of AWAY , NEUTRAL , or HOME
    struct timespec *t;      // for implementing the thread timed wait
    bool is_allocated;       // booleab expression , so that the person isnt allocated multiple seats simultaneously
    pthread_t p_thread;      // thread object
    pthread_mutex_t mutex;   // lock for critical section
    pthread_cond_t cond_var; // cond_var for avoiding busy sleeping while ,
                             //1. thread is looking for seat
                             // 2. thread is watching match
};

struct group
{
    int id;                        // store the group number
    int num_people;                // number of people in a group
    struct person people[MAX_LEN]; //people details
    int exit_count;                // maintain the count of people waiting at the exit gate
    pthread_cond_t wait_at_gate;
    bool print;
    pthread_mutex_t mutex;
};

struct goals
{
    char team[2];     // "H" or "A"
    int time_elapsed; // time from start after which chance is created
    float chance;     // probability for conversion to a goal
    pthread_t g_thread;
};

bool simulation; // 1 if simulation gng on , else 0
struct group spectator_groups[MAX_LEN];
struct goals goals_list[MAX_LEN];

/***************************HELPER FUNCTIONS***********************************/
// helper function to take input from the user
void take_input()
{
    scanf("%d %d %d", &capacity_zone_H, &capacity_zone_A, &capacity_zone_N);

    sem_init(&H, 0, capacity_zone_H);
    sem_init(&N, 0, capacity_zone_N);
    sem_init(&A, 0, capacity_zone_A);

    scanf("%d", &spectate_time);
    scanf("%d", &num_groups);
    // int x;
    //sem_getvalue(&N, &x);
    // printf("Capacity : %d \n", x);
    for (int i = 0; i < num_groups; i++)
    {
        spectator_groups[i].id = i;
        spectator_groups[i].exit_count = 0;
        pthread_mutex_init(&spectator_groups[i].mutex, NULL);
        pthread_cond_init(&spectator_groups[i].wait_at_gate, NULL);
        scanf("%d", &spectator_groups[i].num_people);
        for (int j = 0; j < spectator_groups[i].num_people; j++)
        {
            spectator_groups[i].people[j].id = j;
            spectator_groups[i].people[j].group_id = i;
            spectator_groups[i].people[j].zone_allocated = -1;
            spectator_groups[i].people[j].is_allocated = false;
            spectator_groups[i].people[j].status = WAITING;
            spectator_groups[i].print = 0;

            scanf("%s %s %d %d %d", spectator_groups[i].people[j].name, spectator_groups[i].people[j].zone, &spectator_groups[i].people[j].reach_time, &spectator_groups[i].people[j].patience, &spectator_groups[i].people[j].num_goals);

            pthread_cond_init(&spectator_groups[i].people[j].cond_var, NULL);
            pthread_mutex_init(&spectator_groups[i].people[j].mutex, NULL);
        }
    }

    scanf("%d", &num_goal_chances);

    for (int i = 0; i < num_goal_chances; i++)
    {
        scanf("%s %d %f", goals_list[i].team, &goals_list[i].time_elapsed, &goals_list[i].chance);
    }
}
// returns the "struct timespec *" , used in sem_timedwait and stuff for waiting till a limit
struct timespec *get_time_struct(int time_val)
{
    struct timespec *ts = (struct timespec *)malloc(sizeof(struct timespec));
    clock_gettime(CLOCK_REALTIME, ts);
    // gettimeofday(&tv, NULL);
    // time_val++;
    // ts->tv_sec = tv.tv_sec;
    // ts->tv_nsec = tv.tv_usec * 1000;
    ts->tv_sec += time_val;
    return ts;
}

/************ THREAD FUNCTIONS *******************/

void *goals_thread(void *arg)
{
    int id = *(int *)arg; // get the goal id
    int time_elapsed = goals_list[id].time_elapsed;
    float chance = goals_list[id].chance;
    sleep(time_elapsed);                        // sleep until the chance arises
    float prob_goal = (float)rand() / RAND_MAX; // random probability
    if (chance >= prob_goal)
    {
        // scored the goal
        if (goals_list[id].team[0] == 'H')
        {
            pthread_mutex_lock(&h_goals);
            home_goals++;
            printf(ANSI_GREEN "Team %s scored their %d goal\n" ANSI_RESET, goals_list[id].team, home_goals);
            pthread_mutex_unlock(&h_goals);
        }
        else if (goals_list[id].team[0] == 'A')
        {
            pthread_mutex_lock(&a_goals);
            away_goals++;
            printf(ANSI_GREEN "Team %s scored their %d goal\n" ANSI_RESET, goals_list[id].team, away_goals);
            pthread_mutex_unlock(&a_goals);
        }
    }
    else
    {
        // goal missed
        if (goals_list[id].team[0] == 'H')
            printf(ANSI_RED "Team %s missed the chance to score their %d goal\n" ANSI_RESET, goals_list[id].team, home_goals + 1);
        else
            printf(ANSI_RED "Team %s missed the chance to score their %d goal\n" ANSI_RESET, goals_list[id].team, away_goals + 1);
    }
    return NULL;
}

void *home_fan_thread(void *arg)
{
    // thread to look for seat into the home stand

    struct person *spectator = (struct person *)arg; // get the argument
    int ret = sem_timedwait(&H, spectator->t);       // wait for the seat , until the patience limit of the spectator
    if (ret == -1)                                   // timed out , patience exhausted
    {
        // timed out , coudlnt get a seat
        // decrement the counter variable and signal the waiting thread

        /**************DEBUG statements ********/
        // printf("%s : is here2 : %d\n", spectator->name, errno);
        // printf("%s : HIIIII\n", spectator->name);
        pthread_mutex_lock(&spectator->mutex);
        spectator->counter--; // decrement the counter variable
        pthread_mutex_unlock(&spectator->mutex);
        pthread_cond_signal(&spectator->cond_var); // signal the main thread
        return NULL;
    }

    // got the seat now
    pthread_mutex_lock(&spectator->mutex);
    if (!spectator->is_allocated)
    {
        // if has not been allocated seat by any fellow threads , allocate a seat
        // printf(ANSI_GREEN "Person %d from group %d has been allocated a seat in stand Home\n" ANSI_RESET, spectator->id, spectator->group_id);
        spectator->is_allocated = true;   // mark the boolean variable
        spectator->status = SEATED;       // change the state
        spectator->zone_allocated = HOME; // allocate the zone
        pthread_mutex_unlock(&spectator->mutex);
        pthread_cond_signal(&spectator->cond_var);
        return NULL;
    }
    else              // if already allocated the seat , dont allocate a new one  increase the semaphore value and exit :)
        sem_post(&H); // if already allocated , then dont allot the seat
    pthread_mutex_unlock(&spectator->mutex);
    return NULL;
}

void *away_fan_thread(void *arg)
{
    // thread to look for seat in the away zone similiar to home_fan_thread
    struct person *spectator = (struct person *)arg;
    int ret = sem_timedwait(&A, spectator->t);
    if (ret != 0)
    {
        // printf("%s : is here1 : %d\n", spectator->name, errno);
        // printf("%s : HIIIII\n", spectator->name);
        pthread_mutex_lock(&spectator->mutex);
        spectator->counter--;
        pthread_mutex_unlock(&spectator->mutex);
        pthread_cond_signal(&spectator->cond_var);

        return NULL;
    }
    // if the person was already allocated seat by some other thread
    pthread_mutex_lock(&spectator->mutex);

    if (!spectator->is_allocated)
    {
        // printf(ANSI_GREEN "Person %d from group %d has been allocated a seat in stand Away\n" ANSI_RESET, spectator->id, spectator->group_id);
        // printf(ANSI_GREEN "%s has got a seat in zone A\n" ANSI_RESET, spectator->name);
        spectator->is_allocated = true;
        spectator->status = SEATED;
        spectator->zone_allocated = AWAY;
        pthread_mutex_unlock(&spectator->mutex);
        pthread_cond_signal(&spectator->cond_var);
        return NULL;
    }
    else
        sem_post(&A); // if already allocated , then dont allot the seat

    pthread_mutex_unlock(&spectator->mutex);
    return NULL;
}

void *neutral_fan_thread(void *arg)
{
    // thread to look for seat in the neutral zone , and similiar to home_fan_thread
    struct person *spectator = (struct person *)arg;
    int x;

    sem_getvalue(&N, &x);
    // printf("%s begin ~~~~~~~~~~%d\n", spectator->name, x);

    int ret = sem_timedwait(&N, spectator->t);

    if (ret == -1)
    {
        // printf("%s : is here : %d\n", spectator->name, errno);
        pthread_mutex_lock(&spectator->mutex);
        spectator->counter--;
        pthread_mutex_unlock(&spectator->mutex);
        pthread_cond_signal(&spectator->cond_var);
        sem_getvalue(&N, &x);
        // printf("%s errr ~~~~~~~~~~%d\n", spectator->name, x);

        return NULL;
    }
    // if the person was already allocated seat by some other thread
    pthread_mutex_lock(&spectator->mutex);
    if (!spectator->is_allocated)
    {
        //printf(ANSI_GREEN "Person %d from group %d has been allocated a seat in stand Neutral\n" ANSI_RESET, spectator->id, spectator->group_id);
        // printf(ANSI_GREEN "%s has got a seat in zone N\n" ANSI_RESET, spectator->name);
        spectator->is_allocated = true;
        spectator->status = SEATED;
        spectator->zone_allocated = NEUTRAL;
        pthread_mutex_unlock(&spectator->mutex);
        pthread_cond_signal(&spectator->cond_var);
        sem_getvalue(&N, &x);
        // printf("%s succcc ~~~~~~~~~~%d\n", spectator->name, x);

        return NULL;
    }
    else
        sem_post(&N); // if already allocated , then dont allot the seat
    pthread_mutex_unlock(&spectator->mutex);
    sem_getvalue(&N, &x);
    // printf("%s end ~~~~~~~~~~%d\n", spectator->name, x);

    return NULL;
}

void *signal_thread(void *arg)
{
    // the thread that signals the people thread once goal limit they can bear has been reached
    while (simulation)
    {
        pthread_mutex_lock(&a_goals);
        pthread_mutex_lock(&h_goals);
        for (int i = 0; i < num_groups; i++)
        {
            for (int j = 0; j < spectator_groups[i].num_people; j++)
            {
                int goal_limit = spectator_groups[i].people[j].num_goals;
                if (spectator_groups[i].people[j].status == WAITING || spectator_groups[i].people[j].status == SEATED) // check the current status of the spectator
                    if (spectator_groups[i].people[j].zone[0] == 'H')
                    {
                        if (goal_limit <= away_goals)
                        {
                            // check the goal limit of the spectator
                            pthread_mutex_lock(&spectator_groups[i].people[j].mutex);
                            spectator_groups[i].people[j].status = BAD_PERFORMANCE; // change the status
                            pthread_mutex_unlock(&spectator_groups[i].people[j].mutex);
                            //printf("Signalling %s in stand H\n", spectator_groups[i].people[j].name);
                            pthread_cond_signal(&spectator_groups[i].people[j].cond_var); // send signal
                        }
                    }
                    else if (spectator_groups[i].people[j].zone[0] == 'A')
                    {
                        // check goal limit of the spectator
                        if (goal_limit <= home_goals)
                        {
                            // if more , then signa the waiting thread and change the status
                            pthread_mutex_lock(&spectator_groups[i].people[j].mutex);
                            spectator_groups[i].people[j].status = BAD_PERFORMANCE;
                            //         printf("Signalling %s in stand A\n", spectator_groups[i].people[j].name);
                            pthread_mutex_unlock(&spectator_groups[i].people[j].mutex);
                            pthread_cond_signal(&spectator_groups[i].people[j].cond_var);
                        }
                    }
            }
        }
        pthread_mutex_unlock(&a_goals);
        pthread_mutex_unlock(&h_goals);
    }
    return NULL;
}

void vacate_seat(int zone)
{
    switch (zone)
    {
    case HOME:
        sem_post(&H);
        break;
    case AWAY:
        sem_post(&A);
        break;
    case NEUTRAL:
        sem_post(&N);
        break;
    default:
        break;
    }
    return;
}

int find_seat(int zone, struct person *spectator)
{
    pthread_t home_thread;
    pthread_t away_thread;
    pthread_t neutral_thread;
    if (zone == HOME)
    {
        // if home fan , check for seat in home and neutral thread
        spectator->counter = 2;
        spectator->t = get_time_struct(spectator->patience);
        pthread_create(&home_thread, NULL, home_fan_thread, (void *)(spectator));
        pthread_create(&neutral_thread, NULL, neutral_fan_thread, (void *)(spectator));
    }
    else if (zone == AWAY)
    {
        // if away check only in away thread
        spectator->counter = 1;
        spectator->t = get_time_struct(spectator->patience);
        pthread_create(&away_thread, NULL, away_fan_thread, (void *)(spectator));
    }
    else if (zone == NEUTRAL)
    {
        // if neutral check in home and away thread and neutral thread
        spectator->counter = 3;
        spectator->t = get_time_struct(spectator->patience);
        // if (strcmp(spectator->name, "Roshan") == 0)
        // printf("%ld : %ld \n", spectator->t->tv_nsec, spectator->t->tv_sec);

        pthread_create(&neutral_thread, NULL, neutral_fan_thread, (void *)(spectator));
        pthread_create(&home_thread, NULL, home_fan_thread, (void *)(spectator));
        pthread_create(&away_thread, NULL, away_fan_thread, (void *)(spectator));
    }
    else
    {
        printf(ANSI_RED "Not a valid zone type , fatal error\n" ANSI_RESET);
        return -1;
    }
    return 0;
}

void *spectator_thread(void *arg)
{
    // to simulate the the spectator using threads
    // for each spectator , we spawn multiple threads that could be used to find seats in the allowed zones at the same time
    // after a seat is allocated to the person , the person goes and sits
    // There's no need for a loop or smth for the spectatotr thread , since anyways

    struct person *spectator = (struct person *)arg; // get the spectator object
    int group_id = spectator->group_id;
    int spec_id = spectator->id;
    int time_arrive = spectator->reach_time;
    spectator->status = WAITING;
    char team[2];
    int zone;

    strcpy(team, spectator->zone);
    // assign the zone
    if (strcmp(team, "H") == 0)
        zone = HOME;
    else if (strcmp(team, "A") == 0)
        zone = AWAY;
    else
        zone = NEUTRAL;

    // wait for arrival of the thread
    int ret = sleep(time_arrive);
    // printf("Slept for time %s : %d\n", spectator->name, time_arrive);
    assert(ret == 0);
    // printf("####%s : %d\n", spectator->name, spectator->t->tv_sec);
    // printf("**********%s : %ld***********\n", spectator->name, ts.tv_sec);

    int goal_flag;
    pthread_mutex_lock(&a_goals);
    pthread_mutex_lock(&h_goals);

    if (zone == HOME)
        goal_flag = away_goals < spectator->num_goals;
    else if (zone == AWAY)
        goal_flag = home_goals < spectator->num_goals;
    else
        goal_flag = true;

    pthread_mutex_unlock(&h_goals);
    pthread_mutex_unlock(&a_goals);

    printf(ANSI_MAGENTA "%s has reached the stadium\n" ANSI_RESET, spectator->name);

    // before entring the simulation , check if the goal condition is violated , if yes , leave immediately
    // does not hold true for NEUTRAL fan thread
    if (zone != NEUTRAL && !goal_flag)
    {
        spectator->status = BAD_PERFORMANCE;
        printf(ANSI_RED "%s is leaving due to bad performance of his team\n" ANSI_RESET, spectator->name);
        goto exit_gate;
    }

    spectator->zone_allocated = -1;

    if (spectator->status == WAITING && goal_flag) // spawn the threads corresponding to each of the zones , that signal the threads to wait for the seats
        find_seat(zone, spectator);

    pthread_mutex_lock(&spectator->mutex);

    while (spectator->counter > 0 && spectator->is_allocated == 0 && spectator->status == WAITING)
    {
        // go on sleep , until all the threads have timed out ( in that case counter == 0 and is_allocated = 0 as well)
        // else if seat gets allocated by any of the threads , this thread is signalled wiith is_allocated set to 1
        // leaves to watch the match immediately
        pthread_cond_wait(&spectator->cond_var, &spectator->mutex);
    }
    // breaks from the loop in 3 cases
    // 1. if the spectator is allocated a seat
    // 2. if the spectator is not allocated a seat ( and counter is set to 0 , all threads failed to get the seat )
    // 3. if team performs poorly in that case , status set to bad performance

    if (spectator->status == BAD_PERFORMANCE) // condition 3
    {
        printf(ANSI_RED "%s is leaving due to bad performance of his team\n" ANSI_RESET, spectator->name);
        pthread_mutex_unlock(&spectator->mutex);
        spectator->status = AT_EXIT_GATE;
        goto exit_gate;
    }
    if (spectator->is_allocated == 0) // condition 2
    {
        // could not be allotted seat leave for the exit gate
        printf(ANSI_RED "%s could not get a seat\n" ANSI_RESET, spectator->name);
        spectator->status = AT_EXIT_GATE;
        pthread_mutex_unlock(&spectator->mutex);
        goto exit_gate;
    }

    // allocated the seat
    pthread_mutex_unlock(&spectator->mutex);
    if (spectator->status == SEATED && spectator->is_allocated)
    {
        // if allocated the zeat , print the corresponding message
        if (spectator->zone_allocated == HOME)
            printf(ANSI_GREEN "%s has got a seat in zone H\n" ANSI_RESET, spectator->name);
        else if (spectator->zone_allocated == AWAY)
            printf(ANSI_GREEN "%s has got a seat in zone A\n" ANSI_RESET, spectator->name);
        else if (spectator->zone_allocated == NEUTRAL)
            printf(ANSI_GREEN "%s has got a seat in zone N\n" ANSI_RESET, spectator->name);
    }
    // watch the match , and empty if done watching the match for max limit
    pthread_mutex_lock(&spectator->mutex);

    struct timespec *ts1 = get_time_struct(spectate_time);
    do
    {
        // keep watching the match until , the time limit is reached or goal performance thing is violated
        ret = pthread_cond_timedwait(&spectator->cond_var, &spectator->mutex, ts1);
    } while (ret != ETIMEDOUT && spectator->status == SEATED);

    pthread_mutex_unlock(&spectator->mutex);

    // student could have either completed the time limit or could have bad performance vala thing
    // vacate the space
    vacate_seat(spectator->zone_allocated);

    if (spectator->status == BAD_PERFORMANCE)
    {
        // signalled by the goal thread or something , to leave the
        spectator->status = AT_EXIT_GATE;
        printf(ANSI_RED "%s is leaving due to bad performance of his team\n" ANSI_RESET, spectator->name);
        goto exit_gate;
    }
    if (ret != 0)
    {
        spectator->status = AT_EXIT_GATE;
        printf(ANSI_YELLOW "%s watched the match for %d seconds and is leaving\n" ANSI_RESET, spectator->name, spectate_time);
        goto exit_gate;
    }

exit_gate:

    printf(ANSI_BLUE "%s is waiting for friends at exit\n" ANSI_RESET, spectator->name);
    pthread_mutex_lock(&spectator_groups[group_id].mutex);
    spectator_groups[group_id].exit_count++;
    pthread_cond_broadcast(&spectator_groups[group_id].wait_at_gate);
    while (spectator_groups[group_id].exit_count != spectator_groups[group_id].num_people)
    {
        pthread_cond_wait(&spectator_groups[group_id].wait_at_gate, &spectator_groups[group_id].mutex);
    }
    if (!spectator_groups[group_id].print)
    {
        printf(ANSI_BLUE "Group %d is leaving for dinner\n" ANSI_RESET, group_id);
        spectator_groups[group_id].print = 1;
    }
    pthread_mutex_unlock(&spectator_groups[group_id].mutex);

    return NULL;
}

int main()
{
    srand(time(0));
    take_input();
    simulation = true;
    home_goals = 0;
    away_goals = 0;

    pthread_t goal_check;
    pthread_create(&goal_check, NULL, signal_thread, NULL);

    for (int i = 0; i < num_goal_chances; i++)
    {
        int *id = (int *)malloc(sizeof(int));
        *id = i;
        pthread_create(&goals_list[i].g_thread, NULL, goals_thread, (void *)id);
    }
    for (int i = 0; i < num_groups; i++)
    {
        for (int j = 0; j < spectator_groups[i].num_people; j++)
        {
            pthread_create(&spectator_groups[i].people[j].p_thread, NULL, spectator_thread, (void *)&spectator_groups[i].people[j]);
        }
    }
    // if the team of the person has scored more goals than he could digest he leaves the simulation and waits at the exit gate until all the people arrive

    for (int i = 0; i < num_groups; i++)
    {
        for (int j = 0; j < spectator_groups[i].num_people; j++)
        {
            pthread_join(spectator_groups[i].people[j].p_thread, NULL);
        }
    }

    simulation = false;
    return 0;
}