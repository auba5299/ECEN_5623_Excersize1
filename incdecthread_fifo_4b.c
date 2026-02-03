#define _GNU_SOURCE // for CPU_ZERO and CPU_SET macros from <sched.h>
#include <pthread.h>
#include <stdlib.h>
#include <stdio.h>
#include <sched.h>
#include <unistd.h>    //POSIX system calls such as sleep and usleep but not n-sleep
#include <sys/types.h> //OS types
#include <errno.h>     //for use of system error calls
#include <sys/time.h>  //legacy - use time.h and clock_gettime in excersizes

#define COUNT 1000

typedef struct
{
    int threadIdx;
} threadParams_t;

// POSIX thread declarations and scheduling attributes
//
pthread_t starterthread; // thread to start workers
pthread_t mainthread;    // scheduling thread
pthread_t threads[2];
threadParams_t threadParams[2];

pthread_attr_t fifo_sched_attr; // attribute to be configured for fifo, affinity, priority
struct sched_param fifo_param;  // sched_param struct defined in <sched.h> that simply contains pri lvl as int

// Unsafe global
int gsum = 0;

#define SCHED_POLICY SCHED_FIFO

/*all treads in a process share sched policy, by using 3 lines below,
we pull policy for one which is applicable to all*/
void print_scheduler(void)
{
    int schedType = sched_getscheduler(getpid());
    // switch int values linked to macros from <sched.h>
    switch (schedType)
    {
    case SCHED_FIFO:
        printf("Pthread policy is SCHED_FIFO\n");
        break;
    case SCHED_OTHER:
        printf("Pthread policy is SCHED_OTHER\n");
        break;
    case SCHED_RR:
        printf("Pthread policy is SCHED_RR\n");
        break;
    default:
        printf("Pthread policy is UNKNOWN\n");
    }
}

void set_scheduler(void)
{
    int max_prio, scope, rc, cpuidx;
    cpu_set_t cpuset;

    printf("INITIAL ");
    print_scheduler();

    pthread_attr_init(&fifo_sched_attr);                                    // init attribute object of type pthread_attr_t for our fifo sched params
    pthread_attr_setinheritsched(&fifo_sched_attr, PTHREAD_EXPLICIT_SCHED); // explicitly use selected sched and params below
    pthread_attr_setschedpolicy(&fifo_sched_attr, SCHED_POLICY);            // selected sched = SCHED_FIFO (line 32 define)
    
    // cpu pin to cpu 3
    CPU_ZERO(&cpuset);                                                         // zero out CPU SET to remove default artifacts
    cpuidx = (3);                                                              // CPU3
    CPU_SET(cpuidx, &cpuset);                                                  // add CPU3 to usable CPUs (CPU set)
    pthread_attr_setaffinity_np(&fifo_sched_attr, sizeof(cpu_set_t), &cpuset); // ties cpu3 pin to our fifo_sched_attr

    // assign max priority -10 to fifo_param structure
    max_prio = sched_get_priority_max(SCHED_POLICY); // max prio of SCHED_FIFO is 99
    fifo_param.sched_priority = max_prio - 10;       //goal is to set main to 89

    // process level - sets process scheduling polifcy to
    if ((rc = sched_setscheduler(getpid(), SCHED_POLICY, &fifo_param)) < 0)
        perror("sched_setscheduler");
    // attribute level - when fifo_sched_attr is used in p-thread create, it will have this cpu3 pin, fifo, max prio attributes
    pthread_attr_setschedparam(&fifo_sched_attr, &fifo_param);

    printf("ADJUSTED ");
    print_scheduler();
}

void *incThread(void *threadp)
{
    int i;
    threadParams_t *threadParams = (threadParams_t *)threadp;

    for (i = 0; i < COUNT; i++)
    {
        gsum = gsum + i;
        printf("Increment thread idx=%d, gsum=%d, fifo\n", threadParams->threadIdx, gsum);
    }
}

void *decThread(void *threadp)
{
    int i;
    threadParams_t *threadParams = (threadParams_t *)threadp;

    for (i = 0; i < COUNT; i++)
    {
        gsum = gsum - i;
        printf("Decrement thread idx=%d, gsum=%d, fifo\n", threadParams->threadIdx, gsum);
    }
}

void *starterThread(void *threadp)
{
    int i = 0, rc;

    printf("starter thread running on CPU=%d\n", sched_getcpu());

    fifo_param.sched_priority = sched_get_priority_max(SCHED_POLICY); //inc gets highest priority
    pthread_attr_setschedparam(&fifo_sched_attr, &fifo_param); //update attr to lower priority

    threadParams[i].threadIdx = i;
    pthread_create(&threads[i],               // pointer to thread descriptor
                   &fifo_sched_attr,          // use FIFO RT max priority attributes configured in set_scheduler
                   incThread,                 // thread function entry point
                   (void *)&(threadParams[i]) // parameters to pass in
    );
    i++;

    fifo_param.sched_priority = sched_get_priority_max(SCHED_POLICY) - 1; //lower priority by 1 for decrement thread
    pthread_attr_setschedparam(&fifo_sched_attr, &fifo_param); //update attr to max pri - 1
    //now create decrement at max priority-1
    threadParams[i].threadIdx = i;
    pthread_create(&threads[i], &fifo_sched_attr, decThread, (void *)&(threadParams[i]));

    for (i = 0; i < 2; i++)
        pthread_join(threads[i], NULL);
}

int main(int argc, char *argv[])
{
    int rc;
    int i, j;
    cpu_set_t cpuset;

    /*call set_scheduler to make process FIFO highest pri-10, configure attributes
    for FIFO max pri-10 to use in thread creation*/
    set_scheduler();

    CPU_ZERO(&cpuset); // just clears the var for use below, no real effect yet

    // get affinity set for main thread
    mainthread = pthread_self();

    // Check the affinity mask assigned to the main thread and print - since unaltered should be all
    rc = pthread_getaffinity_np(mainthread, sizeof(cpu_set_t), &cpuset);
    if (rc != 0)
        perror("pthread_getaffinity_np");
    else
    {
        printf("main thread running on CPU=%d, CPUs =", sched_getcpu());

        for (j = 0; j < CPU_SETSIZE; j++)
            if (CPU_ISSET(j, &cpuset))
                printf(" %d", j);

        printf("\n");
    }

    //elevate starter one priority above main
    fifo_param.sched_priority = sched_get_priority_max(SCHED_POLICY) - 9; //increase priority by 1 (-9 instead of -10)
    pthread_attr_setschedparam(&fifo_sched_attr, &fifo_param);          

    // create the starter thread which will generate all other threads
    pthread_create(&starterthread,     // pointer to thread descriptor
                   &fifo_sched_attr, // use FIFO RT max priority attributes
                   starterThread,    // thread function entry point
                   (void *)0         // parameters to pass in
    );
    // allow starterThread to complete before main can continue - block main
    pthread_join(starterthread, NULL);

    printf("\nTEST COMPLETE\n");
}
