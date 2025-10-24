#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdint.h>

// Headers as needed
// made the seed value
const uint32_t SEED_VALUE = 200;

typedef enum {false, true} bool;
typedef enum {ST_UNSTARTED =0, ST_READY=1, ST_RUNNING=2, ST_BLOCKED=3, ST_TERMINATED=4} ProcState;// Allows boolean types in C


/* Defines a job struct */
typedef struct Process {
    uint32_t A;                         // A: Arrival time of the process
    uint32_t B;                         // B: Upper Bound of CPU burst times of the given random integer list
    uint32_t C;                         // C: Total CPU time required
    uint32_t M;                         // M: Multiplier of CPU burst time
    uint32_t processID;                 // The process ID given upon input read
    ProcState state;                    // the state
    uint32_t currentWaitingTime;        // Time spent in ready
    uint32_t remainingCPU;              //remaining total cpu demand
    uint32_t finishingTime;             //when the cycle terminates
    uint32_t currentCPUTimeRun;         //total time the cpu ran
    uint32_t currentIOBlockedTime;      // total time spend in I/O blocked

    uint32_t curCPUBurstLeft;           // ticks left in the current burst
    uint32_t curIOBurstLeft;            // ticks left in current burst
    uint32_t lastCPUBurstLength;        // the length of the last cpu burst used to calculate I/O


    // Add other fields as needed
} _process;


static uint32_t TOTAL_CREATED_PROCESSES = 0;   // The total number of processes constructed
static uint32_t CURRENT_CYCLE= 0; // the global time
static uint32_t TOTAL_FINISHED_PROCESSES = 0; // loop exit condition for each alg
static uint32_t TOTAL_NUMBER_OF_CYCLES_SPENT_BLOCKED = 0; // increments on cycles when at least one process is blocked.
// Additional variables as needed


/**
 * Reads a random non-negative integer X from a file with a given line named random-numbers (in the current directory)
 */

// this is a helper function which I made to skip the // # lines, skip no digit chars, and return a 0 on success or -1 on failure

static int getnext(FILE* f, unsigned* out) {
    int ch;
    // infinite loops till break condition is reached
    for (;;) {
        ch = fgetc(f);
        // if EOF break
        if (ch==EOF) {
            return -1;
        }
        if (ch=='/') {
            int n2 = fgetc(f);
            if (n2=='/') {
                while (ch != '\n' && ch != EOF) {
                    ch = fgetc(f);
                }
                continue;
            }
            else {
                ungetc(n2, f);
            }

        }
        if (ch == '#') { // check for these
            while (ch != '\n' && ch != EOF) {
                ch = fgetc(f);
            }
            continue;
        }

        if (isdigit((unsigned char)ch)) {
            break;
        }
        else {
            continue;
        }


    }
    unsigned v = (unsigned)(ch - '0');

    for (;;){ // another inf loop till break is met
        ch = fgetc(f);
        if (ch == EOF) {
            break;
        }
        if (!isdigit((unsigned char)ch)) {
            ungetc(ch, f);
            break;
        }
        v = v * 10u + (unsigned)(ch - '0');

    }
    *out = v;
    return 0;
}

//opens the file and reads the first int.
// allocates an array of _process that is the size n.

static int readinp(const char* path, _process ** out, uint32_t *nout) {
    FILE* file = fopen(path, "r");
    if (!file) {
        perror("open input");
        return -1;
    }
    unsigned n = 0;
    if (getnext(file, &n)!= 0) {
        fclose(file);
        return -1;
    }
    _process* array = (_process*)calloc(n, sizeof(_process));
    if (!array) {
        fclose(file);
        return -1;
    }
    for (unsigned i = 0; i < n; i++) { // loops n times reading into the A, B, C, and M
        unsigned A, B, C, M;
        if (getnext(file, &A) != 0 || getnext(file, &B) != 0 || getnext(file, &C) != 0 || getnext(file, &M) != 0) {
            free(array);
            fclose(file);
            return -1;
        }// initialize the fields.
        array[i].A = A;
        array[i].B = B;
        array[i].C = C;
        array[i].M = M;
        array[i].processID = i;
        array[i].state = ST_UNSTARTED;
        array[i].currentWaitingTime = 0;
        array[i].remainingCPU = C;
        array[i].finishingTime = 0;
        array[i].currentCPUTimeRun= 0;
        array[i].currentIOBlockedTime = 0;
        array[i].curCPUBurstLeft=0;
        array[i].curIOBurstLeft=0;
        array[i].lastCPUBurstLength = 0;

        // done reading the input
    }

    fclose(file); // close dat file
    *out = array;
    *nout = (uint32_t)n;
    return 0;
}

