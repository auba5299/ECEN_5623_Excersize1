/*Author: Austin bathgate & Nimisha Rajashekhar
  Date: June 2024
  Description: This program creates a real-time scheduling environment using POSIX threads to compute Fibonacci numbers.
               It includes two service threads (fib10 and fib20) that perform Fibonacci calculations and a sequencer thread
               that coordinates their execution based on specified timing intervals. The program uses semaphores for synchronization
               and sets CPU affinity to ensure the threads run on a specific CPU core. It is a modified version of
               the lab1.c file provided in class.
*/

// This is necessary for CPU affinity macros in Linux
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

#define USEC_PER_MSEC (1000)
#define NSEC_PER_MSEC (1000000)
#define NUM_CPU_CORES (1)
#define FIB_TEST_CYCLES (100)
#define NUM_THREADS (3) // service threads + sequencer
sem_t semF10, semF20;

#define FIB_LIMIT_FOR_32_BIT (47)
#define FIB_LIMIT (10)

#define FIB10_TEST_CYCLES (106783) // found experimentally
#define FIB20_TEST_CYCLES (210545) // found experimentally


int abortTest = 0;
double start_time;

unsigned int seqIterations = FIB_LIMIT_FOR_32_BIT;
unsigned int idx = 0, jdx = 1;
unsigned int fib = 0, fib0 = 0, fib1 = 1;

double getTimeMsec(void);

#define FIB_TEST(seqCnt, iterCnt)     \
  for (idx = 0; idx < iterCnt; idx++) \
  {                                   \
    fib0 = 0;                         \
    fib1 = 1;                         \
    jdx = 1;                          \
    fib = fib0 + fib1;                \
    while (jdx < seqCnt)              \
    {                                 \
      fib0 = fib1;                    \
      fib1 = fib;                     \
      fib = fib0 + fib1;              \
      jdx++;                          \
    }                                 \
  }

typedef struct
{
  int threadIdx;
  int MajorPeriods;
} threadParams_t;

/* Iterations, 2nd arg must be tuned for any given target type
   using timestamps

   Be careful of WCET overloading CPU during first period of LCM.

 */
void *fib10(void *threadp)
{
  double event_time;
  int release = 0, cpucore, i;
  threadParams_t *threadParams = (threadParams_t *)threadp;

  while (!abortTest)
  {
    // wait for semaphore release from sequencer
    sem_wait(&semF10);

    // log start time of fib 10 process and core in use
    // cpucore = sched_getcpu();
    // event_time = getTimeMsec() - start_time;
    // syslog(LOG_INFO, "F10 start %d @ %lf on core %d",
    //        release, event_time, cpucore);

    // run the fib10 simulated load
    FIB_TEST(seqIterations, FIB10_TEST_CYCLES);

    // log completion time of fib 10 process
    event_time = getTimeMsec() - start_time;
    syslog(LOG_INFO, "F10 complete %d @ %lf",
           release, event_time);
    release++;
  }

  pthread_exit((void *)0);
}

void *fib20(void *threadp)
{
  double event_time;
  int release = 0, cpucore, i;
  threadParams_t *threadParams = (threadParams_t *)threadp;

  while (!abortTest)
  {
    // wait for semaphore release from sequencer
    sem_wait(&semF20);

    // log start time of fib 20 process and core in use
    // cpucore = sched_getcpu();
    // event_time = getTimeMsec() - start_time;
    // syslog(LOG_INFO, "F20 start %d @ %lf on core %d",
    //        release, event_time, cpucore);

    // run the fib20 simulated load
    FIB_TEST(seqIterations, FIB20_TEST_CYCLES);

    // log completion time of fib 20 process
    event_time = getTimeMsec() - start_time;
    syslog(LOG_INFO, "F20 complete %d @ %lf",
           release, event_time);
    release++;
  }

  pthread_exit((void *)0);
}

double getTimeMsec(void)
{
  struct timespec event_ts = {0, 0};

  clock_gettime(CLOCK_MONOTONIC_RAW, &event_ts);
  return ((event_ts.tv_sec) * 1000.0) + ((event_ts.tv_nsec) / 1000000.0);
}

