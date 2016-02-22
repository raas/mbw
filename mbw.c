/*
 * vim: ai ts=4 sts=4 sw=4 cinoptions=>4 expandtab
 */
#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/time.h>
#include <time.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>

/* how many runs to average by default */
#define DEFAULT_NR_LOOPS 10

/* we have 3 tests at the moment */
#define MAX_TESTS 4 

/* default block size for test 2, in bytes */
#define DEFAULT_BLOCK_SIZE 262144

/* test types */
#define TEST_MEMCPY         0
#define TEST_DUMB           1
#define TEST_MCBLOCK        2
#define TEST_ARCH_MEMCPY    3 

/* version number */
#define VERSION "1.4"

/*
 * MBW memory bandwidth benchmark
 *
 * 2006, 2012 Andras.Horvath@gmail.com
 * 2013 j.m.slocum@gmail.com 
 * (Special thanks to Stephen Pasich)
 *
 * http://github.com/raas/mbw
 *
 * compile with:
 *			gcc -O -o mbw mbw.c
 *
 * run with eg.:
 *
 *			./mbw 300
 *
 * or './mbw -h' for help
 *
 * watch out for swap usage (or turn off swap)
 */



static const char *test_name[MAX_TESTS] = {
    "MEMCPY", "DUMB", "MCBLOCK", "AMEMCPY" };
/* whats tests to run (-t x) */
static int tests[MAX_TESTS];
/* suppress extra messages */
static int quiet = 0;
/* show average */
static int showavg = 1; 
/* how many runs to average? */
static int nr_loops=DEFAULT_NR_LOOPS;
/* MiBytes transferred == array size in MiB */
static double mt = 0;
/* number of threads */
static int mt_num = 1;
/* number of cores */
static int num_cores = 0;
static int core_factor = 1;
/* fixed memcpy block size for -t2 */
unsigned long long block_size=DEFAULT_BLOCK_SIZE;
unsigned long long asize=0; /* array size (elements in array) */

void run_test(int tid);

__attribute__((noinline)) void *arch_memcpy(void *dest, const void *src, size_t n)
{
#ifdef __x86_64__
    register void *ret asm ("rax") = dest;
    asm volatile ("movq     %rdi, %rax\n\t"
                  "movq     %rdx, %rcx\n\t"
                  "shrq     $3, %rcx\n\t"
                  "andl     $7, %edx\n\t"
                  "rep      movsq\n\t"
                  "movl     %edx, %ecx\n\t"
                  "rep      movsb\n\t"
            );
    return ret;
#else
    asm volatile ("movl     %0, %%edi\n\t"
                  "movl     %1, %%esi\n\t"
                  "movl     %2, %%edx\n\t"
                  "movl     %%edx, %%ecx\n\t"
                  "shrl     $2, %%ecx\n\t"
                  "andl     $3, %%edx\n\t"
                  "rep      movsl    \n\t"
                  "movl     %%edx, %%ecx\n\t"
                  "rep      movsb    \n\t"
                  :
                  : "r"(dest), "r"(src), "r"(n)
                  : "edi", "esi", "ecx", "edx", "memory");
    return dest;
#endif
}

#define CONFIG_MT_MAX_THREADS   4

double *data[CONFIG_MT_MAX_THREADS][MAX_TESTS];
static pthread_t            mt_threads[CONFIG_MT_MAX_THREADS];
static pthread_barrier_t    mt_barrier;
static double               mt_ave[CONFIG_MT_MAX_THREADS][MAX_TESTS];

static void mt_bar(void)
{
    int ret = 0;
    if (mt_num == 1) return;
    ret = pthread_barrier_wait(&mt_barrier);
    if (ret != 0 && ret != PTHREAD_BARRIER_SERIAL_THREAD) {
        printf("barrier error %d\n", ret);
        exit(1);
    }
}

void *  mt_worker(void *arg)
{
    int tid = (arg - (void *)mt_threads) / sizeof(pthread_t);
    int ret = 0;
    cpu_set_t cs;
    CPU_ZERO(&cs);
    CPU_SET(tid * core_factor, &cs);
    ret = pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cs);
    if (ret) {
        printf("Binding thread %d to core %d failed\n", tid, tid * core_factor);
        exit(1);
    }
    printf("Bound thread %d to core %d\n", tid, tid * core_factor);
    mt_bar();
    run_test(tid);
    mt_bar();

    return 0;
}

/* collect data for per thread per run */
static void mt_collect_data(int tid, int run, int type, double te)
{
    data[tid][type][run] = te; 
}