// for the fifo queue
typedef struct {
    int* buffer;
    int capacity;
    int head;
    int tail;
    int size;
}Queue;

// allocating buffers and space for things
static void q_init(Queue *q, int capacity) {
    q->buffer = (int*)malloc(capacity * sizeof(int));
    q->capacity = capacity;
    q->head = 0;
    q->tail = 0;
    q->size = 0;
}
//free da buffer
static void q_free(Queue *q) {
    free(q->buffer);
}
//tells you if the buffer is empty
static int q_empty(Queue *q) {
    return q->size==0;
}
//preserves fifo even when resizing, resizes buffer, and eques tail.
static void q_push(Queue *q, int value) {
    if (q->size == q->capacity) {
        int n = q->capacity *2+8;
        int* nx = (int*)malloc(sizeof(int)*n);
        for (int i = 0; i < q->size; i++) {
            nx[i] = q->buffer[(q->head + i) % (q->capacity)];

        }
        free(q->buffer);
        q->buffer = nx;
        q->capacity = n;
        q->head = 0;
        q->tail=q->size;

    }
    q->buffer[q->tail] = value;
    q->tail = (q->tail + 1) % q->capacity;
    q->size++;
}
// do the popping, deques from head
static int q_pop(Queue *q) {
    int value = q->buffer[q->head];
    q->head = (q->head + 1) % q->capacity;
    q->size--;
    return value;
}


// compares two _processes used in the sorted input header.

static int cmpArrivalTime(const void* a, const void* b) {
    const _process* p1 = ( const _process*)a;
    const _process* p2 = ( const _process*)b;
    if (p1->A != p2->A) {
        return (p1->A < p2->A) ? -1 : 1;
    }
    if (p1->processID != p2->processID) {
        return (p1->processID < p2->processID) ? -1 : 1;

    }
    return 0;
}






// the teacher made this
uint32_t getRandNumFromFile(uint32_t line, FILE* random_num_file_ptr){
    uint32_t end, loop;
    char str[512];

    rewind(random_num_file_ptr); // reset to be beginning
    for(end = loop = 0;loop<line;++loop){
        if(0==fgets(str, sizeof(str), random_num_file_ptr)){ //include '\n'
            end = 1;  //can't input (EOF)
            break;
        }
    }
    if(!end) {
        return (uint32_t) atoi(str);
    }

    // Fail-safe return
    return (uint32_t) 1804289383;
}



/**
 * Reads a random non-negative integer X from a file named random-numbers.
 * Returns the CPU Burst: : 1 + (random-number-from-file % upper_bound)
 */
// the teacher made this
uint32_t randomOS(uint32_t upper_bound, uint32_t process_indx, FILE* random_num_file_ptr)
{
    char str[20];

    uint32_t unsigned_rand_int = (uint32_t) getRandNumFromFile(SEED_VALUE+process_indx, random_num_file_ptr);
    uint32_t returnValue = 1 + (unsigned_rand_int % upper_bound);

    return returnValue;
}

