#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <unistd.h>

#define SOCKET_PATH "/tmp/bench.sock"
#define FIFO_PATH "/tmp/bench.fifo"
#define BUFFER_SIZE 1024
#define DATA_SIZE (100 * 1024)  // 100KB
#define NUM_ITERATIONS 1000

// Utility function to get current time in microseconds
long long get_usec(void) {
  struct timeval tv;
  gettimeofday(&tv, NULL);
  return tv.tv_sec * 1000000LL + tv.tv_usec;
}

// FIFO benchmark
void fifo_sender(void) {
  char buffer[BUFFER_SIZE];
  memset(buffer, 'A', BUFFER_SIZE);  // Fill with test data

  int fd = open(FIFO_PATH, O_WRONLY);
  if (fd < 0) {
    perror("FIFO open failed");
    exit(1);
  }

  long long start_time = get_usec();

  for (int i = 0; i < NUM_ITERATIONS; i++) {
    size_t remaining = DATA_SIZE;
    while (remaining > 0) {
      size_t to_write = (remaining < BUFFER_SIZE) ? remaining : BUFFER_SIZE;
      ssize_t written = write(fd, buffer, to_write);
      if (written < 0) {
        perror("FIFO write failed");
        exit(1);
      }
      remaining -= written;
    }
  }

  long long end_time = get_usec();
  printf("FIFO sender finished: %lld usec\n", end_time - start_time);
  close(fd);
}

void fifo_receiver(void) {
  char buffer[BUFFER_SIZE];

  int fd = open(FIFO_PATH, O_RDONLY);
  if (fd < 0) {
    perror("FIFO open failed");
    exit(1);
  }

  long long start_time = get_usec();

  for (int i = 0; i < NUM_ITERATIONS; i++) {
    size_t remaining = DATA_SIZE;
    while (remaining > 0) {
      size_t to_read = (remaining < BUFFER_SIZE) ? remaining : BUFFER_SIZE;
      ssize_t bytes_read = read(fd, buffer, to_read);
      if (bytes_read < 0) {
        perror("FIFO read failed");
        exit(1);
      }
      remaining -= bytes_read;
    }
  }

  long long end_time = get_usec();
  printf("FIFO receiver finished: %lld usec\n", end_time - start_time);
  close(fd);
}

// Datagram socket benchmark
void socket_sender(void) {
  char buffer[BUFFER_SIZE];
  memset(buffer, 'A', BUFFER_SIZE);

  int sock_fd = socket(AF_UNIX, SOCK_DGRAM, 0);
  if (sock_fd < 0) {
    perror("Socket creation failed");
    exit(1);
  }

  struct sockaddr_un addr;
  memset(&addr, 0, sizeof(addr));
  addr.sun_family = AF_UNIX;
  strncpy(addr.sun_path, SOCKET_PATH, sizeof(addr.sun_path) - 1);

  long long start_time = get_usec();

  for (int i = 0; i < NUM_ITERATIONS; i++) {
    size_t remaining = DATA_SIZE;
    while (remaining > 0) {
      size_t to_send = (remaining < BUFFER_SIZE) ? remaining : BUFFER_SIZE;
      ssize_t sent = sendto(sock_fd, buffer, to_send, 0, (struct sockaddr*)&addr, sizeof(addr));
      if (sent < 0) {
        perror("Socket send failed");
        exit(1);
      }
      remaining -= sent;
    }
  }

  long long end_time = get_usec();
  printf("Socket sender finished: %lld usec\n", end_time - start_time);
  close(sock_fd);
}

void socket_receiver(void) {
  char buffer[BUFFER_SIZE];

  int sock_fd = socket(AF_UNIX, SOCK_DGRAM, 0);
  if (sock_fd < 0) {
    perror("Socket creation failed");
    exit(1);
  }

  struct sockaddr_un addr;
  memset(&addr, 0, sizeof(addr));
  addr.sun_family = AF_UNIX;
  strncpy(addr.sun_path, SOCKET_PATH, sizeof(addr.sun_path) - 1);

  unlink(SOCKET_PATH);
  if (bind(sock_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
    perror("Socket bind failed");
    exit(1);
  }

  long long start_time = get_usec();

  for (int i = 0; i < NUM_ITERATIONS; i++) {
    size_t remaining = DATA_SIZE;
    while (remaining > 0) {
      size_t to_recv = (remaining < BUFFER_SIZE) ? remaining : BUFFER_SIZE;
      ssize_t received = recv(sock_fd, buffer, to_recv, 0);
      if (received < 0) {
        perror("Socket receive failed");
        exit(1);
      }
      remaining -= received;
    }
  }

  long long end_time = get_usec();
  printf("Socket receiver finished: %lld usec\n", end_time - start_time);
  close(sock_fd);
  unlink(SOCKET_PATH);
}

int main(void) {
  printf("=== FIFO Benchmark ===\n");
  mkfifo(FIFO_PATH, 0666);

  pid_t fifo_pid = fork();
  if (fifo_pid == 0) {
    fifo_receiver();
    exit(0);
  } else {
    fifo_sender();
    wait(NULL);
  }
  unlink(FIFO_PATH);

  printf("\n=== Socket Benchmark ===\n");
  pid_t socket_pid = fork();
  if (socket_pid == 0) {
    socket_receiver();
    exit(0);
  } else {
    sleep(1);  // Give receiver time to bind
    socket_sender();
    wait(NULL);
  }

  return 0;
}
