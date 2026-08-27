#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/select.h>
#include <sys/stat.h>
#include <errno.h>
#include <libgen.h>
#include <time.h>
#include <stdarg.h>

#include "os.h"
#include "compiler.h"
#include "processor.h"
#include "memory.h"

#define MAX_TASKS 64
#define TIME_SLICE 10

typedef struct Process {
    int pid;
    int proc_id;        // -1 = waiting/ready
    int finished;
    char prog_file[256];
    char data_file[256];
    char base_name[256];
} Process;

static Process tasks[MAX_TASKS];
static int num_tasks = 0;
static int next_pid = 1;
static int proc_busy[NP] = {0};
static int shell_exit = 0;
static FILE *os_log_fp = NULL;

static void ensure_dir(const char *dir) {
    if (mkdir(dir, 0777) == -1 && errno != EEXIST) {
        perror("mkdir");
        exit(EXIT_FAILURE);
    }
}

static void log_event(const char *fmt, ...) {
    if (!os_log_fp) return;
    time_t now = time(NULL);
    char timestamp[64];
    strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", localtime(&now));
    fprintf(os_log_fp, "[%s] ", timestamp);
    va_list args;
    va_start(args, fmt);
    vfprintf(os_log_fp, fmt, args);
    va_end(args);
    fprintf(os_log_fp, "\n");
    fflush(os_log_fp);
}

static void get_base_name(const char *prog_name, char *base, size_t base_len) {
    char temp[256];
    strncpy(temp, prog_name, sizeof(temp) - 1);
    temp[sizeof(temp) - 1] = '\0';

    char *bname = basename(temp);
    strncpy(base, bname, base_len - 1);
    base[base_len - 1] = '\0';

    char *dot = strrchr(base, '.');
    if (dot && dot != base) {
        *dot = '\0';
    }
}

static void create_zero_data_file(const char *data_path) {
    FILE *fp = fopen(data_path, "w");
    if (!fp) {
        perror("fopen create zero data");
        exit(EXIT_FAILURE);
    }
    unsigned char zero[4] = {0, 0, 0, 0};
    fwrite(zero, 1, 4, fp);
    fclose(fp);
}

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
        initialize(proc, tasks[i].prog_file, tasks[i].data_file);
        reset(proc);
        tasks[i].proc_id = proc;
        proc_busy[proc] = 1;
        log_event("PID %d moved from Ready to Running on processor %d",
                  tasks[i].pid, proc);
    }
}

static void loader(const char *prog_name, const char *data_name) {
    char base[256];
    get_base_name(prog_name, base, sizeof(base));

    char compiled_name[512];
    snprintf(compiled_name, sizeof(compiled_name), "%s.byte", base);
    compile(prog_name, compiled_name);

    ensure_dir("data");
    ensure_dir("logs");

    char data_path[512];
    int use_default = (data_name == NULL || strlen(data_name) == 0);

    if (use_default) {
        char candidate1[512], candidate2[512];
        snprintf(candidate1, sizeof(candidate1), "data/%s.data", base);
        snprintf(candidate2, sizeof(candidate2), "data/%s.byte", base);

        FILE *check = fopen(candidate1, "r");
        if (check) {
            fclose(check);
            strncpy(data_path, candidate1, sizeof(data_path) - 1);
            data_path[sizeof(data_path) - 1] = '\0';
        } else {
            check = fopen(candidate2, "r");
            if (check) {
                fclose(check);
                strncpy(data_path, candidate2, sizeof(data_path) - 1);
                data_path[sizeof(data_path) - 1] = '\0';
            } else {
                strncpy(data_path, candidate1, sizeof(data_path) - 1);
                data_path[sizeof(data_path) - 1] = '\0';
                create_zero_data_file(data_path);
            }
        }
    } else {
        if (strchr(data_name, '/') == NULL && strchr(data_name, '\\') == NULL) {
            snprintf(data_path, sizeof(data_path), "data/%s", data_name);
        } else {
            strncpy(data_path, data_name, sizeof(data_path) - 1);
            data_path[sizeof(data_path) - 1] = '\0';
        }
        FILE *check = fopen(data_path, "r");
        if (!check) {
            create_zero_data_file(data_path);
        } else {
            fclose(check);
        }
    }

    int proc = find_free_processor();
    if (proc != -1 && num_tasks < MAX_TASKS) {
        initialize(proc, compiled_name, data_path);
        reset(proc);
        tasks[num_tasks].pid = next_pid++;
        tasks[num_tasks].proc_id = proc;
        tasks[num_tasks].finished = 0;
        strcpy(tasks[num_tasks].prog_file, compiled_name);
        strcpy(tasks[num_tasks].data_file, data_path);
        strcpy(tasks[num_tasks].base_name, base);
        num_tasks++;
        proc_busy[proc] = 1;
        log_event("PID %d created, program %s, assigned to processor %d (state: Running)",
                  tasks[num_tasks-1].pid, base, proc);
        printf("Loaded PID %d on processor %d (program: %s, data: %s)\n",
               tasks[num_tasks-1].pid, proc, compiled_name, data_path);
    } else if (num_tasks < MAX_TASKS) {
        tasks[num_tasks].pid = next_pid++;
        tasks[num_tasks].proc_id = -1;
        tasks[num_tasks].finished = 0;
        strcpy(tasks[num_tasks].prog_file, compiled_name);
        strcpy(tasks[num_tasks].data_file, data_path);
        strcpy(tasks[num_tasks].base_name, base);
        num_tasks++;
        log_event("PID %d created, program %s, placed in Ready queue (state: Ready)",
                  tasks[num_tasks-1].pid, base);
        printf("No free processor, PID %d queued\n", tasks[num_tasks-1].pid);
    }
}