//first come first serve function
static void do_fcfs(_process* P, uint32_t N, FILE* rndf) {
    Queue ready;
    q_init(&ready, (int)(N+8));
    TOTAL_FINISHED_PROCESSES =0;
    CURRENT_CYCLE = 0;
    TOTAL_FINISHED_PROCESSES = 0;
    TOTAL_NUMBER_OF_CYCLES_SPENT_BLOCKED = 0;

    int running = -1;
    // reset the counters and cpu starts idle
    while (TOTAL_FINISHED_PROCESSES < N) {

        // move arrivals to the ready queue, keeps tracket of order

        for (uint32_t i = 0; i < N; i++) {
            if (P[i].state == ST_UNSTARTED && P[i].A == CURRENT_CYCLE) {
                P[i].state = ST_READY;
                q_push(&ready, (int)i);
            }
        }
        // if idle pick the next by poping from head. if there are no burst left, compute one with the randomOS,
        //attached to remaining cpu and record the lastCPUBUrstLenght. also handles the I/O ticks
        if (running == -1) {
            if (!q_empty(&ready)) {
                int next = q_pop(&ready);
                _process* R = &P[next];
                R->state = ST_RUNNING;

                if (R->curCPUBurstLeft ==0) {
                    uint32_t daburst = randomOS(R->B, R->processID, rndf);
                    if (daburst > R->remainingCPU) {
                        daburst = R->remainingCPU;
                    }
                    R->curCPUBurstLeft = daburst;
                    R->lastCPUBurstLength = daburst;
                }
                running = next;
            }
        }
        //run one cycle for the running process
        if (running != -1) {
            _process* R = &P[running];
            R->curCPUBurstLeft--;
            R->remainingCPU--;
            R->currentCPUTimeRun++;
        }

        int dablocked =0;

        for (uint32_t i = 0; i < N; i++) {
            if (P[i].state == ST_BLOCKED) {
                dablocked =1;
                P[i].curIOBurstLeft--;
                P[i].currentIOBlockedTime++;
            }
        }

        if (dablocked) {
            TOTAL_NUMBER_OF_CYCLES_SPENT_BLOCKED++;
        }

        // see if it terminates
        if (running != -1) {
            _process* R = &P[running];
            if (R->remainingCPU == 0) {
                R->state = ST_TERMINATED;
                R->finishingTime = CURRENT_CYCLE+1;
                running = -1;
                TOTAL_FINISHED_PROCESSES++;
            }
            else if (R->curCPUBurstLeft==0) {
                R->state = ST_BLOCKED;
                R->curIOBurstLeft = R->lastCPUBurstLength * R->M;
                running = -1;
            }
        }

        for (uint32_t i = 0; i < N; i++) {
            if (P[i].state == ST_READY) {
                P[i].currentWaitingTime++;
            }
        }

        // do the IO completions. put proccesses in the buffer, sort the buffer, and push then in order.
        {
            int cnt = 0;
            int* buf = (int*)alloca(sizeof(int) * N);
            for (uint32_t i = 0; i < N; i++) {
                if (P[i].state == ST_BLOCKED && P[i].curIOBurstLeft == 0) {
                    P[i].state = ST_READY;
                    buf[cnt++] = (int)i;
                }
            }
            // insertion sort by (A, processID)
            for (int x = 1; x < cnt; ++x) {
                int k = buf[x], y = x - 1;
                while (y >= 0 && (P[buf[y]].A > P[k].A ||
                       (P[buf[y]].A == P[k].A && P[buf[y]].processID > P[k].processID))) {
                    buf[y + 1] = buf[y];
                    --y;
                       }
                buf[y + 1] = k;
            }
            for (int t = 0; t < cnt; ++t) q_push(&ready, buf[t]);
        }

        CURRENT_CYCLE++;


    }
    q_free(&ready);
}

