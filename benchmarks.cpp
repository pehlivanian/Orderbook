#include <errno.h>
#include <fcntl.h>
#include <mqueue.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/eventfd.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/uio.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <unistd.h>
#include <cstdint>

// Common definitions for all tests
#define SOCKET_PATH "/tmp/bench.sock"
#define FIFO_PATH "/tmp/bench.fifo"
#define QUEUE_NAME "/bench_queue"
#define SHM_NAME "/my_shared_mem"
#define BUFFER_SIZE (32 * 1024)  // 32KB buffer for all tests
#define NUM_ITERATIONS 1000

// Shared structures
struct shared_data {
  size_t size;
  char buffer[BUFFER_SIZE];
};

struct shared_info {
  void* buffer_addr;
  size_t buffer_size;
};

// Utility functions
void random_bits(char* buffer, size_t num_bytes) {
  int fd = open("/dev/urandom", O_RDONLY);
  if (fd < 0) {
    perror("open urandom");
    exit(1);
  }
  ssize_t bytes = read(fd, buffer, num_bytes);
  if (bytes != num_bytes) {
    perror("read urandom");
    exit(1);
  }
  close(fd);
}

long long get_usec(void) {
  struct timeval tv;
  gettimeofday(&tv, NULL);
  return tv.tv_sec * 1000000LL + tv.tv_usec;
}

// FIFO Implementation
void fifo_source(void) {
  char buffer[BUFFER_SIZE];
  random_bits(buffer, BUFFER_SIZE);

  int fd = open(FIFO_PATH, O_WRONLY);
  if (fd < 0) {
    perror("FIFO open failed");
    exit(1);
  }

  long long start_time = get_usec();

  for (int i = 0; i < NUM_ITERATIONS; i++) {
    if (write(fd, buffer, BUFFER_SIZE) != BUFFER_SIZE) {
      perror("FIFO write failed");
      exit(1);
    }
  }

  long long end_time = get_usec();
  printf("FIFO sender finished: %lld usec (%.2f MB/s)\n", end_time - start_time,
         ((double)BUFFER_SIZE * NUM_ITERATIONS) / (end_time - start_time));
  close(fd);
}

void fifo_target(void) {
  char buffer[BUFFER_SIZE];

  int fd = open(FIFO_PATH, O_RDONLY);
  if (fd < 0) {
    perror("FIFO open failed");
    exit(1);
  }

  long long start_time = get_usec();

  for (int i = 0; i < NUM_ITERATIONS; i++) {
    if (read(fd, buffer, BUFFER_SIZE) != BUFFER_SIZE) {
      perror("FIFO read failed");
      exit(1);
    }
  }

  long long end_time = get_usec();
  printf("FIFO receiver finished: %lld usec (%.2f MB/s)\n", end_time - start_time,
         ((double)BUFFER_SIZE * NUM_ITERATIONS) / (end_time - start_time));
  close(fd);
}

