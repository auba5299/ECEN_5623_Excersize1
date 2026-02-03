#define _GNU_SOURCE    //enables aditional extensions for setting and checking affinity / CPU
#include <pthread.h>   //for pthread_xxx calls
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>     //for use of system error calls  
#include <sys/time.h>  //legacy - use time.h and clock_gettime in excersizes
#include <sys/types.h> //OS types 
#include <sched.h>     //sched policies
#include <unistd.h>    //POSIX system calls such as sleep and usleep but not n-sleep

#define NUM_THREADS 64
#define NUM_CPUS 8     //unused, Linux checks system for num CPUs

//structure to align with POSIX but only contains thread idx
typedef struct
{
    int threadIdx;
} threadParams_t;


// POSIX thread declarations and scheduling attributes
//
pthread_t threads[NUM_THREADS];
pthread_t mainthread;       //scheduling thread
pthread_t startthread;      //thread to start workers
threadParams_t threadParams[NUM_THREADS];

pthread_attr_t fifo_sched_attr; //attribute to be configured for fifo, affinity, priority
pthread_attr_t orig_sched_attr; //unused artifact
struct sched_param fifo_param;  //sched_param struct defined in <sched.h> that simply contains pri lvl as int

#define SCHED_POLICY SCHED_FIFO
#define MAX_ITERATIONS (1000000)

/*all treads in a process share sched policy, by using 3 lines below,
we pull policy for one which is applicable to all*/
void print_scheduler(void) 
{
    int schedType = sched_getscheduler(getpid());
    //switch int values linked to macros from <sched.h>
    switch(schedType)
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

    printf("INITIAL "); print_scheduler();

    pthread_attr_init(&fifo_sched_attr); //init attribute object of type pthread_attr_t for our fifo sched params
    pthread_attr_setinheritsched(&fifo_sched_attr, PTHREAD_EXPLICIT_SCHED); //explicitly use selected sched and params below
    pthread_attr_setschedpolicy(&fifo_sched_attr, SCHED_POLICY);    //selected sched = SCHED_FIFO (line 32 define)
    //cpu pin to cpu 3
    CPU_ZERO(&cpuset);  //zero out CPU SET to remove default artifacts
    cpuidx=(3);         //CPU3
    CPU_SET(cpuidx, &cpuset);   //add CPU3 to usable CPUs (CPU set)
    pthread_attr_setaffinity_np(&fifo_sched_attr, sizeof(cpu_set_t), &cpuset); //ties cpu3 pin to our fifo_sched_attr

    //assign max priority to fifo_param structure
    max_prio=sched_get_priority_max(SCHED_POLICY);  //max prio of SCHED_FIFO is 99
    fifo_param.sched_priority=max_prio;    
    
    //process level - sets process scheduling polifcy to 
    if((rc=sched_setscheduler(getpid(), SCHED_POLICY, &fifo_param)) < 0) 
        perror("sched_setscheduler");
    //attribute level - when fifo_sched_attr is used in p-thread create, it will have this cpu3 pin, fifo, max prio attributes
    pthread_attr_setschedparam(&fifo_sched_attr, &fifo_param);

    printf("ADJUSTED "); print_scheduler();
}




void *counterThread(void *threadp)
{
    int sum=0, i, rc, iterations;
    threadParams_t *threadParams = (threadParams_t *)threadp;
    pthread_t mythread;
    double start=0.0, stop=0.0;
    struct timeval startTime, stopTime;

    gettimeofday(&startTime, 0);    //this is time since Epoch
    start = ((startTime.tv_sec * 1000000.0) + startTime.tv_usec)/1000000.0;


    for(iterations=0; iterations < MAX_ITERATIONS; iterations++)
    {
        sum=0;
        for(i=1; i < (threadParams->threadIdx)+1; i++)
            sum=sum+i;
    }


    gettimeofday(&stopTime, 0);
    stop = ((stopTime.tv_sec * 1000000.0) + stopTime.tv_usec)/1000000.0;

    printf("\nThread idx=%d, sum[0...%d]=%d, running on CPU=%d, start=%lf, stop=%lf", 
           threadParams->threadIdx,
           threadParams->threadIdx, sum, sched_getcpu(),
           start, stop);
}


void *starterThread(void *threadp)
{
   int i, rc;

   printf("starter thread running on CPU=%d\n", sched_getcpu());

   for(i=0; i < NUM_THREADS; i++)
   {
       threadParams[i].threadIdx=i;

       pthread_create(&threads[i],   // pointer to thread descriptor
                      &fifo_sched_attr,     // use FIFO RT max priority attributes configured in set_scheduler
                      counterThread, // thread function entry point - worker thread for count, exec time, and 
                      (void *)&(threadParams[i]) // parameters to pass in
                     );

   }
   //let all worker threads finish before continuing starterThread - block starterThread
   for(i=0;i<NUM_THREADS;i++)
       pthread_join(threads[i], NULL);

}


int main (int argc, char *argv[])
{
   int rc;
   int i, j;
   cpu_set_t cpuset;

   /*call set_scheduler to make process FIFO highest pri, configure attributed
   for FIFO max pri to use in thread creation*/
   set_scheduler();

   CPU_ZERO(&cpuset); //just clears the var for use below, no real effect yet

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
   //create the starter thread which will generate all other threads
   pthread_create(&startthread,   // pointer to thread descriptor
                  &fifo_sched_attr,     // use FIFO RT max priority attributes
                  starterThread, // thread function entry point
                  (void *)0 // parameters to pass in
                 );
   //allow starterThread to complete before main can continue - block main
   pthread_join(startthread, NULL);

   printf("\nTEST COMPLETE\n");
}
