#define _POSIX_SOURCE 1
#define LEFT_FD 3
#define RIGHT_FD 4

#include <assert.h>
#include <signal.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

unsigned short num_philosophers;
unsigned short id;
pid_t next;
bool is_worker;

void sigusr1Handler(int signo) {}

volatile bool termination_requested = false;
void sigquitHandler(int signo) {
  termination_requested = true;
}

void waitForSignals(int count, ...) {
  sigset_t mask;
  sigfillset(&mask);

  va_list args;
  va_start(args, count);

  while (count--) {
    sigdelset(&mask, va_arg(args, int));
  };

  sigsuspend(&mask);
}

void quit(bool is_initiator) {
  close(LEFT_FD);
  close(RIGHT_FD);

  // Initiate the quit cycle.
  if (kill(next, SIGQUIT) == -1) {
    perror("kill() failed");
    exit(EXIT_FAILURE);
  }

  if (is_initiator) {
    // If I didn't wait, the pid could be reused and a new,
    // unrelated process could receive the signal.
    waitForSignals(1, SIGQUIT);
  }

  exit(EXIT_SUCCESS);
}

void getInput(int fd, void* buffer, size_t num_bytes) {
  while (num_bytes > 0) {
    int count = read(fd, buffer, num_bytes);
    if (count == -1) {
      perror("read() failed");
      exit(EXIT_FAILURE);
    }
    if (count == 0) {
      quit(true);
    }
    buffer += count;
    num_bytes -= count;
  }
}

void handlePacket() {
  unsigned short left_phase_id, right_phase_id;
  getInput(LEFT_FD, &left_phase_id, 2);
  getInput(RIGHT_FD, &right_phase_id, 2);

  if (left_phase_id < right_phase_id) {
    char discard[4];
    getInput(LEFT_FD, discard, 4);
    getInput(LEFT_FD, &left_phase_id, 2);
  }

  char message[10];
  memcpy(message, &id, 2);
  getInput(LEFT_FD, message + 2, 4);
  getInput(RIGHT_FD, message + 6, 4);

  // Write requests of <= PIPE_BUF bytes shall return nbyte on normal completion.
  if (write(1, message, 10) == -1) {
    perror("write() failed");
    exit(EXIT_FAILURE);
  }
}

void pokeNext() {
  if (kill(next, SIGUSR1) == -1) {
    perror("kill() failed");
    exit(EXIT_FAILURE);
  }
}

int main(int argc, char* argv[]) {
  assert(sizeof(unsigned short) == 2);
  assert(argc >= 3);

  {
    sigset_t block_mask;
    sigemptyset(&block_mask);
    sigaddset(&block_mask, SIGQUIT);
    sigprocmask(SIG_BLOCK, &block_mask, NULL);
  }

  {
    struct sigaction action;
    action.sa_flags = 0;
    sigemptyset(&action.sa_mask);
    action.sa_handler = sigusr1Handler;
    sigaction(SIGUSR1, &action, NULL);
    action.sa_handler = sigquitHandler;
    sigaction(SIGQUIT, &action, NULL);
  }

  num_philosophers = atoi(argv[1]);
  id = atoi(argv[2]);
  getInput(LEFT_FD, &next, sizeof(pid_t));
  is_worker = id % 2 == 0;

  if (id == num_philosophers - 1) {
    // Here I use the fact that the SIGUSR1 signal has been blocked in the parent process.
    // The philosopher will not receive the signal until it gets temporarily unblocked in waitForSignals().
    pokeNext();
  }

  // An empty run ensures that reading "next" precedes any attempt to read the packet contents.
  waitForSignals(1, SIGUSR1);
  pokeNext();

  while (true) {
    waitForSignals(2, SIGUSR1, SIGQUIT);
    if (termination_requested) {
      quit(false);
    }
    if (is_worker) {
      handlePacket();
    }
    is_worker = !is_worker;
    pokeNext();
  }
}