// Unix Domain Socket Implementation
void socket_source(void) {
  char buffer[BUFFER_SIZE];
  random_bits(buffer, BUFFER_SIZE);

  int sock_fd = socket(AF_UNIX, SOCK_STREAM, 0);
  if (sock_fd < 0) {
    perror("Socket creation failed");
    exit(1);
  }

  struct sockaddr_un addr;
  memset(&addr, 0, sizeof(addr));
  addr.sun_family = AF_UNIX;
  strncpy(addr.sun_path, SOCKET_PATH, sizeof(addr.sun_path) - 1);

  sleep(1);  // Give receiver time to start

  if (connect(sock_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
    perror("Socket connect failed");
    exit(1);
  }

  long long start_time = get_usec();

  for (int i = 0; i < NUM_ITERATIONS; i++) {
    if (send(sock_fd, buffer, BUFFER_SIZE, 0) != BUFFER_SIZE) {
      perror("Socket send failed");
      exit(1);
    }
  }

  long long end_time = get_usec();
  printf("Socket sender finished: %lld usec (%.2f MB/s)\n", end_time - start_time,
         ((double)BUFFER_SIZE * NUM_ITERATIONS) / (end_time - start_time));
  close(sock_fd);
}

void socket_target(void) {
  char buffer[BUFFER_SIZE];

  int sock_fd = socket(AF_UNIX, SOCK_STREAM, 0);
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

  if (listen(sock_fd, 1) < 0) {
    perror("Socket listen failed");
    exit(1);
  }

  int client_fd = accept(sock_fd, NULL, NULL);
  if (client_fd < 0) {
    perror("Socket accept failed");
    exit(1);
  }

  long long start_time = get_usec();

  for (int i = 0; i < NUM_ITERATIONS; i++) {
    if (recv(client_fd, buffer, BUFFER_SIZE, 0) != BUFFER_SIZE) {
      perror("Socket recv failed");
      exit(1);
    }
  }

  long long end_time = get_usec();
  printf("Socket receiver finished: %lld usec (%.2f MB/s)\n", end_time - start_time,
         ((double)BUFFER_SIZE * NUM_ITERATIONS) / (end_time - start_time));
  close(client_fd);
  close(sock_fd);
  unlink(SOCKET_PATH);
}

// Message Queue benchmark
void mq_source(void) {
  char buffer[BUFFER_SIZE];
  random_bits(buffer, BUFFER_SIZE);

  struct mq_attr attr = {
      .mq_flags = 0, .mq_maxmsg = 10, .mq_msgsize = BUFFER_SIZE, .mq_curmsgs = 0};

  mqd_t mq = mq_open(QUEUE_NAME, O_WRONLY | O_CREAT, 0644, &attr);
  if (mq == (mqd_t)-1) {
    perror("Message queue open failed");
    exit(1);
  }

  int num_iterations = NUM_ITERATIONS;

  long long start_time = get_usec();

  while (num_iterations) {
    mq_send(mq, buffer, BUFFER_SIZE, 0);
    num_iterations--;
  }

  long long end_time = get_usec();
  printf("Message queue sender finished: %lld usec\n", end_time - start_time);
  mq_close(mq);
}

void mq_target(void) {
  char buffer[BUFFER_SIZE];

  mqd_t mq = mq_open(QUEUE_NAME, O_RDONLY);
  if (mq == (mqd_t)-1) {
    perror("Message queue open failed");
    exit(1);
  }

  int num_iterations = NUM_ITERATIONS;

  long long start_time = get_usec();

  unsigned int prio;

  while (num_iterations) {
    ssize_t bytes = mq_receive(mq, buffer, BUFFER_SIZE, &prio);
    num_iterations--;
  }

  long long end_time = get_usec();
  printf("Message queue receiver finished: %lld usec\n", end_time - start_time);
  mq_close(mq);
  mq_unlink(QUEUE_NAME);
}

// Message Queue Implementation
void mq_source_old(void) {
  char buffer[BUFFER_SIZE];
  random_bits(buffer, BUFFER_SIZE);

  struct mq_attr attr = {
      .mq_flags = 0, .mq_maxmsg = 10, .mq_msgsize = BUFFER_SIZE, .mq_curmsgs = 0};

  mqd_t mq = mq_open(QUEUE_NAME, O_WRONLY | O_CREAT, 0644, &attr);
  if (mq == (mqd_t)-1) {
    perror("Message queue open failed");
    exit(1);
  }

  long long start_time = get_usec();

  for (int i = 0; i < NUM_ITERATIONS; i++) {
    if (mq_send(mq, buffer, BUFFER_SIZE, 0) == -1) {
      perror("Message queue send failed");
      exit(1);
    }
  }

  long long end_time = get_usec();
  printf("Message queue sender finished: %lld usec (%.2f MB/s)\n", end_time - start_time,
         ((double)BUFFER_SIZE * NUM_ITERATIONS) / (end_time - start_time));
  mq_close(mq);
}

void mq_target_old(void) {
  char buffer[BUFFER_SIZE];
  unsigned int prio;

  mqd_t mq = mq_open(QUEUE_NAME, O_RDONLY);
  if (mq == (mqd_t)-1) {
    perror("Message queue open failed");
    exit(1);
  }

  long long start_time = get_usec();

  for (int i = 0; i < NUM_ITERATIONS; i++) {
    if (mq_receive(mq, buffer, BUFFER_SIZE, &prio) == -1) {
      perror("Message queue receive failed");
      exit(1);
    }
  }

  long long end_time = get_usec();
  printf("Message queue receiver finished: %lld usec (%.2f MB/s)\n", end_time - start_time,
         ((double)BUFFER_SIZE * NUM_ITERATIONS) / (end_time - start_time));
  mq_close(mq);
  mq_unlink(QUEUE_NAME);
}

void eventfd_sender(int efd, struct shared_data* shm) {
  // Initialize test data
  char test_data[BUFFER_SIZE];
  memset(test_data, 'A', BUFFER_SIZE);

  int num_iterations = NUM_ITERATIONS;

  long long start_time = get_usec();

  while (num_iterations) {
    // Write data to shared memory
    memcpy(shm->buffer, test_data, BUFFER_SIZE);
    shm->size = BUFFER_SIZE;

    // Signal receiver using eventfd
    uint64_t u = 1;
    write(efd, &u, sizeof(uint64_t));

    num_iterations--;
  }

  /*
  for (int i = 0; i < NUM_ITERATIONS; i++) {
      // Write data to shared memory
      memcpy(shm->buffer, test_data, BUFFER_SIZE);
      shm->size = BUFFER_SIZE;

      // Signal receiver using eventfd
      uint64_t u = 1;
      if (write(efd, &u, sizeof(uint64_t)) != sizeof(uint64_t)) {
          perror("write eventfd");
          exit(1);
      }
  }
  */

  long long end_time = get_usec();
  printf("Sender finished: %lld usec\n", end_time - start_time);
}

void eventfd_receiver(int efd, struct shared_data* shm) {
  int num_iterations = NUM_ITERATIONS;

  long long start_time = get_usec();

  while (num_iterations) {
    uint64_t u;
    read(efd, &u, sizeof(uint64_t));
    num_iterations--;
  }

  /*
  for (int i = 0; i < NUM_ITERATIONS; i++) {
      // Wait for signal from sender
      uint64_t u;
      if (read(efd, &u, sizeof(uint64_t)) != sizeof(uint64_t)) {
          perror("read eventfd");
          exit(1);
      }

      // Process data from shared memory
      // In this example, we just verify the size
      if (shm->size != BUFFER_SIZE) {
          fprintf(stderr, "Size mismatch: expected %d, got %zu\n",
                  BUFFER_SIZE, shm->size);
          exit(1);
      }
  }
  */

  long long end_time = get_usec();
  printf("Receiver finished: %lld usec\n", end_time - start_time);
}

// Shared Memory with eventfd Implementation
void eventfd_sender_old(int efd, struct shared_data* shm) {
  char buffer[BUFFER_SIZE];
  random_bits(buffer, BUFFER_SIZE);

  long long start_time = get_usec();

  for (int i = 0; i < NUM_ITERATIONS; i++) {
    memcpy(shm->buffer, buffer, BUFFER_SIZE);
    shm->size = BUFFER_SIZE;

    uint64_t u = 1;
    if (write(efd, &u, sizeof(uint64_t)) != sizeof(uint64_t)) {
      perror("eventfd write failed");
      exit(1);
    }
  }

  long long end_time = get_usec();
  printf("Shared Memory sender finished: %lld usec (%.2f MB/s)\n", end_time - start_time,
         ((double)BUFFER_SIZE * NUM_ITERATIONS) / (end_time - start_time));
}

void eventfd_receiver_old(int efd, struct shared_data* shm) {
  char buffer[BUFFER_SIZE];
  long long start_time = get_usec();

  for (int i = 0; i < NUM_ITERATIONS; i++) {
    uint64_t u;
    if (read(efd, &u, sizeof(uint64_t)) != sizeof(uint64_t)) {
      perror("eventfd read failed");
      exit(1);
    }

    if (shm->size != BUFFER_SIZE) {
      fprintf(stderr, "Size mismatch: expected %d, got %zu\n", BUFFER_SIZE, shm->size);
      exit(1);
    }
    memcpy(buffer, shm->buffer, BUFFER_SIZE);
  }

  long long end_time = get_usec();
  printf("Shared Memory receiver finished: %lld usec (%.2f MB/s)\n", end_time - start_time,
         ((double)BUFFER_SIZE * NUM_ITERATIONS) / (end_time - start_time));
}

int main(void) {
  printf("Buffer size: %d bytes\n", BUFFER_SIZE);
  printf("Number of iterations: %d\n\n", NUM_ITERATIONS);

  printf("=== FIFO Benchmark ===\n");
  mkfifo(FIFO_PATH, 0666);
  pid_t fifo_pid = fork();
  if (fifo_pid == 0) {
    fifo_target();
    exit(0);
  } else {
    sleep(1);  // Give receiver time to start
    fifo_source();
    wait(NULL);
  }
  unlink(FIFO_PATH);

  printf("\n=== Unix Domain Socket Benchmark ===\n");
  pid_t socket_pid = fork();
  if (socket_pid == 0) {
    socket_target();
    exit(0);
  } else {
    socket_source();
    wait(NULL);
  }

  printf("\n=== Message Queue Benchmark ===\n");
  pid_t mq_pid = fork();
  if (mq_pid == 0) {
    mq_target();
    exit(0);
  } else {
    mq_source();
    wait(NULL);
  }

  printf("\n=== Shared Memory + eventfd Benchmark ===\n");
  int efd = eventfd(0, 0);
  if (efd == -1) {
    perror("eventfd");
    return 1;
  }

  struct shared_data* shm = (struct shared_data*)mmap(
      NULL, sizeof(struct shared_data), PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
  pid_t shm_pid = fork();
  if (shm_pid == 0) {
    eventfd_receiver(efd, shm);
    exit(0);
  } else {
    eventfd_sender(efd, shm);
    wait(NULL);
  }

  close(efd);
  munmap(shm, sizeof(struct shared_data));
  return 0;
}