static int mt_init(void)
{
    int ret = 0;
    int i = 0;
    int j = 0;

    printf("Number of Threads %d\n", mt_num);
    ret = pthread_barrier_init(&mt_barrier, 0, mt_num);
    if (ret) { printf("init barrier error %d\n", ret); return ret; }

    cpu_set_t cs;
    CPU_ZERO(&cs);
    sched_getaffinity(0, sizeof(cs), &cs);

    /* assuming 8 cores max */
    for (i = 0; i < 8; i++)  {
        if (CPU_ISSET(i, &cs)) {
            printf("Core %d\n", i);
            num_cores++;
        }
    }

    printf("We have %d cores \n", num_cores);
    
    if (num_cores < mt_num) {
        printf("The number of available cores %d is less than required threads %d\n", num_cores, mt_num);
        exit(1);
    }

    for (i = 0; i < mt_num; i++) {
        ret = pthread_create(&mt_threads[i], 0, mt_worker, &mt_threads[i]);
        if (ret) return ret;
        for (j = 0; j < MAX_TESTS; j++) {
            data[i][j] = (double *)malloc(sizeof(double) * nr_loops);
            if (data[i][j] == 0) {
                printf("alloc data error\n");
                return 1;
            }
            bzero(data[i][j], sizeof(double) * nr_loops);
        }
    }

    return 0; 
}

void printout(double te, double mt, int type);
static void mt_wait(void)
{
    int i = 0;
    int j = 0;
    int k = 0;
    double sum = 0;

    for (i = 0; i < mt_num; i++) {
        pthread_join(mt_threads[i], 0);
    }

    for (i = 0; i < mt_num; i++) {
        printf("\n-- result for thread %d --\n", i);
        for (j = 0; j < MAX_TESTS; j++) {
            sum = 0;
            for (k = 0; k < nr_loops; k++) {
                if (data[i][j][k] != 0) {
                    printf("%d\t", k);
                    printout(data[i][j][k], mt, j);
                    sum += data[i][j][k];
                }
            }

            if (showavg && sum != 0) {
                printf("AVG\t");
                printout(sum / nr_loops, mt, j);
                mt_ave[i][j] = (mt * 8 * 1024 * 1024) / 1000 / 1000 / (sum / nr_loops); 
            }

        }

        printf("-- end of result for thread %d -- \n", i);
    }
}

void usage()
{
    printf("mbw memory benchmark v%s, https://github.com/raas/mbw\n", VERSION);
    printf("Usage: mbw [options] array_size_in_MiB\n");
    printf("Options:\n");
    printf("	-n: number of runs per test\n");
    printf("	-a: Don't display average\n");
    printf("	-t%d: memcpy test\n", TEST_MEMCPY);
    printf("	-t%d: dumb (b[i]=a[i] style) test\n", TEST_DUMB);
    printf("	-t%d: memcpy test with fixed block size\n", TEST_MCBLOCK);
    printf("	-b <size>: block size in bytes for -t2 (default: %d)\n", DEFAULT_BLOCK_SIZE);
    printf("\t-T <num_threads>: running the benchmark concurrently in multiple threads\n");
    printf("	-q: quiet (print statistics only)\n");
    printf("(will then use two arrays, watch out for swapping)\n");
    printf("'Bandwidth' is amount of data copied over the time this operation took.\n");
    printf("\nThe default is to run all tests available.\n");
}

/* ------------------------------------------------------ */

/* allocate a test array and fill it with data
 * so as to force Linux to _really_ allocate it */
long *make_array(unsigned long long asize)
{
    unsigned long long t;
    unsigned int long_size=sizeof(long);
    long *a;

    a=calloc(asize, long_size);

    if(NULL==a) {
        perror("Error allocating memory");
        exit(1);
    }

    /* make sure both arrays are allocated, fill with pattern */
    for(t=0; t<asize; t++) {
        a[t]=0xaa;
    }
    return a;
}

/* actual benchmark */
/* asize: number of type 'long' elements in test arrays
 * long_size: sizeof(long) cached
 * type: 0=use memcpy, 1=use dumb copy loop (whatever GCC thinks best)
 *
 * return value: elapsed time in seconds
 */
double worker(unsigned long long asize, long *a, long *b, int type, unsigned long long block_size)
{
    unsigned long long t;
    struct timeval starttime, endtime;
    double te;
    unsigned int long_size=sizeof(long);
    /* array size in bytes */
    unsigned long long array_bytes=asize*long_size;

    if(type==TEST_MEMCPY) { /* memcpy test */
        /* timer starts */
        mt_bar();

        gettimeofday(&starttime, NULL);
        memcpy(b, a, array_bytes);
        /* timer stops */
        gettimeofday(&endtime, NULL);

        mt_bar();
    } else if(type==TEST_MCBLOCK) { /* memcpy block test */
        char* aa = (char*)a;
        char* bb = (char*)b;
        mt_bar();

        gettimeofday(&starttime, NULL);
        for (t=array_bytes; t >= block_size; t-=block_size, aa+=block_size){
            bb=mempcpy(bb, aa, block_size);
        }
        if(t) {
            bb=mempcpy(bb, aa, t);
        }
        gettimeofday(&endtime, NULL);

        mt_bar();
    } else if(type==TEST_DUMB) { /* dumb test */
        mt_bar();
        
        gettimeofday(&starttime, NULL);
        for(t=0; t<asize; t++) {
            b[t]=a[t];
        }
        gettimeofday(&endtime, NULL);

        mt_bar();

    } else if (type == TEST_ARCH_MEMCPY) {
        mt_bar();
        gettimeofday(&starttime, NULL);
        arch_memcpy(b, a, array_bytes);
        gettimeofday(&endtime, NULL);
        mt_bar();
    }

    te=((double)(endtime.tv_sec*1000000-starttime.tv_sec*1000000+endtime.tv_usec-starttime.tv_usec))/1000000;

    return te;
}

