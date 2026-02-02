// https://www.i-programmer.info/programming/cc/13002-applying-c-deadline-scheduling.html?start=1


#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <sys/syscall.h>
#include <linux/sched.h>
#include <pthread.h>
#include <stdint.h>
#include <errno.h>


#include <unistd.h>
#include <sys/syscall.h>


#include <sched.h>
#include <time.h>
#include <string.h>

struct sched_attr
{
    uint32_t size;
    uint32_t sched_policy;
    uint64_t sched_flags;
    int32_t sched_nice;
    uint32_t sched_priority;
    uint64_t sched_runtime;
    uint64_t sched_deadline;
    uint64_t sched_period;
};

int sched_setattr(pid_t pid, const struct sched_attr *attr, unsigned int flags)
{
    return syscall(__NR_sched_setattr, pid, attr, flags);
}

//to choose the policy and pass it to the thread
typedef enum
{
    POLICY_DEADLINE = 0,
    POLICY_FIFO = 1
} policy_t;

//to print the posix time
static void print_posix_time(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);

    printf("Time (monotonic): %ld.%09ld\n",(long)ts.tv_sec, ts.tv_nsec);
}

void * threadA(void *p)
{
    // read which option we want
    policy_t policy = POLICY_DEADLINE;
    if (p != NULL) policy = *((policy_t *)p);

    if (policy == POLICY_DEADLINE)
    {
        //keeping this same as old code 
        struct sched_attr attr =
        {
            .size = sizeof (attr),
            .sched_policy = SCHED_DEADLINE,
            .sched_runtime = 10 * 1000 * 1000,
            .sched_period = 2 * 1000 * 1000 * 1000,
            .sched_deadline = 11 * 1000 * 1000
        };

        // to see the differences 
        if (sched_setattr(0, &attr, 0) != 0)
        {
            perror("sched_setattr(SCHED_DEADLINE)");
        }
    }
    else
    {
        // for sched_fifo
        //randomly setting priority as 88, can chnage to anyhtign 
        struct sched_param sp;
        sp.sched_priority = 88;

        if (pthread_setschedparam(pthread_self(), SCHED_FIFO, &sp) != 0)
        {
            perror("pthread_setschedparam(SCHED_FIFO)");
        }
    }

    for (;;)
    {
        //POSIX clock print
        print_posix_time();
        fflush(0);
        sched_yield();
    };

    
    return NULL;
}


int main(int argc, char** argv)
{
    pthread_t pthreadA;

    
    //this is by default
    policy_t policy = POLICY_DEADLINE;

    if (argc >= 2)
    {
        if (strcmp(argv[1], "fifo") == 0 || strcmp(argv[1], "SCHED_FIFO") == 0)
        {
            policy = POLICY_FIFO;  // doing a string compare, you can only pass fifo or SCHED_FIFO to argv
        }
        else if (strcmp(argv[1], "deadline") == 0 ||strcmp(argv[1], "SCHED_DEADLINE") == 0)
        {
            policy = POLICY_DEADLINE;
        }
        else
        {
            fprintf(stderr, "Usage: %s [deadline|fifo]\n", argv[0]);
            fprintf(stderr, "Default is 'deadline'.\n");
        }
    }


    pthread_create(&pthreadA, NULL, threadA, (void *)&policy);

    pthread_exit(0);
    return (EXIT_SUCCESS);
}