//doing round robin. Reserts per run counter that may have been used.
// arrivals work the same as FCFS.
static void do_RR(_process* P, uint32_t nah, FILE* rndf, uint32_t Qslice) {
    Queue ready;
    q_init(&ready, ((int)(nah+8)));

    TOTAL_FINISHED_PROCESSES = 0;
    TOTAL_FINISHED_PROCESSES = 0;
    CURRENT_CYCLE = 0;
    TOTAL_NUMBER_OF_CYCLES_SPENT_BLOCKED = 0;
    // running = -1 means its idle
    int running = -1;
    uint32_t sLeft = 0;
    // while loop until all the processes terminate, I named them nah
    while (TOTAL_FINISHED_PROCESSES < nah) {
        // move processes to ready when thier arrival matches the cycle
        for (uint32_t i = 0; i < nah; i++) {
            if (P[i].state == ST_UNSTARTED && P[i].A == CURRENT_CYCLE) {
                P[i].state = ST_READY;
                q_push(&ready, (int)i);
            }
        }
        // if the CPU is idle, we can take the next procss from the ready queue and starts the process for it.
        // round robin uses times slices for this
        if (running == -1 && !q_empty(&ready)) {
            int next = q_pop(&ready);
            _process* R = &P[next];
            R->state = ST_RUNNING;
            if (R->curCPUBurstLeft == 0) {
                uint32_t daburst = randomOS(R->B, R->processID, rndf);
                if (daburst > R->remainingCPU) {
                    daburst = R->remainingCPU;

                }
                R->curCPUBurstLeft = daburst;
                R->lastCPUBurstLength = daburst;

            }
            running = next;
            sLeft = Qslice;


        }
        // if a process is running, this executes a tick to consume the cpu burst/ slice.
        if (running != -1) {
            _process* R = &P[running];
            R->curCPUBurstLeft--;
            R->remainingCPU--;
            R->currentCPUTimeRun++;
            sLeft--; // eat the slice
        }
        // all blocked proccess tick the I/O to count forward in the cycle.
        int dablocked =0;
        for (uint32_t i = 0; i < nah; i++) {
            if (P[i].state == ST_BLOCKED) {
                dablocked =1;
                P[i].curIOBurstLeft--;
                P[i].currentIOBlockedTime++;
            }
        }
        // keeping track
        if (dablocked) {
            TOTAL_NUMBER_OF_CYCLES_SPENT_BLOCKED++;
        }
        //
        if (running != -1) {
            _process* R = &P[running];

            if (R->remainingCPU == 0) {// if processes finished for the cpu then terminate
                R->state = ST_TERMINATED;
                R->finishingTime = CURRENT_CYCLE+1;
                running = -1;
                TOTAL_FINISHED_PROCESSES++;
            }
            else if (R->curCPUBurstLeft == 0) { // if the cpu burst is finished, block for the I/O
                R->state = ST_BLOCKED;
                R->curIOBurstLeft = R->lastCPUBurstLength * R->M;
                running = -1;
            }
            else if (sLeft == 0) { // if the time slices expired, go back to ready
                R->state = ST_READY;
                q_push(&ready, running);
                running = -1;
            }
        }
        // change the waiting time for all processes back to ready.
        for (uint32_t i = 0; i < nah; i++) {
            if (P[i].state == ST_READY) {
                P[i].currentWaitingTime++;
            }
        }


        {
            int cnt = 0;
            int* buf = (int*)alloca(sizeof(int) * nah); // temp buffer
            for (uint32_t i = 0; i < nah; i++) {
                if (P[i].state == ST_BLOCKED && P[i].curIOBurstLeft == 0) {
                    P[i].state = ST_READY;
                    buf[cnt++] = (int)i;
                }// collect stuff that finsihed the I/O
            }
            for (int x = 1; x < cnt; ++x) { // sort the collected stuff again by A and processID
                int key = buf[x], y = x - 1;
                while (y >= 0 && (P[buf[y]].A > P[key].A ||
                       (P[buf[y]].A == P[key].A && P[buf[y]].processID > P[key].processID))) {
                    buf[y + 1] = buf[y];
                    --y;
                       }
                buf[y + 1] = key;
            }
            for (int t = 0; t < cnt; ++t) q_push(&ready, buf[t]); // push it back into queue
        }
        CURRENT_CYCLE++; // advance the global cycles
    }
    q_free(&ready); // free da boy

}



