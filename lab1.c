// Sam Siewert, September 2016 (reference)

// Modified by: Nimisha and Austin for Part (e) Jetson run + Part (d) alignment
//


#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include <pthread.h>
#include <sched.h>
#include <time.h>
#include <semaphore.h>

#include <sys/sysinfo.h>
#include <syslog.h>
#include <errno.h>
#include <string.h>

#define USEC_PER_MSEC (1000)
#define NSEC_PER_MSEC (1000000L)
#define NSEC_PER_SEC  (1000000000L)

#define NUM_CPU_CORES   (1)     
#define NUM_THREADS     (3)     
#define FIB_TEST_CYCLES (100)   
#define CAL_SAMPLES     (20)    // averaging over multiple timing samples for better calibrration
#define PINNED_CPU_PREF (3)     // considering the use case for when 3 isnt available to fallabck

// LCM schedule parameters for Part (e)
#define T1_MSEC (20)    // f10 period
#define C1_MSEC (10)    // f10 compute time
#define T2_MSEC (50)    // f20 period
#define C2_MSEC (20)    // f20 compute time
#define LCM_MSEC (100)  // LCM of 20 , 50

sem_t semF10, semF20;

#define FIB_LIMIT (10)

static int abortTest = 0;
static double start_time_msec = 0.0;

static unsigned int seqIterations = FIB_LIMIT;
static unsigned int idx = 0, jdx = 1;

// making it volatile for avoiding compiler optimisations 
static volatile unsigned int fib = 0, fib0 = 0, fib1 = 1;

// helper code to get timestamp from monotonic clock
static double getTimeMsec(void)
{
    struct timespec event_ts;
    clock_gettime(CLOCK_MONOTONIC, &event_ts);
    return ((event_ts.tv_sec) * 1000.0) + ((event_ts.tv_nsec) / 1000000.0);
}

// Helpers for absolute scheduling
static void timespec_add_msec(struct timespec *t, long msec)
{
    // add msec in absolute timings arithmetic
    t->tv_nsec += (msec * NSEC_PER_MSEC);
    while(t->tv_nsec >= NSEC_PER_SEC)
    {
        t->tv_nsec -= NSEC_PER_SEC;
        t->tv_sec += 1;
    }
}

static void sleep_until_monotonic(const struct timespec *abs_ts)
{
    // using absolute sleep to avoid drifts
    int rc;
    do
    {
        rc = clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, abs_ts, NULL);
    }
    while(rc == EINTR);
}

// custom fibonacci workload to test
#define FIB_TEST(seqCnt, iterCnt)      \
   for(idx=0; idx < (iterCnt); idx++)  \
   {                                   \
      fib0=0; fib1=1; jdx=1;           \
      fib = fib0 + fib1;               \
      while(jdx < (seqCnt))            \
      {                                \
         fib0 = fib1;                  \
         fib1 = fib;                   \
         fib = fib0 + fib1;            \
         jdx++;                        \
      }                                \
   }                                   \


// event logger which preserves  the trace without io interference which can be dumped later in part f to syslog.


typedef enum
{
    EVT_CI = 0,
    EVT_F10_START,
    EVT_F10_DONE,
    EVT_F20_START,
    EVT_F20_DONE
} event_type_t;

typedef struct
{
    double t_msec;      // timestamp relative to start_time_msec
    event_type_t type;  // type of the event 
    int release;        // release count
    int cpu;            // CPU core used
    int loops;          // number of loops executed for events ehich are done 
} event_t;

#define MAX_EVENTS (5000)
static event_t event_log[MAX_EVENTS];
static int event_log_idx = 0;

static void log_event(event_type_t type, int release, int cpu, int loops)
{
    //if buffer fills well stop logging 
    if(event_log_idx < MAX_EVENTS)
    {
        event_log[event_log_idx].t_msec  = getTimeMsec() - start_time_msec;
        event_log[event_log_idx].type   = type;
        event_log[event_log_idx].release= release;
        event_log[event_log_idx].cpu    = cpu;
        event_log[event_log_idx].loops  = loops;
        event_log_idx++;
    }
}