void print_scheduler(void)
{
  int schedType;

  schedType = sched_getscheduler(getpid());

  switch (schedType)
  {
  case SCHED_FIFO:
    printf("Pthread Policy is SCHED_FIFO\n");
    break;
  case SCHED_OTHER:
    printf("Pthread Policy is SCHED_OTHER\n");
    exit(-1);
    break;
  case SCHED_RR:
    printf("Pthread Policy is SCHED_RR\n");
    exit(-1);
    break;
  default:
    printf("Pthread Policy is UNKNOWN\n");
    exit(-1);
  }
}

void *Sequencer(void *threadp)
{
  int i;
  int MajorPeriodCnt = 0;
  double event_time;
  threadParams_t *threadParams = (threadParams_t *)threadp;

  struct timespec release_20ms = {0, 20 * NSEC_PER_MSEC};
  struct timespec release_10ms = {0, 10 * NSEC_PER_MSEC};

  printf("Starting Sequencer: [S1, T1=20, C1=10], [S2, T2=50, C2=20], U=0.9, LCM=100\n");
  printf("See the syslog for timestamps from inside the sequencer\n");
  start_time = getTimeMsec();

  // Sequencing loop for LCM phasing of S1, S2
  do
  {
    // Basic sequence of releases after CI for 90% load
    //
    // S1: T1= 20, C1=10 msec
    // S2: T2= 50, C2=20 msec
    //
    // usleep was swapped for clock_nanosleep for better accuracy and use of CLOCK_MONOTONIC
    //  
    // Simulate the C.I. for S1 and S2 and timestamp in log
    
    sem_post(&semF10);
    sem_post(&semF20);
    syslog(LOG_INFO, "**** BEGIN - RELEASE S1 AND S2 t=%lf\n", event_time = getTimeMsec() - start_time);

    clock_nanosleep(CLOCK_MONOTONIC, 0, &release_20ms, NULL);
    sem_post(&semF10);
    syslog(LOG_INFO, "Fib10 release at t=%lf", event_time = getTimeMsec() - start_time);

    clock_nanosleep(CLOCK_MONOTONIC, 0, &release_20ms, NULL);
    sem_post(&semF10);
    syslog(LOG_INFO, "Fib10 release at t=%lf", event_time = getTimeMsec() - start_time);
    
    clock_nanosleep(CLOCK_MONOTONIC, 0, &release_10ms, NULL);
    sem_post(&semF20);
    syslog(LOG_INFO, "Fib20 release at t=%lf", event_time = getTimeMsec() - start_time);

    clock_nanosleep(CLOCK_MONOTONIC, 0, &release_10ms, NULL);
    sem_post(&semF10);
    syslog(LOG_INFO, "Fib10 release at t=%lf", event_time = getTimeMsec() - start_time);
    
    clock_nanosleep(CLOCK_MONOTONIC, 0, &release_20ms, NULL);
    sem_post(&semF10);
    syslog(LOG_INFO, "Fib10 release at t=%lf", event_time = getTimeMsec() - start_time);
    
    clock_nanosleep(CLOCK_MONOTONIC, 0, &release_20ms, NULL);
    MajorPeriodCnt++;
  } while (MajorPeriodCnt < threadParams->MajorPeriods);

  abortTest = 1;
  syslog(LOG_INFO, "!! abortTest = 1, ignore final sequence which follows!!");

  sem_post(&semF10);
  sem_post(&semF20);
}