// this section was awful becuse I missread the instruction and had to re-do it. then i realized the i missread them
// a second time and what I did the first time was closer to correct so I had to re-do it again. not very fun.
// this is just doing the shortest job first.
static void dosjf(_process* P, uint32_t nah, FILE* rndf) {
    Queue ready;
    q_init(&ready, ((int)(nah+8)));
    TOTAL_FINISHED_PROCESSES = 0;
    TOTAL_NUMBER_OF_CYCLES_SPENT_BLOCKED = 0;
    CURRENT_CYCLE = 0;

    int running = -1;
    // reset all the things again
    while (TOTAL_FINISHED_PROCESSES < nah) {


        int cnt = 0;
        int* buf = (int*)alloca(sizeof(int) * nah); // da buffer
        for (uint32_t i = 0; i < nah; i++) { // add new stuff to ready
            if (P[i].state == ST_UNSTARTED && P[i].A == CURRENT_CYCLE) {
                P[i].state = ST_READY;
                q_push(&ready, (int)i);
            }
        }

        if (running == -1) { //
            int b = -1;
            for (int j = 0; j < ready.size; j++) {
                int idx = ready.buffer[(ready.head + j) % ready.capacity];
                // if the cpu is idle, we scan ready and pick the one with the smallest remaining cpu.
                // this is part of the tie breaking stuff.
                if (b == -1 ||
                    (P[idx].remainingCPU < P[b].remainingCPU) ||
                    (P[idx].remainingCPU == P[b].remainingCPU &&
                     (P[idx].A < P[b].A ||
                      (P[idx].A == P[b].A && P[idx].processID < P[b].processID)))) {
                    b = idx;
                      }
            }

            // remove proccess b from the ready queue.
            if (b != -1) {
                int newS = 0;
                for (int j = 0; j < ready.size; j++) {
                    int idx = ready.buffer[(ready.head + j) % ready.capacity];
                    if (idx != b) {
                        ready.buffer[(ready.head + newS) % ready.capacity] = idx;
                        newS ++;
                    }
                }

                ready.tail = (ready.head + newS) % ready.capacity;
                ready.size = newS;
                // dispathc the chosen process and mark as running. also do cpu burst if needed.
                _process* R = &P[b];
                R->state = ST_RUNNING;
                if (R->curCPUBurstLeft == 0) {
                    uint32_t daburst = randomOS(R->B, R->processID, rndf);
                    if (daburst > R->remainingCPU) {
                        daburst = R->remainingCPU;
                    }
                    R->curCPUBurstLeft = daburst;
                    R->lastCPUBurstLength = daburst;
                }
                running = b;
            }
        }

        // do for one cycle
        if (running != -1) {
            _process* R = &P[running];
            R->curCPUBurstLeft--;
            R->remainingCPU--;
            R->currentCPUTimeRun++;
        }
        // keep track of the blocked stuff again.
        int dablocked =0;
        for (uint32_t i = 0; i < nah; i++) {
            if (P[i].state == ST_BLOCKED) {
                dablocked =1;
                P[i].curIOBurstLeft--;
                P[i].currentIOBlockedTime++;
            }
        }

        if (dablocked) {
            TOTAL_NUMBER_OF_CYCLES_SPENT_BLOCKED++;
        }
        // check ifthings terminated, or just finsihed a cpu burst
        if (running != -1) {
            _process* R = &P[running];
            if (R->remainingCPU == 0) {
                R->state = ST_TERMINATED;
                R->finishingTime = CURRENT_CYCLE+1;
                running = -1;
                TOTAL_FINISHED_PROCESSES++;
            }
            else if (R->curCPUBurstLeft == 0) {
                R->state = ST_BLOCKED;
                R->curIOBurstLeft = R->lastCPUBurstLength * R->M;
                running = -1;
            }
        }
        // add one cycle to waiting timei for everything stuck at ready.
        for (uint32_t i = 0; i < nah; i++) {
            if (P[i].state == ST_READY) {
                P[i].currentWaitingTime++;
            }
        }

        // this works pretty much the same as the one for round robin did, yay for reusing things.
        {

            for (uint32_t i = 0; i < nah; i++) {
                if (P[i].state == ST_BLOCKED && P[i].curIOBurstLeft ==0) {
                    P[i].state = ST_READY;
                    buf[cnt++] = (int)i;
                }
            }
            for (int x = 1; x < cnt; ++x) {
                int key = buf[x], y = x - 1;
                while (y >= 0 && (P[buf[y]].A > P[key].A ||
                       (P[buf[y]].A == P[key].A && P[buf[y]].processID > P[key].processID))) {
                    buf[y + 1] = buf[y];
                    --y;
                       }
                buf[y + 1] = key;
            }
            for (int t = 0; t < cnt; ++t) q_push(&ready, buf[t]);
        }




        CURRENT_CYCLE++;
    }
    q_free(&ready);
}




