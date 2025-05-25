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

#define SOCKET_PATH "/tmp/bench.sock"
#define FIFO_PATH "/tmp/bench.fifo"
#define QUEUE_NAME "/bench_queue"
#define DATA_SIZE (100 * 1024)  // 100KB
#define BUFFER_SIZE (1024 * 32)
#define NUM_ITERATIONS 10000

/*
  Note you may have to issue
  $ echo 10 > /proc/sys/fs/mqueue/msg_max
  $ echo 8192 > /proc/sys/fs/mqueue/msgsize_max
 */

struct shared_data {
  size_t size;
  char buffer[BUFFER_SIZE];
};

void print_bytes(char* buffer) {
  printf("First 10 bytes: ");
  for (int i = 0; i < 10; i++) {
    printf("0x%02x ", (unsigned char)buffer[i]);
  }
  printf("\n");
}

void random_bits(char* buffer, size_t num_bytes) {
  int fd = open("/dev/urandom", O_RDONLY);
  ssize_t bytes = read(fd, buffer, num_bytes);
  close(fd);
}

// Utility function to get current time in microseconds
long long get_usec(void) {
  struct timeval tv;
  gettimeofday(&tv, NULL);
  return tv.tv_sec * 1000000LL + tv.tv_usec;
}

// FIFO benchmark
void fifo_source(void) {
  char buffer[BUFFER_SIZE];
  random_bits(buffer, BUFFER_SIZE);

  printf("FIFO Sender:   ");
  print_bytes(buffer);

  int num_iterations = NUM_ITERATIONS;

  int fd = open(FIFO_PATH, O_WRONLY);
  if (fd < 0) {
    perror("FIFO open failed");
    exit(1);
  }

  long long start_time = get_usec();

  while (num_iterations) {
    ssize_t bytes = write(fd, buffer, BUFFER_SIZE);
    num_iterations--;
  }

  long long end_time = get_usec();
  printf("FIFO sender finished: %lld usec\n", end_time - start_time);
  close(fd);
}

void fifo_target(void) {
  char buffer[BUFFER_SIZE];

  int fd = open(FIFO_PATH, O_RDONLY);
  if (fd < 0) {
    perror("FIFO open failed");
    exit(1);
  }

  int num_iterations = NUM_ITERATIONS;

  long long start_time = get_usec();

  while (num_iterations) {
    ssize_t bytes = read(fd, buffer, BUFFER_SIZE);
    num_iterations--;
  }

  long long end_time = get_usec();

  printf("FIFO Receiver: ");
  print_bytes(buffer);

  printf("FIFO receiver finished: %lld usec\n", end_time - start_time);
  close(fd);
}

// Unix Domain Socket benchmark
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

  int num_iterations = NUM_ITERATIONS;

  long long start_time = get_usec();

  while (num_iterations) {
    ssize_t bytes = send(sock_fd, buffer, BUFFER_SIZE, 0);
    num_iterations--;
  }

  long long end_time = get_usec();
  printf("Socket sender finished: %lld usec\n", end_time - start_time);
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

  int num_iterations = NUM_ITERATIONS;

  long long start_time = get_usec();

  while (num_iterations) {
    ssize_t bytes = recv(client_fd, buffer, BUFFER_SIZE, 0);
    num_iterations--;
  }

  long long end_time = get_usec();
  printf("Socket receiver finished: %lld usec\n", end_time - start_time);
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

  long long end_time = get_usec();
  printf("Receiver finished: %lld usec\n", end_time - start_time);
}

#define BUFFER_SIZE (1024 * 1024)  // 1MB buffer
#define SHM_NAME "/my_shared_mem"

struct shared_info {
  void* buffer_addr;
  size_t buffer_size;
};

void print_maps(const char* who) {
  char path[64];
  snprintf(path, sizeof(path), "/proc/%d/maps", getpid());
  printf("\n=== Memory maps for %s (PID: %d) ===\n", who, getpid());
  FILE* f = fopen(path, "r");
  if (f) {
    char line[256];
    while (fgets(line, sizeof(line), f)) {
      printf("%s", line);
    }
    fclose(f);
  }
  printf("===================================\n\n");
}