//caliberation done using averages instead of one value 
static unsigned int calibrate_required_cycles(double target_ms)
{
    
    
    double sum_ms = 0.0;
    double t0, t1;

   
    FIB_TEST(seqIterations, FIB_TEST_CYCLES);

    for(int i = 0; i < CAL_SAMPLES; i++)
    {
        t0 = getTimeMsec();
        FIB_TEST(seqIterations, FIB_TEST_CYCLES);
        t1 = getTimeMsec();
        sum_ms += (t1 - t0);
    }

    double avg_ms = sum_ms / (double)CAL_SAMPLES;

    // just to prevent hard fault in case divide by zero happens
    if(avg_ms <= 0.0)
        avg_ms = 0.001;

    
    unsigned int required = (unsigned int)(target_ms / avg_ms);

    // to make sure service does nonzero work 
    if(required < 1) required = 1;

    // logging calibeartion result using syslog and one printf only because its outisde the real time zone
    syslog(LOG_INFO, "Calibration: target=%.3f ms, avg=%.6f ms per %d test cycles, required=%u",
           target_ms, avg_ms, FIB_TEST_CYCLES, required);

    printf("Calibration: target=%.3f ms, avg=%.6f ms per %d test cycles, required=%u\n",
           target_ms, avg_ms, FIB_TEST_CYCLES, required);

    return required;
}

typedef struct
{
    int threadIdx;
    int MajorPeriods;
} threadParams_t;

//service thread f10 (C1=10ms, T1=20ms)
static void *fib10(void *threadp)
{
    (void)threadp;

    int release = 0;
    int loops = 0;

    unsigned int required_cycles = calibrate_required_cycles((double)C1_MSEC);

    while(!abortTest)
    {
        sem_wait(&semF10);

        if(abortTest) break;

        release++;
        loops = 0;

        int cpu = sched_getcpu();
        log_event(EVT_F10_START, release, cpu, 0);
        syslog(LOG_INFO, "F10 start r=%d t=%.3f cpu=%d", release, getTimeMsec() - start_time_msec, cpu);

        // bruning cpu cycles to approximate C1
        while((unsigned int)loops < required_cycles)
        {
            FIB_TEST(seqIterations, FIB_TEST_CYCLES);
            loops++;
        }

        cpu = sched_getcpu();
        log_event(EVT_F10_DONE, release, cpu, loops);
        syslog(LOG_INFO, "F10 done  r=%d t=%.3f cpu=%d loops=%d", release, getTimeMsec() - start_time_msec, cpu, loops);
    }

    pthread_exit(NULL);
}

// service thread f20 (C2=20ms, T2=50ms)
static void *fib20(void *threadp)
{
    (void)threadp;

    int release = 0;
    int loops = 0;

    unsigned int required_cycles = calibrate_required_cycles((double)C2_MSEC);

    while(!abortTest)
    {
        sem_wait(&semF20);

        if(abortTest) break;

        release++;
        loops = 0;

        int cpu = sched_getcpu();
        log_event(EVT_F20_START, release, cpu, 0);
        syslog(LOG_INFO, "F20 start r=%d t=%.3f cpu=%d", release, getTimeMsec() - start_time_msec, cpu);

        // burning cpu cycles to approximate c2
        while((unsigned int)loops < required_cycles)
        {
            FIB_TEST(seqIterations, FIB_TEST_CYCLES);
            loops++;
        }

        cpu = sched_getcpu();
        log_event(EVT_F20_DONE, release, cpu, loops);
        syslog(LOG_INFO, "F20 done  r=%d t=%.3f cpu=%d loops=%d", release, getTimeMsec() - start_time_msec, cpu, loops);
    }

    pthread_exit(NULL);
}

// printing out the scheduling policy
static void print_scheduler(void)
{
    int schedType = sched_getscheduler(getpid());

    switch(schedType)
    {
        case SCHED_FIFO:
            printf("Pthread Policy is SCHED_FIFO\n");
            break;
        case SCHED_OTHER:
            printf("Pthread Policy is SCHED_OTHER\n");
            break;
        case SCHED_RR:
            printf("Pthread Policy is SCHED_RR\n");
            break;
        default:
            printf("Pthread Policy is UNKNOWN\n");
            break;
    }
}