/* ------------------------------------------------------ */

/* pretty print worker's output in human-readable terms */
/* te: elapsed time in seconds
 * mt: amount of transferred data in MiB
 * type: see 'worker' above
 *
 * return value: -
 */
void printout(double te, double mt, int type)
{
    switch(type) {
        case TEST_MEMCPY:
            printf("Method: MEMCPY\t");
            break;
        case TEST_DUMB:
            printf("Method: DUMB\t");
            break;
        case TEST_MCBLOCK:
            printf("Method: MCBLOCK\t");
            break;
        case TEST_ARCH_MEMCPY:
            printf("Method: AMEMCPY\t");
            break;
    }
    printf("Elapsed: %.5f\t", te);
    printf("MiB: %.5f\t", mt);
    printf("Copy: %.3f MiB/s", mt/te);
    printf("  %.3f Mb/s\n", (mt * 8 * 1024 * 1024) / 1000 / 1000 / te);
    return;
}


void run_test(int tid)
{
    long *a = make_array(asize);
    long *b = make_array(asize);

    unsigned long testno = 0;
    double te; 
    int i = 0;

    for (testno = 0; testno < MAX_TESTS; testno++) {
        if (tests[testno]) {
            for (i = 0; i < nr_loops; i++) {
                te = worker(asize, a, b, testno, block_size);
                    mt_collect_data(tid, i, testno, te);
            }

        }
    }

    free(a);
    free(b);
    return;
}

/* ------------------------------------------------------ */

int main(int argc, char **argv)
{
    unsigned int long_size=0;
    int i, j;
    int o; /* getopt options */
    unsigned long testno;

    /* options */
   
    for (i = 0; i < MAX_TESTS; i++) {
        tests[i] = 0;
    }

    while((o=getopt(argc, argv, "haqn:t:b:T:")) != EOF) {
        switch(o) {
            case 'h':
                usage();
                exit(1);
                break;
            case 'a': /* suppress printing average */
                showavg=0;
                break;
            case 'n': /* no. loops */
                nr_loops=strtoul(optarg, (char **)NULL, 10);
                break;
            case 't': /* test to run */
                testno=strtoul(optarg, (char **)NULL, 10);
                if(0>testno) {
                    printf("Error: test number must be between 0 and %d\n", MAX_TESTS);
                    exit(1);
                }
                tests[testno]=1;
                break;
            case 'b': /* block size in bytes*/
                block_size=strtoull(optarg, (char **)NULL, 10);
                if(0>=block_size) {
                    printf("Error: what block size do you mean?\n");
                    exit(1);
                }
                break;
            case 'q': /* quiet */
                quiet=1;
                break;
            case 'T': /* multithreaded */
                mt_num=strtoul(optarg, (char **)NULL, 10);
                if (mt_num <= 0 || mt_num > CONFIG_MT_MAX_THREADS) {
                    printf("invalid parater for thread number %d range (1-4) \n", mt_num);
                    exit(1);
                }
                break;
            default:
                break;
        }
    }

    /* default is to run all tests if no specific tests were requested */
    int all_tests = 0;
    for (i = 0; i< MAX_TESTS; i++) {
        all_tests += tests[i]; 
    }
    if (all_tests == 0) {
        for (i = 0; i < MAX_TESTS; i++) {
            tests[i] = 1;
        }
    }

    if(optind<argc) {
        mt=strtoul(argv[optind++], (char **)NULL, 10);
    } else {
        printf("Error: no array size given!\n");
        exit(1);
    }

    if(0>=mt) {
        printf("Error: array size wrong!\n");
        exit(1);
    }

    /* ------------------------------------------------------ */

    long_size=sizeof(long); /* the size of long on this platform */
    asize=1024*1024/long_size*mt; /* how many longs then in one array? */

    if(asize*long_size < block_size) {
        printf("Error: array size larger than block size (%llu bytes)!\n", block_size);
        exit(1);
    }

    if(!quiet) {
        printf("Long uses %d bytes. ", long_size);
        printf("Allocating 2*%lld elements = %lld bytes of memory.\n", asize, 2*asize*long_size);
        if(tests[2]) {
            printf("Using %lld bytes as blocks for memcpy block copy test.\n", block_size);
        }
    }


    if (mt_init()) {
        printf("mt init error\n");
        exit(1);
    }
    mt_wait();
    for (j = 0; j < MAX_TESTS; j++) {      
        double total = 0;
        for (i = 0; i < mt_num; i++) {
            total += mt_ave[i][j];
        }
        if (total != 0) {
            printf("Test %s\t %d Thread(s) AVG Total COPY %f Mb/s\n", test_name[j], mt_num, total);
        }
    }

    return 0;
}

