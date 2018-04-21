#ifndef _SAMPLING_H_
#define _SAMPLING_H_

#if !(_POSIX_C_SOURCE >= 199309L)
#error "POSIX_C_SOURCE is not compatible"
#endif

#define CLOCKID CLOCK_MONOTONIC

#include <signal.h>
#include <time.h>

#define EXIT_ERR(msg)    do { perror(msg); exit(EXIT_FAILURE); \
                        } while (0)

typedef struct timer_data_t {
  timer_t timerid;
  struct sigevent sev;
  struct itimerspec its;
  long long freq_nanosecs;
} timer_data_t;

static inline void start_sampling(timer_data_t* timer_, long long sampl_interv, void (*cb_func)(int, siginfo_t*, void*)){

  /* Setup a handler for the timer signal */
  struct sigaction sa;
  sa.sa_flags = SA_SIGINFO;
  sa.sa_sigaction = cb_func;
  sigemptyset(&sa.sa_mask);
  if (sigaction(SIGRTMIN, &sa, NULL) == -1)
      EXIT_ERR("sigaction");

  /* Create the timer */

  timer_->sev.sigev_notify = SIGEV_SIGNAL;
  timer_->sev.sigev_signo = SIGRTMIN;
  timer_->sev.sigev_value.sival_ptr = &timer_->timerid;
  if (timer_create(CLOCKID, &timer_->sev, &timer_->timerid) == -1)
      EXIT_ERR("timer_create");

  timer_->its.it_value.tv_sec = sampl_interv / 1000000000;
  timer_->its.it_value.tv_nsec = sampl_interv % 1000000000;
  timer_->its.it_interval.tv_sec = timer_->its.it_value.tv_sec;
  timer_->its.it_interval.tv_nsec = timer_->its.it_value.tv_nsec;

  /* Start the timer */

  if (timer_settime(timer_->timerid, 0, &timer_->its, NULL) == -1)
       EXIT_ERR("timer_settime");
}

static inline void stop_sampling(timer_data_t* timer_, void (*cb_func)(void)) {
    /* Desarm the timer */
    timer_->its.it_value.tv_sec = 0;
    timer_->its.it_value.tv_nsec = 0;
    if (timer_settime(timer_->timerid, 0, &timer_->its, NULL) == -1)
       EXIT_ERR("timer_settime");

    cb_func();
}
#endif /*_SAMPLING_H_*/