// sequencer thread for lcm schedule 
static void *Sequencer(void *threadp)
{
    threadParams_t *threadParams = (threadParams_t *)threadp;

    printf("Starting Sequencer: [S1=f10, T1=%d, C1=%d], [S2=f20, T2=%d, C2=%d], U=0.9, LCM=%d\n",
           T1_MSEC, C1_MSEC, T2_MSEC, C2_MSEC, LCM_MSEC);

    // ref time for start of run
    start_time_msec = getTimeMsec();

    struct timespec next_release;
    clock_gettime(CLOCK_MONOTONIC, &next_release);

    int MajorPeriodCnt = 0;

    // lcm invarient schedule 
    while(MajorPeriodCnt < threadParams->MajorPeriods)
    {
        // CI marker at start of frame
        log_event(EVT_CI, MajorPeriodCnt + 1, sched_getcpu(), 0);
        syslog(LOG_INFO, "CI frame=%d t=%.3f", MajorPeriodCnt + 1, getTimeMsec() - start_time_msec);

        // CI release f10 and f20 at t=0
        sem_post(&semF10);
        sem_post(&semF20);

        // from this point we use absolute monotonic deadlines inside the frame.
        

        timespec_add_msec(&next_release, 20);
        sleep_until_monotonic(&next_release);
        sem_post(&semF10);

        timespec_add_msec(&next_release, 20);
        sleep_until_monotonic(&next_release);
        sem_post(&semF10);

        timespec_add_msec(&next_release, 10);
        sleep_until_monotonic(&next_release);
        sem_post(&semF20);

        timespec_add_msec(&next_release, 10);
        sleep_until_monotonic(&next_release);
        sem_post(&semF10);

        timespec_add_msec(&next_release, 20);
        sleep_until_monotonic(&next_release);
        sem_post(&semF10);

        timespec_add_msec(&next_release, 20);
        sleep_until_monotonic(&next_release);

        MajorPeriodCnt++;
    }

    // unblocking the service to exit 
    abortTest = 1;
    sem_post(&semF10);
    sem_post(&semF20);

    pthread_exit(NULL);
}

// printing out the log at the end 
static const char *evt_name(event_type_t t)
{
    switch(t)
    {
        case EVT_CI:        return "CI";
        case EVT_F10_START: return "F10_START";
        case EVT_F10_DONE:  return "F10_DONE";
        case EVT_F20_START: return "F20_START";
        case EVT_F20_DONE:  return "F20_DONE";
        default:            return "UNKNOWN";
    }
}

static void dump_event_log(void)
{
    printf("\n--- EVENT LOG DUMP (relative ms) ---\n");
    printf("idx, t_ms, event, release, cpu, loops\n");

    for(int i = 0; i < event_log_idx; i++)
    {
        printf("%d, %.3f, %s, %d, %d, %d\n",
               i,
               event_log[i].t_msec,
               evt_name(event_log[i].type),
               event_log[i].release,
               event_log[i].cpu,
               event_log[i].loops);
    }
}

