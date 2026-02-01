/****************************************************************************/
/* Function: nanosleep and POSIX 1003.1b RT clock demonstration             */
/*                                                                          */
/* Sam Siewert - 02/05/2011                                                 */
/*                                                                          */
/****************************************************************************/

#include <pthread.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <errno.h>
//These are time conversion constants
#define NSEC_PER_SEC (1000000000)
#define NSEC_PER_MSEC (1000000)
#define NSEC_PER_USEC (1000)

#define ERROR (-1)
#define OK (0)

//This is the sleep intervals timings thats 10ms in ns
#define TEST_SECONDS (0)
#define TEST_NANOSECONDS (NSEC_PER_MSEC * 10)

void end_delay_test(void);

//specs used by nanosleep function
static struct timespec sleep_time = {0, 0}; // what we ask nanosleep for 
static struct timespec sleep_requested = {0, 0}; //original requested duration
static struct timespec remaining_time = {0, 0}; //if theres an interrupt during nanosleep, its the remaining kernel fallback

static unsigned int sleep_count = 0;

pthread_t main_thread;
pthread_attr_t main_sched_attr;
int rt_max_prio, rt_min_prio, min;
struct sched_param main_param;

//printing the current scheduling policy to coinfirm if its SCHED FIFIO
void print_scheduler(void)
{
   int schedType;

   schedType = sched_getscheduler(getpid());

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
   }
}

//helper function for printing purposes converting timestamp to floating 
double d_ftime(struct timespec *fstart, struct timespec *fstop)
{
  double dfstart = ((double)(fstart->tv_sec) + ((double)(fstart->tv_nsec) / 1000000000.0));
  double dfstop = ((double)(fstop->tv_sec) + ((double)(fstop->tv_nsec) / 1000000000.0));

  return(dfstop - dfstart); 
}

/*Error calculation function 
timespec is tv_sec and tv_nsec , so subtraction should handle the borrow form nanosec
returns OK if success
else error (if subtraction is negative)*/


int delta_t(struct timespec *stop, struct timespec *start, struct timespec *delta_t)
{
  int dt_sec=stop->tv_sec - start->tv_sec;
  int dt_nsec=stop->tv_nsec - start->tv_nsec;

  //printf("\ndt calcuation\n");

  // case 1 - less than a second of change
  if(dt_sec == 0)
  {
	  //printf("dt less than 1 second\n");

    // when nsec increased bw start and stop
	  if(dt_nsec >= 0 && dt_nsec < NSEC_PER_SEC)
	  {
	          //printf("nanosec greater at stop than start\n");
		  delta_t->tv_sec = 0;
		  delta_t->tv_nsec = dt_nsec;
	  }


    // for overflow type of case
	  else if(dt_nsec > NSEC_PER_SEC)
	  {
	          //printf("nanosec overflow\n");
		  delta_t->tv_sec = 1;
		  delta_t->tv_nsec = dt_nsec-NSEC_PER_SEC;
	  }

	  else // dt_nsec < 0 means stop is earlier than start
	  {
	         printf("stop is earlier than start\n");
		 return(ERROR);  
	  }
  }

  // case 2 - more than a second of change, check for roll-over
  else if(dt_sec > 0)
  {
	  //printf("dt more than 1 second\n");

    //if nsec didnt rollover
	  if(dt_nsec >= 0 && dt_nsec < NSEC_PER_SEC)
	  {
	          //printf("nanosec greater at stop than start\n");
		  delta_t->tv_sec = dt_sec;
		  delta_t->tv_nsec = dt_nsec;
	  }
    //if nsec overflowed more than 1 sec worth of time
	  else if(dt_nsec > NSEC_PER_SEC)
	  {
	          //printf("nanosec overflow\n");
		  delta_t->tv_sec = delta_t->tv_sec + 1;
		  delta_t->tv_nsec = dt_nsec-NSEC_PER_SEC;
	  }
    //in case of a borrow 
	  else // dt_nsec < 0 means roll over
	  {
	          //printf("nanosec roll over\n");
		  delta_t->tv_sec = dt_sec-1;
		  delta_t->tv_nsec = NSEC_PER_SEC + dt_nsec;
	  }
  }

  return(OK);
}

static struct timespec rtclk_dt = {0, 0}; //stop - start 
static struct timespec rtclk_start_time = {0, 0}; //timestamp before calling nanosleep
static struct timespec rtclk_stop_time = {0, 0};//timestamp after nanosleep
static struct timespec delay_error = {0, 0}; //error bw measured elapsed and requested interval

//#define MY_CLOCK CLOCK_REALTIME
#define MY_CLOCK CLOCK_MONOTONIC  //we will be using this clock instead of monotonic raw 
//#define MY_CLOCK CLOCK_MONOTONIC_RAW
//#define MY_CLOCK CLOCK_REALTIME_COARSE
//#define MY_CLOCK CLOCK_MONOTONIC_COARSE

#define TEST_ITERATIONS (100)