void cma_sender(struct shared_info* info, pid_t receiver_pid, int sync_fd) {
  // Open existing shared memory
  int shm_fd = shm_open(SHM_NAME, O_RDWR, 0666);
  if (shm_fd == -1) {
    perror("shm_open sender");
    exit(1);
  }

  // Map the shared memory
  char* buffer = (char*)mmap(NULL, BUFFER_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
  if (buffer == MAP_FAILED) {
    perror("mmap sender");
    close(shm_fd);
    exit(1);
  }

  // Fill with test pattern
  memset(buffer, 'A', BUFFER_SIZE);

  printf("Sender (PID: %d) buffer at %p\n", getpid(), (void*)buffer);
  // print_maps("sender");

  long long start_time = get_usec();

  // Signal completion
  uint64_t sync_val = 1;
  if (write(sync_fd, &sync_val, sizeof(sync_val)) != sizeof(sync_val)) {
    perror("write sync");
    exit(1);
  }

  long long end_time = get_usec();
  printf("Run_sender finished: %lld usec\n", end_time - start_time);

  // Clean up
  munmap(buffer, BUFFER_SIZE);
  close(shm_fd);
}

void cma_receiver(struct shared_info* info, int sync_fd) {
  // Create shared memory
  shm_unlink(SHM_NAME);  // Remove any existing
  int shm_fd = shm_open(SHM_NAME, O_CREAT | O_RDWR, 0666);
  if (shm_fd == -1) {
    perror("shm_open receiver");
    exit(1);
  }

  // Set size
  if (ftruncate(shm_fd, BUFFER_SIZE) == -1) {
    perror("ftruncate");
    close(shm_fd);
    exit(1);
  }

  // Map the shared memory
  char* buffer = (char*)mmap(NULL, BUFFER_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
  if (buffer == MAP_FAILED) {
    perror("mmap receiver");
    close(shm_fd);
    exit(1);
  }

  // Clear buffer
  memset(buffer, 0, BUFFER_SIZE);

  printf("Receiver (PID: %d) buffer at %p\n", getpid(), (void*)buffer);
  // print_maps("receiver");

  // Store buffer address in shared info
  info->buffer_addr = buffer;
  info->buffer_size = BUFFER_SIZE;

  long long start_time = get_usec();

  // Wait for sender completion
  uint64_t sync_val;
  if (read(sync_fd, &sync_val, sizeof(sync_val)) != sizeof(sync_val)) {
    perror("read sync");
    exit(1);
  }

  long long end_time = get_usec();
  end_time -= 1000000LL;
  printf("Run_receiver finished: %lld usec\n", end_time - start_time);

#ifdef CMA_DIAGNOSTICS
  // Print first bytes
  printf("First 10 bytes received: ");
  for (int i = 0; i < 10; i++) {
    if (buffer[i] == 'A') {
      printf("A");
    } else {
      printf("0x%02x ", (unsigned char)buffer[i]);
    }
  }
  printf("\n");

  // Verify data
  int matches = 0;
  for (size_t i = 0; i < BUFFER_SIZE; i++) {
    if (buffer[i] == 'A') {
      matches++;
    }
  }

  printf("Found %d matching bytes out of %d\n", matches, BUFFER_SIZE);
  if (matches == BUFFER_SIZE) {
    printf("Data verification succeeded!\n");
  } else {
    printf("Data verification failed!\n");

    // Print more debug info
    printf("First non-matching byte at: position %zu (got 0x%02x)\n",
           (size_t)(buffer[matches] != 'A' ? matches : 0), (unsigned char)buffer[matches]);
  }
#endif

  // Clean up
  munmap(buffer, BUFFER_SIZE);
  close(shm_fd);
  shm_unlink(SHM_NAME);
}

int main(void) {
  printf("\n=== Cross Memory Attach + eventfd Benchmark ===\n");

  struct shared_info* info = (struct shared_info*)mmap(
      NULL, sizeof(struct shared_info), PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
  if (info == MAP_FAILED) {
    perror("mmap");
    return 1;
  }

  int sync_fd = eventfd(0, 0);
  if (sync_fd == -1) {
    perror("eventfd");
    return 1;
  }

  pid_t pid = fork();
  if (pid < 0) {
    perror("fork");
    return 1;
  }

  if (pid == 0) {
    cma_receiver(info, sync_fd);
    exit(0);
  } else {
    cma_sender(info, pid, sync_fd);
    int status;
    waitpid(pid, &status, 0);
    if (WIFEXITED(status)) {
      printf("Receiver exited with status %d\n", WEXITSTATUS(status));
    }
  }

  int efd = eventfd(0, EFD_SEMAPHORE);
  if (efd == -1) {
    perror("eventfd");
    return 1;
  }

  struct shared_data* shm = (struct shared_data*)mmap(
      NULL, sizeof(struct shared_data), PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);

  if (shm == MAP_FAILED) {
    perror("mmap");
    close(efd);
    return 1;
  }

  printf("=== FIFO Benchmark ===\n");
  mkfifo(FIFO_PATH, 0666);

  pid_t fifo_pid = fork();
  if (fifo_pid == 0) {
    fifo_target();
    exit(0);
  } else {
    fifo_source();
    wait(NULL);
  }
  unlink(FIFO_PATH);

  printf("\n=== Socket Benchmark ===\n");
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
  pid_t eventfd_pid = fork();
  if (eventfd_pid == 0) {
    eventfd_receiver(efd, shm);
    exit(0);
  } else {
    eventfd_sender(efd, shm);
    wait(NULL);
  }

  close(sync_fd);
  munmap(info, sizeof(struct shared_info));
  return 0;
}