int main(void)
{
    int rc, scope;
    pthread_t threads[NUM_THREADS];
    threadParams_t threadParams[NUM_THREADS];

    pthread_attr_t rt_sched_attr[NUM_THREADS];
    struct sched_param rt_param[NUM_THREADS];

    struct sched_param main_param;
    pthread_attr_t main_attr;

    cpu_set_t allcpuset;
    cpu_set_t threadcpu;

    abortTest = 0;

    // for syslog capturing 
    openlog("rtes_lab1", LOG_PID | LOG_CONS, LOG_USER);

    printf("System has %d processors configured and %d available.\n", get_nprocs_conf(), get_nprocs());

    CPU_ZERO(&allcpuset);
    for(int i = 0; i < NUM_CPU_CORES; i++)
        CPU_SET(i, &allcpuset);

    printf("Using CPUS=%d from total available.\n", CPU_COUNT(&allcpuset));

    // semaphores to be initailsed in empty state
    if(sem_init(&semF10, 0, 0)) { perror("sem_init semF10"); exit(-1); }
    if(sem_init(&semF20, 0, 0)) { perror("sem_init semF20"); exit(-1); }

    // make it shced fifo for the main process
    int rt_max_prio = sched_get_priority_max(SCHED_FIFO);
    int rt_min_prio = sched_get_priority_min(SCHED_FIFO);

    rc = sched_getparam(getpid(), &main_param);
    main_param.sched_priority = rt_max_prio;
    rc = sched_setscheduler(getpid(), SCHED_FIFO, &main_param);

    if(rc < 0)
    {
        // to proect against non sudo run 
        perror("sched_setscheduler (run with sudo)");
    }

    print_scheduler();

    // making sure main_attr is valid while finsing the current threads attributes
    pthread_getattr_np(pthread_self(), &main_attr);
    pthread_attr_getscope(&main_attr, &scope);

    if(scope == PTHREAD_SCOPE_SYSTEM)
        printf("PTHREAD SCOPE SYSTEM\n");
    else if(scope == PTHREAD_SCOPE_PROCESS)
        printf("PTHREAD SCOPE PROCESS\n");
    else
        printf("PTHREAD SCOPE UNKNOWN\n");

    printf("rt_max_prio=%d\n", rt_max_prio);
    printf("rt_min_prio=%d\n", rt_min_prio);

    // checking th eavailable cores and falling back to 0 instead of 3 if 3 isnt available 
    int available = get_nprocs();
    int pinned_cpu = (available > PINNED_CPU_PREF) ? PINNED_CPU_PREF : 0;

    // configuring the atttributes 
    for(int i = 0; i < NUM_THREADS; i++)
    {
        CPU_ZERO(&threadcpu);
        CPU_SET(pinned_cpu, &threadcpu);

        pthread_attr_init(&rt_sched_attr[i]);
        pthread_attr_setinheritsched(&rt_sched_attr[i], PTHREAD_EXPLICIT_SCHED);
        pthread_attr_setschedpolicy(&rt_sched_attr[i], SCHED_FIFO);
        pthread_attr_setaffinity_np(&rt_sched_attr[i], sizeof(cpu_set_t), &threadcpu);

      
        if(i == 0) rt_param[i].sched_priority = rt_max_prio;     // sequencer
        if(i == 1) rt_param[i].sched_priority = rt_max_prio - 1; // f10
        if(i == 2) rt_param[i].sched_priority = rt_max_prio - 2; // f20

        pthread_attr_setschedparam(&rt_sched_attr[i], &rt_param[i]);

        threadParams[i].threadIdx = i;
    }

    printf("Service threads will run pinned on CPU core %d (single-core RM emulation intent)\n", pinned_cpu);

    //creating service tghreads 
    rc = pthread_create(&threads[1], &rt_sched_attr[1], fib10, (void *)&threadParams[1]);
    if(rc != 0) { printf("pthread_create fib10 failed: %s\n", strerror(rc)); exit(-1); }

    rc = pthread_create(&threads[2], &rt_sched_attr[2], fib20, (void *)&threadParams[2]);
    if(rc != 0) { printf("pthread_create fib20 failed: %s\n", strerror(rc)); exit(-1); }

    // finsih caliberation of f10 and f20 before starting the sequencer thread 
    usleep(300000);

    printf("Start sequencer\n");
    threadParams[0].MajorPeriods = 3; 

    rc = pthread_create(&threads[0], &rt_sched_attr[0], Sequencer, (void *)&threadParams[0]);
    if(rc != 0) { printf("pthread_create sequencer failed: %s\n", strerror(rc)); exit(-1); }

   
    for(int i = 0; i < NUM_THREADS; i++)
        pthread_join(threads[i], NULL);

    printf("\nTEST COMPLETE\n");

    
    dump_event_log();

    // to clkean the syslog
    closelog();

    return 0;
}