static void display_status(void) {
    time_t now = time(NULL);
    char timestamp[64];
    strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", localtime(&now));

    printf("\n===== Process Control Block (at %s) =====\n", timestamp);
    printf("%-5s %-20s %-12s %-10s\n", "PID", "Program", "Processor", "Status");
    printf("-----------------------------------------------\n");

    for (int i = 0; i < num_tasks; i++) {
        const char *status;
        char proc_str[16];

        if (tasks[i].finished) {
            status = "Finished";
            strcpy(proc_str, "-");
        } else if (tasks[i].proc_id != -1) {
            status = "Running";
            snprintf(proc_str, sizeof(proc_str), "P%d", tasks[i].proc_id);
        } else {
            status = "Ready";
            strcpy(proc_str, "-");
        }

        printf("%-5d %-20s %-12s %-10s\n",
               tasks[i].pid,
               tasks[i].base_name,
               proc_str,
               status);
    }
    printf("=================================\n\n");
}

static void shell(void) {
    static char input[512];
    static int len = 0;
    static int prompt_shown = 0;

    if (!prompt_shown) {
        printf("$ ");
        fflush(stdout);
        prompt_shown = 1;
    }

    fd_set readfds;
    FD_ZERO(&readfds);
    FD_SET(STDIN_FILENO, &readfds);
    struct timeval tv;
    tv.tv_sec = 0;
    tv.tv_usec = 0;

    if (select(STDIN_FILENO + 1, &readfds, NULL, NULL, &tv) > 0) {
        char ch;
        while (read(STDIN_FILENO, &ch, 1) > 0 && ch != '\n') {
            if (len < (int)sizeof(input) - 1)
                input[len++] = ch;
        }
        if (ch == '\n') {
            input[len] = '\0';
            len = 0;
            prompt_shown = 0;

            if (strcmp(input, "exit") == 0) {
                shell_exit = 1;
            } else if (strcmp(input, "ps") == 0 || strcmp(input, "status") == 0) {
                display_status();
            } else if (strcmp(input, "help") == 0) {
                printf("Available commands:\n");
                printf("  <program> [data]   - load and run a program\n");
                printf("  ps                 - show process status\n");
                printf("  status             - same as ps\n");
                printf("  exit               - terminate simulator\n");
            } else {
                char *space = strchr(input, ' ');
                if (space) {
                    *space = '\0';
                    char *prog = input;
                    char *data = space + 1;
                    loader(prog, data);
                } else {
                    loader(input, NULL);
                }
            }
        }
    }
}

void os_run(int argc, char** argv) {
    ensure_dir("logs");
    ensure_dir("data");

    os_log_fp = fopen("logs/os.log", "a");
    if (!os_log_fp) {
        perror("fopen os.log");
        exit(EXIT_FAILURE);
    }
    log_event("Simulator started");

    if (argc >= 2) {
        const char *prog = argv[1];
        const char *data = (argc >= 3) ? argv[2] : NULL;
        loader(prog, data);
    }

    while (!shell_exit) {
        for (int i = 0; i < num_tasks; i++) {
            if (tasks[i].finished || tasks[i].proc_id == -1)
                continue;
            int proc = tasks[i].proc_id;
            process_instructions(proc, tasks[i].pid, TIME_SLICE);
            if (EndSim[proc]) {
                char outname[512];
                snprintf(outname, sizeof(outname), "data/%s_out.byte", tasks[i].base_name);
                finalize(proc, outname);
                proc_busy[proc] = 0;
                tasks[i].finished = 1;
                tasks[i].proc_id = -1;
                log_event("PID %d finished, data written to %s", tasks[i].pid, outname);
                printf("PID %d finished, data written to %s\n", tasks[i].pid, outname);
            }
        }

        assign_waiting();

        shell();

        usleep(10000);
    }

    log_event("Simulator exiting");
    fclose(os_log_fp);
}