/********************* SOME PRINTING HELPERS *********************/


/**
 * Prints to standard output the original input
 * process_list is the original processes inputted (in array form)
 */
// made by the teacher
void printStart(_process process_list[])
{
    printf("The original input was: %i", TOTAL_CREATED_PROCESSES);

    uint32_t i = 0;
    for (; i < TOTAL_CREATED_PROCESSES; ++i)
    {
        printf(" ( %u %u %u %u)", process_list[i].A, process_list[i].B,
               process_list[i].C, process_list[i].M);
    }
    printf("\n");
}

/**
 * Prints to standard output the final output
 * finished_process_list is the terminated processes (in array form) in the order they each finished in.
 */

// made by the teacher
void printFinal(_process finished_process_list[])
{
    printf("The (sorted) input is: %i", TOTAL_CREATED_PROCESSES);

    uint32_t i = 0;
    for (; i < TOTAL_FINISHED_PROCESSES; ++i)
    {
        printf(" ( %u %u %u %u)", finished_process_list[i].A, finished_process_list[i].B,
               finished_process_list[i].C, finished_process_list[i].M);
    }
    printf("\n");
} // End of the print final function

/**
 * Prints out specifics for each process  (helper function, you may need to adjust variables accordingly)
 * @param process_list The original processes inputted, in array form
 */

// also made by the teacher
void printProcessSpecifics(_process process_list[])
{
    uint32_t i = 0;
    printf("\n");
    for (; i < TOTAL_CREATED_PROCESSES; ++i)
    {
        printf("Process %i:\n", process_list[i].processID);
        printf("\t(A,B,C,M) = (%i,%i,%i,%i)\n", process_list[i].A, process_list[i].B,
               process_list[i].C, process_list[i].M);
        printf("\tFinishing time: %i\n", process_list[i].finishingTime);
        printf("\tTurnaround time: %i\n", process_list[i].finishingTime - process_list[i].A);
        printf("\tI/O time: %i\n", process_list[i].currentIOBlockedTime);
        printf("\tWaiting time: %i\n", process_list[i].currentWaitingTime);
        printf("\n");
    }
} // End of the print process specifics function

/**
 * Prints out the summary data (helper function, you may need to adjust variables accordingly)
 * process_list The original processes inputted in array form
 */
// you guessed it, also made by the teacher.
void printSummaryData(_process process_list[])
{
    uint32_t i = 0;
    double total_amount_of_time_utilizing_cpu = 0.0;
    double total_amount_of_time_io_blocked = 0.0;
    double total_amount_of_time_spent_waiting = 0.0;
    double total_turnaround_time = 0.0;
    uint32_t final_finishing_time = CURRENT_CYCLE;
    for (; i < TOTAL_CREATED_PROCESSES; ++i)
    {
        total_amount_of_time_utilizing_cpu += process_list[i].currentCPUTimeRun;
        total_amount_of_time_io_blocked += process_list[i].currentIOBlockedTime;
        total_amount_of_time_spent_waiting += process_list[i].currentWaitingTime;
        total_turnaround_time += (process_list[i].finishingTime - process_list[i].A);
    }

    // Calculates the CPU utilisation
    double cpu_util = total_amount_of_time_utilizing_cpu / final_finishing_time;

    // Calculates the IO utilisation
    double io_util = (double) TOTAL_NUMBER_OF_CYCLES_SPENT_BLOCKED / final_finishing_time;

    // Calculates the throughput (Number of processes over the final finishing time times 100)
    double throughput =  100 * ((double) TOTAL_CREATED_PROCESSES/ final_finishing_time);

    // Calculates the average turnaround time
    double avg_turnaround_time = total_turnaround_time / TOTAL_CREATED_PROCESSES;

    // Calculates the average waiting time
    double avg_waiting_time = total_amount_of_time_spent_waiting / TOTAL_CREATED_PROCESSES;

    printf("Summary Data:\n");
    printf("\tFinishing time: %i\n", CURRENT_CYCLE);
    printf("\tCPU Utilisation: %6f\n", cpu_util);
    printf("\tI/O Utilisation: %6f\n", io_util);
    printf("\tThroughput: %6f processes per hundred cycles\n", throughput);
    printf("\tAverage turnaround time: %6f\n", avg_turnaround_time);
    printf("\tAverage waiting time: %6f\n", avg_waiting_time);
} // End of the print summary data function





