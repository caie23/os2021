#include "sut.h"
#include "queue.h"
#include <unistd.h>
#include <pthread.h>
#include <ucontext.h>
#include <stdint.h>
#include <time.h>

int num_cexecutor = 1; // set to 1 or 2

struct queue rq; // ready queue (for C-EXECs)
struct queue wq; // wait queue (for I-EXEC)

pthread_t *CEXEC, *CEXEC2, *IEXEC; // executors (kernel level threads)

ucontext_t cexec1context, cexec2context, iexeccontext;

ucontext_t tasks[30]; // context of the tasks
char tstack[30][16 * 1024];
int idx = 0;

// contexts for sut functions
ucontext_t sutyield, sutexit, sutopen, sutread, sutwrite, sutclose;

pthread_mutex_t lck; // mutex lock

void *cexec1();
void *cexec2();
void *iexec();

void sut_init()
{
    // initialize the ready queue
    rq = queue_create();
    queue_init(&rq);

    // initialize the wait queue
    wq = queue_create();
    queue_init(&wq);

    // Allocate memory for threads
    CEXEC = (pthread_t *)malloc(sizeof(pthread_t));
    if (num_cexecutor == 2)
        CEXEC2 = (pthread_t *)malloc(sizeof(pthread_t));
    IEXEC = (pthread_t *)malloc(sizeof(pthread_t));

    // create threads for the executors
    pthread_create(CEXEC, NULL, cexec1, NULL);
    if (num_cexecutor == 2)
        pthread_create(CEXEC2, NULL, cexec2, NULL);
    pthread_create(IEXEC, NULL, iexec, NULL);
}

void *cexec1()
{
    while (true)
    {
        if (queue_peek_front(&rq) != NULL)
        {
            pthread_mutex_lock(&lck);
            struct queue_entry *e = queue_peek_front(&rq);
            swapcontext(&cexec1context, (ucontext_t *)e->data);
            pthread_mutex_unlock(&lck);
        }
        usleep(100);
    }
}

void *cexec2()
{
    while (true)
    {
        if (queue_peek_front(&rq) != NULL)
        {
            pthread_mutex_lock(&lck);
            struct queue_entry *e = queue_peek_front(&rq);
            swapcontext(&cexec1context, (ucontext_t *)e->data);
            pthread_mutex_unlock(&lck);
        }
        usleep(100);
    }
}

void *iexec()
{
    while (true)
    {
        if (queue_peek_front(&wq) != NULL)
        {
            struct queue_entry *e = queue_peek_front(&wq);
            swapcontext(&iexeccontext, (ucontext_t *)e->data);
        }
        usleep(100);
    }
}

bool sut_create(sut_task_f fn)
{
    // create TCB for task fn
    getcontext(&tasks[idx]);
    tasks[idx].uc_stack.ss_sp = tstack[idx];
    tasks[idx].uc_stack.ss_size = sizeof(tstack[idx]);
    tasks[idx].uc_link = &cexec1context;
    makecontext(&tasks[idx], fn, 0);

    // put TCB in rq
    struct queue_entry *entry = queue_new_node(&tasks[idx]); // store TCB as data of q entry
    queue_insert_tail(&rq, entry);

    idx++;

    return true;
}

void sut_yield()
{
    // pop the current running task from the ready queue
    struct queue_entry *e = queue_pop_head(&rq);

    // put it to the end of the queue
    queue_insert_tail(&rq, e);

    swapcontext(&sutyield, &cexec1context);
}

void sut_exit()
{
    // pop the current running task from the ready queue
    struct queue_entry *e = queue_pop_head(&rq);

    // instead of put it back, delete it
    free(e);
        
        swapcontext(&sutexit, &cexec1context);

}

int sut_open(char *dest)
{
    // pop a TCB from rq and link back to sut_open
    struct queue_entry *e = queue_pop_head(&rq);
    ucontext_t *tc = (ucontext_t *)e->data;
    (*tc).uc_link = &sutopen;

    // swap to C-EXEC
    swapcontext(&sutopen, &cexec1context);

    // put sut_open in wq
    struct queue_entry *e1 = queue_new_node(&sutopen);
    queue_insert_tail(&wq, e1);

    // complete IO operation and return
    // open the file
    int fd = (intptr_t)fopen(dest, "ab+");

    // put the task back to ready queue
    struct queue_entry *e2 = queue_new_node(tc);
    queue_insert_tail(&rq, e2);

    return fd;
}

char *sut_read(int fd, char *buf, int size)
{
    struct queue_entry *e = queue_pop_head(&rq);
    ucontext_t *tc = (ucontext_t *)e->data;
    (*tc).uc_link = &sutread;

    swapcontext(&sutread, &cexec1context);

    struct queue_entry *e1 = queue_new_node(&sutread);
    queue_insert_tail(&wq, e1);

    // read from the file
    fread(buf, size, size, (FILE *)(long)fd);

    // put the task back to ready queue
    struct queue_entry *e2 = queue_new_node(tc);
    queue_insert_tail(&rq, e2);

    return buf;
}

void sut_write(int fd, char *buf, int size)
{
    struct queue_entry *e = queue_pop_head(&rq);
    ucontext_t *tc = (ucontext_t *)e->data;
    (*tc).uc_link = &sutwrite;

    swapcontext(&sutwrite, &cexec1context);

    struct queue_entry *e1 = queue_new_node(&sutwrite);
    queue_insert_tail(&wq, e1);

    // write to the file
    fwrite(buf, size, size, (FILE *)(long)fd);

    // put the task back to ready queue
    struct queue_entry *e2 = queue_new_node(tc);
    queue_insert_tail(&rq, e2);
}

void sut_close(int fd)
{
    struct queue_entry *e = queue_pop_head(&rq);
    ucontext_t *tc = (ucontext_t *)e->data;
    (*tc).uc_link = &sutclose;

    swapcontext(&sutclose, &cexec1context);

    struct queue_entry *e1 = queue_new_node(&sutclose);
    queue_insert_tail(&wq, e1);

    // close the file
    fclose((FILE *)(long)fd);

    // put the task back to ready queue
    struct queue_entry *e2 = queue_new_node(tc);
    queue_insert_tail(&rq, e2);
}

void sut_shutdown()
{
    usleep(100);
    while (queue_peek_front(&rq) != NULL)
        ; // shutdown only when the queue is empty

    // join the executors then terminate the program
    pthread_join(*CEXEC, NULL);
    pthread_join(*CEXEC2, NULL);
    pthread_join(*IEXEC, NULL);
    pthread_exit(NULL); // main exits
}