void main(void)
{
  int i, rc, scope;
  cpu_set_t threadcpu;
  pthread_t threads[NUM_THREADS];
  threadParams_t threadParams[NUM_THREADS];
  pthread_attr_t rt_sched_attr[NUM_THREADS];
  int rt_max_prio, rt_min_prio;
  struct sched_param rt_param[NUM_THREADS];
  struct sched_param main_param;
  pthread_attr_t main_attr;
  pid_t mainpid;
  cpu_set_t allcpuset;
  openlog("RT-FIB-FIFO", LOG_PID, LOG_USER);
  abortTest = 0;

  printf("System has %d processors configured and %d available.\n", get_nprocs_conf(), get_nprocs());

  CPU_ZERO(&allcpuset);

  for (i = 0; i < NUM_CPU_CORES; i++)
    CPU_SET(i, &allcpuset);

  printf("Using CPUS=%d from total available.\n", CPU_COUNT(&allcpuset));

  // initialize the sequencer semaphores
  //
  if (sem_init(&semF10, 0, 0))
  {
    printf("Failed to initialize semF10 semaphore\n");
    exit(-1);
  }
  if (sem_init(&semF20, 0, 0))
  {
    printf("Failed to initialize semF20 semaphore\n");
    exit(-1);
  }

  mainpid = getpid();

  rt_max_prio = sched_get_priority_max(SCHED_FIFO);
  rt_min_prio = sched_get_priority_min(SCHED_FIFO);

  rc = sched_getparam(mainpid, &main_param);
  main_param.sched_priority = rt_max_prio;
  rc = sched_setscheduler(getpid(), SCHED_FIFO, &main_param);
  if (rc < 0)
    perror("main_param");
  print_scheduler();

  pthread_attr_getscope(&main_attr, &scope);

  if (scope == PTHREAD_SCOPE_SYSTEM)
    printf("PTHREAD SCOPE SYSTEM\n");
  else if (scope == PTHREAD_SCOPE_PROCESS)
    printf("PTHREAD SCOPE PROCESS\n");
  else
    printf("PTHREAD SCOPE UNKNOWN\n");

  printf("rt_max_prio=%d\n", rt_max_prio);
  printf("rt_min_prio=%d\n", rt_min_prio);

  for (i = 0; i < NUM_THREADS; i++)
  {

    CPU_ZERO(&threadcpu);
    CPU_SET(3, &threadcpu);

    rc = pthread_attr_init(&rt_sched_attr[i]);
    rc = pthread_attr_setinheritsched(&rt_sched_attr[i], PTHREAD_EXPLICIT_SCHED);
    rc = pthread_attr_setschedpolicy(&rt_sched_attr[i], SCHED_FIFO);
    rc = pthread_attr_setaffinity_np(&rt_sched_attr[i], sizeof(cpu_set_t), &threadcpu);

    // scheduler is [0] with highest, fib10 is next with index [1], and fib20 laowest [2]
    rt_param[i].sched_priority = rt_max_prio - i;
    pthread_attr_setschedparam(&rt_sched_attr[i], &rt_param[i]);

    threadParams[i].threadIdx = i;
  }

  printf("Service threads will run on %d CPU cores\n", CPU_COUNT(&threadcpu));

  // Create Service threads which will block awaiting release for:
  //
  // serviceF10
  rc = pthread_create(&threads[1],       // pointer to thread descriptor
                      &rt_sched_attr[1], // use specific attributes
                      //(void *)0,                 // default attributes
                      fib10,                     // thread function entry point
                      (void *)&(threadParams[1]) // parameters to pass in
  );
  // serviceF20
  rc = pthread_create(&threads[2],       // pointer to thread descriptor
                      &rt_sched_attr[2], // use specific attributes
                      //(void *)0,                 // default attributes
                      fib20,                     // thread function entry point
                      (void *)&(threadParams[2]) // parameters to pass in
  );

  // Wait for service threads to calibrate and await relese by sequencer
  usleep(300000); // leaving as is since just a delay before sequencer starts and not RT

  // Create Sequencer thread, which like a cyclic executive, is highest prio
  printf("Start sequencer\n");
  threadParams[0].MajorPeriods = 3;

  rc = pthread_create(&threads[0],       // pointer to thread descriptor
                      &rt_sched_attr[0], // use specific attributes
                      //(void *)0,                 // default attributes
                      Sequencer,                 // thread function entry point
                      (void *)&(threadParams[0]) // parameters to pass in
  );

  for (i = 0; i < NUM_THREADS; i++)
    pthread_join(threads[i], NULL);

  printf("\nTEST COMPLETE\n");
  closelog();
}
