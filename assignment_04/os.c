#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/select.h>
#include "os.h"
#include "compiler.h"
#include "processor.h"
#include "memory.h"

#define MAX_TASKS 64
#define TIME_SLICE 10

typedef struct Process {
    int pid;
    int proc_id;        // -1 = waiting
    int finished;
    char prog_file[256];
    char data_file[256];
} Process;

static Process tasks[MAX_TASKS];
static int num_tasks = 0;
static int next_pid = 1;
static int proc_busy[NP] = {0};
static int shell_exit = 0;

static int find_free_processor(void) {
    for (int i = 0; i < NP; i++)
        if (!proc_busy[i])
            return i;
    return -1;
}

static void assign_waiting(void) {
    for (int i = 0; i < num_tasks; i++) {
        if (tasks[i].finished || tasks[i].proc_id != -1)
            continue;
        int proc = find_free_processor();
        if (proc == -1)
            break;
        initialize(proc, "program.byte", tasks[i].data_file);
        reset(proc);
        tasks[i].proc_id = proc;
        proc_busy[proc] = 1;
    }
}

static void loader(const char *prog_name, const char *data_name) {
    compile(prog_name, "program.byte");

    int proc = find_free_processor();
    if (proc != -1 && num_tasks < MAX_TASKS) {
        initialize(proc, "program.byte", data_name);
        reset(proc);
        tasks[num_tasks].pid = next_pid++;
        tasks[num_tasks].proc_id = proc;
        tasks[num_tasks].finished = 0;
        strcpy(tasks[num_tasks].prog_file, prog_name);
        strcpy(tasks[num_tasks].data_file, data_name);
        num_tasks++;
        proc_busy[proc] = 1;
        printf("Loaded PID %d on processor %d\n", tasks[num_tasks-1].pid, proc);
    } else if (num_tasks < MAX_TASKS) {
        tasks[num_tasks].pid = next_pid++;
        tasks[num_tasks].proc_id = -1;
        tasks[num_tasks].finished = 0;
        strcpy(tasks[num_tasks].prog_file, prog_name);
        strcpy(tasks[num_tasks].data_file, data_name);
        num_tasks++;
        printf("No free processor, PID %d queued\n", tasks[num_tasks-1].pid);
    }
}

static void shell(void) {
    static char input[512];
    static int len = 0;

    fd_set readfds;
    FD_ZERO(&readfds);
    FD_SET(STDIN_FILENO, &readfds);
    struct timeval tv;
    tv.tv_sec = 0;
    tv.tv_usec = 0;

    printf("$ ");
    fflush(stdout);

    if (select(STDIN_FILENO + 1, &readfds, NULL, NULL, &tv) > 0) {
        char ch;
        while (read(STDIN_FILENO, &ch, 1) > 0 && ch != '\n') {
            if (len < (int)sizeof(input) - 1)
                input[len++] = ch;
        }
        if (ch == '\n') {
            input[len] = '\0';
            len = 0;

            if (strcmp(input, "exit") == 0) {
                shell_exit = 1;
            } else {
                char *space = strchr(input, ' ');
                if (space) {
                    *space = '\0';
                    char *prog = input;
                    char *data = space + 1;
                    loader(prog, data);
                } else {
                    printf("Usage: <program.txt> <data.byte>\n");
                }
            }
        }
    }
}

void os_run(void) {
    while (1) {
        // Run each active process
        for (int i = 0; i < num_tasks; i++) {
            if (tasks[i].finished || tasks[i].proc_id == -1)
                continue;
            int proc = tasks[i].proc_id;
            process_instructions(proc, tasks[i].pid, TIME_SLICE);
            if (EndSim[proc]) {
                char outname[256];
                snprintf(outname, sizeof(outname), "data_out_%d.byte", tasks[i].pid);
                finalize(proc, outname);
                proc_busy[proc] = 0;
                tasks[i].finished = 1;
                tasks[i].proc_id = -1;
                printf("PID %d finished, data written to %s\n", tasks[i].pid, outname);
            }
        }

        assign_waiting();

        if (!shell_exit)
            shell();

        int active = 0;
        for (int i = 0; i < num_tasks; i++)
            if (!tasks[i].finished && tasks[i].proc_id != -1)
                active++;

        if (shell_exit && active == 0)
            break;
    }
}