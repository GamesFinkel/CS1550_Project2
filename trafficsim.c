#include <sys/mman.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <linux/unistd.h>

struct cs1550_sem{
  int value;
  struct cs1550_node* head;
  struct cs1550_node* tail;
};

void down(struct cs1550_sem *sem) {
       syscall(__NR_sys_cs1550_down, sem);
}

void up(struct cs1550_sem *sem) {
       syscall(__NR_sys_cs1550_up, sem);
}

int main(int argc, char* argv[])
{
  // Seed the random number generator to get random values
  srand(time(NULL));

  // How many cars can fit at each side of the road
  int buffer_size = 10;
  // Allow user to change buffer size
  if(argc == 2)
    buffer_size = atoi(argv[1]);

  // Keep track of the start time,
  // the # of cars across both directions,
  // the direction the flagperson is looking,
  // and if a car has honked yet
  void *start_mem = mmap(NULL,sizeof(time_t),PROT_READ|PROT_WRITE, MAP_SHARED|MAP_ANONYMOUS, 0, 0);
  void *long_car_mem = mmap(NULL,sizeof(long),PROT_READ|PROT_WRITE, MAP_SHARED|MAP_ANONYMOUS, 0, 0);
  void *dir_mem = mmap(NULL,sizeof(char),PROT_READ|PROT_WRITE, MAP_SHARED|MAP_ANONYMOUS, 0, 0);
  void *honk = mmap(NULL,sizeof(char),PROT_READ|PROT_WRITE, MAP_SHARED|MAP_ANONYMOUS, 0, 0);

  // After creating space in memory,
  // Designate the start pointer of each block
  time_t* start_time = (time_t*) start_mem;
  long* total_cars = (long*) long_car_mem;
  char* direction = (char*) dir_mem;
  char* honked = (char*) honk;

  // Create space for six semaphores,
  // then for each direction, we need the next spot to produce to and consume from
  // followed by the buffer itself
  void *sem_memory = mmap(NULL, sizeof(struct cs1550_sem)*6, PROT_READ|PROT_WRITE, MAP_SHARED|MAP_ANONYMOUS, 0, 0);
  void *north_memory = mmap(NULL, sizeof(int)*(buffer_size+2), PROT_READ|PROT_WRITE, MAP_SHARED|MAP_ANONYMOUS, 0, 0);
  void *south_memory = mmap(NULL, sizeof(int)*(buffer_size+2), PROT_READ|PROT_WRITE, MAP_SHARED|MAP_ANONYMOUS, 0, 0);

  // Semaphores to keep track of mutual exclusion,
  // full and empty spots from the north,
  // full and empty spots from the south,
  // and number of cars in the intersection
  struct cs1550_sem* mutex = (struct cs1550_sem*)sem_memory;
  struct cs1550_sem* n_full = (struct cs1550_sem*)sem_memory+1;
  struct cs1550_sem* n_empty = (struct cs1550_sem*)sem_memory+2;
  struct cs1550_sem* s_full = (struct cs1550_sem*)sem_memory+3;
  struct cs1550_sem* s_empty = (struct cs1550_sem*)sem_memory+4;
  struct cs1550_sem* car_sem = (struct cs1550_sem*)sem_memory+5;

  // Set all semaphore values
  mutex->value = 1;
  n_full->value = 0;
  s_full->value = 0;
  n_empty->value = buffer_size;
  s_empty->value = buffer_size;
  car_sem->value = 0;

  // Null all heads
  mutex->head = NULL;
  n_full->head = NULL;
  s_full->head = NULL;
  n_empty->head = NULL;
  s_empty->head = NULL;
  car_sem->head = NULL;

  // Null all tails
  mutex->tail = NULL;
  n_full->tail = NULL;
  s_full->tail = NULL;
  n_empty->tail = NULL;
  s_empty->tail = NULL;
  car_sem->tail = NULL;

  // Set our variables to defaults
  *total_cars = 0;
  *direction = ' ';
  *honked = 'N';

  // No cars yet, so the flagperson can sleep
  printf("The flagperson is now asleep.\n");

  // Hold the pointers for where to produce and consume and where the buffer starts
  // For the north
  int* n_prod_ptr = (int*)north_memory;
  int* n_cons_ptr = (int*)north_memory+1;
  int* n_buff_ptr = (int*)north_memory+2;

  // Hold the pointers for where to produce and consume and where the buffer starts
  // For the south
  int* s_prod_ptr = (int*)south_memory;
  int* s_cons_ptr = (int*)south_memory+1;
  int* s_buff_ptr = (int*)south_memory+2;

  // We are going to start producing/consuming from the front
  *n_prod_ptr = 0;
  *n_cons_ptr = 0;
  *s_prod_ptr = 0;
  *s_cons_ptr = 0;

  // Set all values in the buffers to be 0
  // There is not a car yet, so we don't want them holding anything besides 0
  int x;
  for(x = 0; x<buffer_size;x++)
  {
    n_buff_ptr[x] = 0;
    s_buff_ptr[x] = 0;
  }
  // Set our start time
  *start_time = time(NULL);
  // Start forking our processes
  if(fork()==0)
  {
    // North Producer
    if(fork()==0)
    {
      while(1)
      {
        down(n_empty);
        down(mutex);
        // Increment the total number of cars that have passed through the intersection
        *total_cars = *total_cars + 1;
        // Store the car number in the buffer
        n_buff_ptr[*n_prod_ptr%buffer_size] = *total_cars;
        // Increment which spot in the buffer we are producing to
        *n_prod_ptr = *n_prod_ptr + 1;
        // Check to see if the car needs to honk
        if(car_sem->value <= 0 && *honked == 'N')
        {
          // HONK
          *honked = 'Y';
          *direction = 'N';
          printf("The flagperson is now awake.\n");
          printf("Car %d coming from the N direction, blew their horn at time %d.\n",*total_cars,time(NULL)-*start_time);
        }
        // Arrive in the buffer
        printf("Car %d coming from the N direction arrived in the queue at time %d.\n",*total_cars,time(NULL)-*start_time);
        up(mutex);
        up(n_full);
        up(car_sem);
        // If no more cars are coming, we sleep
        if(rand()%10 >= 8)
          sleep(20);
      }
    }
    // South Producer
    else
    {
      while(1)
      {
        down(s_empty);
        down(mutex);
        // Increment the total number of cars that have passed through the intersection
        *total_cars = *total_cars + 1;
        // Store the car number in the buffer
        s_buff_ptr[*s_prod_ptr%buffer_size] = *total_cars;
        // Increment which spot in the buffer we are producing to
        *s_prod_ptr = *s_prod_ptr + 1;
        // Check to see if the car needs to honk
        if(car_sem->value <= 0 && *honked == 'N')
        {
          // HONK
          *direction = 'S';
          *honked = 'Y';
          printf("The flagperson is now awake.\n");
          printf("Car %d coming from the S direction, blew their horn at time %d.\n",*total_cars,time(NULL)-*start_time);
        }
        // Arrive in the buffer
        printf("Car %d coming from the S direction arrived in the queue at time %d.\n",*total_cars,time(NULL)-*start_time);
        up(mutex);
        up(s_full);
        up(car_sem);
        // If no more cars are coming, we sleep
        if(rand()%10 >= 8)
          sleep(20);
      }
    }
  }
  // Consumer
  if(fork()==0)
  {
    while(1)
    {
      // Variables to hold the car number and direction
      int car_num;
      char car_dir;
      down(mutex);
      // Check if there are no cars in the array, if so prepare to sleep
      if(car_sem->value == 0)
      {
        printf("The flagperson is now asleep.\n");
        *honked = 'N';
      }
      up(mutex);
      // Actually sleep
      down(car_sem);
      down(mutex);
      // We should never be in the consumer without a car in a queue
      if(car_sem->value < 0)
      {
        printf("ERRORERROR\n");
        exit(1);
      }
      // If the other buffer is above 10, we should switch directions
      if(*direction == 'N' && s_full->value >= 10)
        *direction = 'S';
      else if(*direction == 'S' && n_full->value >= 10)
        *direction = 'N';

      // Depending on the direction, we need to consume cars from the N or the S
      if(*direction == 'N')
      {
        down(n_full);
        car_num = n_buff_ptr[(*n_cons_ptr)%buffer_size];
        car_dir = 'N';
        *n_cons_ptr = *n_cons_ptr + 1;
        // If there are no more cars from the N, switch directions
        if(n_full->value == 0)
          *direction = 'S';
      }
      else if(*direction == 'S')
      {
        down(s_full);
        car_num = s_buff_ptr[(*s_cons_ptr)%buffer_size];
        car_dir = 'S';
        *s_cons_ptr = *s_cons_ptr + 1;
        // If there are no more cars from the S, switch directions
        if(s_full->value == 0)
          *direction = 'N';
      }
      up(mutex);
      sleep(2);
      printf("Car %d coming from the %c direction left the construction zone at time %d.\n",car_num,car_dir,time(NULL)-*start_time);
      if(car_dir == 'N')
      up(n_empty);
      if(car_dir == 'S')
      up(s_empty);
    }
  }
  int waiting;
  wait(&waiting);
  return 0;
}