/**
 * The magic starts from here
 */
// main function where "magic" happens
int main(int argc, char *argv[])
{
    if (argc < 2) { // require input for the file path
        fprintf(stderr, "Usage: %s <input_file>\n", argv[0]);
        return 1;
    }

    int dorr = 0;
    int thejsf = 0;

    uint32_t Qslice = 2;

    if (argc >= 3 && strcmp(argv[2], "rr") == 0) { // pick which scheduler to run, with the default being fcfs
        dorr = 1;
        if (argc >= 4) {
            int q = atoi(argv[3]);
            if (q > 0) {
                Qslice = (uint32_t)q;
            }
        }
    }
    else if (argc >= 3 && strcmp(argv[2], "sjf") == 0) {
        thejsf = 1;
    }

    // reads all the processes from the input file keeping nProcesses for printing.
    _process* P = NULL;
    uint32_t nProcesses = 0;
    if (readinp(argv[1], &P, &nProcesses) != 0) {
        fprintf(stderr, "Failed to parse input\n");
        return 1;
    }
    TOTAL_CREATED_PROCESSES = nProcesses;

    printStart(P);
    // print in origional order

    _process* sorted = (_process*) malloc(sizeof(_process) *nProcesses);
    memcpy(sorted, P, nProcesses * sizeof(_process));

    for (uint32_t i = 0; i < nProcesses; ++i) {
        for (uint32_t j = i+1; j < nProcesses; ++j) {
            if (sorted[j].A < sorted[i].A || sorted[j].A == sorted[i].A && sorted[j].processID < sorted[i].processID) {
                _process temp = sorted[i];
                sorted[i] = sorted[j];
                sorted[j] = temp;
            }
        }
    }

    uint32_t save = TOTAL_CREATED_PROCESSES;
    TOTAL_FINISHED_PROCESSES = nProcesses;
    printFinal(sorted);
    TOTAL_FINISHED_PROCESSES = save;
    free(sorted);
    // this makes a sorted copy of, prints the sorted copy, and uses nProcesses to loop for the prints.


    // OPEN da file for rnd numbers, abort if no file
    FILE* rfile = fopen("random-numbers", "r");
    if (!rfile) {
        perror("Failed to open random numbers file");
        free(P);
        return 1;
    }
    // run schedule selector
    _process* run = (_process*) malloc(sizeof(_process) * nProcesses);
    if (!run) { // copy stuff to preserve the array P for the printing.
        perror("malloc didnt work");
        fclose(rfile);
        free(P);
        return 1;
    }
    memcpy(run, P, nProcesses * sizeof(_process));

    if (dorr) {
        do_RR(run, nProcesses, rfile, Qslice);
    }
    else if (thejsf) {
        dosjf(run, nProcesses, rfile);
    }
    else{
        do_fcfs(run, nProcesses,rfile);

    }
    // dispatch stuff to its scheduler


    // actually do the printing, close the files, free my boy, and return 0 to exit.
    printProcessSpecifics(run);
    printSummaryData(run);
    free(run);
    fclose(rfile);
    free(P);
    return 0;


    // Write code for your shiny scheduler


}