void *delay_test(void *threadID)
{
  int idx, rc;
  unsigned int max_sleep_calls=3; // if there are interrupts, max retries 
  int flags = 0;
  struct timespec rtclk_resolution;

  sleep_count = 0;

  //check for clocks granularity
  if(clock_getres(MY_CLOCK, &rtclk_resolution) == ERROR)
  {
      perror("clock_getres");
      exit(-1);
  }
  else
  {
      printf("\n\nPOSIX Clock demo using system RT clock with resolution:\n\t%ld secs, %ld microsecs, %ld nanosecs\n", rtclk_resolution.tv_sec, (rtclk_resolution.tv_nsec/1000), rtclk_resolution.tv_nsec);
  }

  for(idx=0; idx < TEST_ITERATIONS; idx++)
  {
      printf("test %d\n", idx);

      /* run test for defined seconds */
      //requested time interval
      sleep_time.tv_sec=TEST_SECONDS;
      sleep_time.tv_nsec=TEST_NANOSECONDS;

      //keeping a copy to later find differece
      sleep_requested.tv_sec=sleep_time.tv_sec;
      sleep_requested.tv_nsec=sleep_time.tv_nsec;

      /* start time stamp */ 
      clock_gettime(MY_CLOCK, &rtclk_start_time);

      /* request sleep time and repeat if time remains */
      do 
      {
          if(rc=nanosleep(&sleep_time, &remaining_time) == 0) break;
         
          //for interruption 
          sleep_time.tv_sec = remaining_time.tv_sec;
          sleep_time.tv_nsec = remaining_time.tv_nsec;
          sleep_count++;
      } 
      while (((remaining_time.tv_sec > 0) || (remaining_time.tv_nsec > 0))
		      && (sleep_count < max_sleep_calls));


      // timestamp after nanosleep ends
      clock_gettime(MY_CLOCK, &rtclk_stop_time);
      //finsiding measured differece 
      delta_t(&rtclk_stop_time, &rtclk_start_time, &rtclk_dt);
      //finding error 
      delta_t(&rtclk_dt, &sleep_requested, &delay_error);
      //helper to print results
      end_delay_test();
  }

}

void end_delay_test(void)
{
    double real_dt;
#if 0
  printf("MY_CLOCK start seconds = %ld, nanoseconds = %ld\n", 
         rtclk_start_time.tv_sec, rtclk_start_time.tv_nsec);
  
  printf("MY_CLOCK clock stop seconds = %ld, nanoseconds = %ld\n", 
         rtclk_stop_time.tv_sec, rtclk_stop_time.tv_nsec);
#endif
  //floating pt version for increased redability
  real_dt=d_ftime(&rtclk_start_time, &rtclk_stop_time);
  printf("MY_CLOCK clock DT seconds = %ld, msec=%ld, usec=%ld, nsec=%ld, sec=%6.9lf\n", 
         rtclk_dt.tv_sec, rtclk_dt.tv_nsec/1000000, rtclk_dt.tv_nsec/1000, rtclk_dt.tv_nsec, real_dt);

#if 0
  printf("Requested sleep seconds = %ld, nanoseconds = %ld\n", 
         sleep_requested.tv_sec, sleep_requested.tv_nsec);

  printf("\n");
  printf("Sleep loop count = %ld\n", sleep_count);
#endif
  printf("MY_CLOCK delay error = %ld, nanoseconds = %ld\n", 
         delay_error.tv_sec, delay_error.tv_nsec);
}

#define RUN_RT_THREAD

/* the main function prints initial sched policy
at max priority shifts to sched fifo
creates test thread for the mesurement loop
waits for that thread to finish*/

void main(void)
{
   int rc, scope;

   printf("Before adjustments to scheduling policy:\n");
   print_scheduler();

#ifdef RUN_RT_THREAD
  //setting threads properties
   pthread_attr_init(&main_sched_attr);
   pthread_attr_setinheritsched(&main_sched_attr, PTHREAD_EXPLICIT_SCHED);
   pthread_attr_setschedpolicy(&main_sched_attr, SCHED_FIFO);

   //linuxes prioirty range for fifo
   rt_max_prio = sched_get_priority_max(SCHED_FIFO);
   rt_min_prio = sched_get_priority_min(SCHED_FIFO);

  // tries to set to fifo max prority if not successfull, prints error message
   main_param.sched_priority = rt_max_prio;
   rc=sched_setscheduler(getpid(), SCHED_FIFO, &main_param);


   if (rc)
   {
       printf("ERROR; sched_setscheduler rc is %d\n", rc);
       perror("sched_setschduler"); exit(-1);
   }

   printf("After adjustments to scheduling policy:\n");
   print_scheduler();

   //applying the priority to use for pthread_create
   main_param.sched_priority = rt_max_prio;
   pthread_attr_setschedparam(&main_sched_attr, &main_param);
   
   //creating the test thread
   rc = pthread_create(&main_thread, &main_sched_attr, delay_test, (void *)0);

   if (rc)
   {
       printf("ERROR; pthread_create() rc is %d\n", rc);
       perror("pthread_create");
       exit(-1);
   }
 
   //waiting to finish the timing test
   pthread_join(main_thread, NULL);

   //clear the object
   if(pthread_attr_destroy(&main_sched_attr) != 0)
     perror("attr destroy");
#else //if not run test in the main thread
   delay_test((void *)0);
#endif

   printf("TEST COMPLETE\n");
}

