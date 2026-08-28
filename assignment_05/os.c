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
#define DEBUG_SCHEDULER 1   // set to 0 to disable debug prints

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

// Paging structures
static char pageTable[NP][NUM_LOGICAL_PAGES];       // physical frame number for each logical page
static char freePages[NUM_PHYSICAL_PAGES];          // 0 free, 1 allocated

static void init_free_pages() {
    for (int i = 0; i < NUM_PHYSICAL_PAGES; i++)
        freePages[i] = 0;
    freePages[0] = 1;   // frame 0 reserved
}

static int getFreePage() {
    for (int i = 1; i < NUM_PHYSICAL_PAGES; i++) {
        if (freePages[i] == 0) {
            freePages[i] = 1;
            return i;
        }
    }
    fprintf(stderr, "ERROR: No free physical pages available!\n");
    exit(EXIT_FAILURE);
}

int getPhysicallAddress(int proc_id, int isFetch, int address) {
    int page_num, offset;
    if (isFetch) {
        if (address >= 1024) {
            fprintf(stderr, "Instruction address out of range: %d\n", address);
            exit(EXIT_FAILURE);
        }
        page_num = address / PAGESIZE;
    } else {
        if (address >= 4096) {
            fprintf(stderr, "Data address out of range: %d\n", address);
            exit(EXIT_FAILURE);
        }
        page_num = address / PAGESIZE + 1024/PAGESIZE;
    }
    offset = address % PAGESIZE;
    int frame = pageTable[proc_id][page_num];
    if (frame == -1) {
        fprintf(stderr, "Page fault: process %d, isFetch=%d, address=%d\n", proc_id, isFetch, address);
        exit(EXIT_FAILURE);
    }
    return frame * PAGESIZE + offset;
}

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
    fprintf(fp, "00 00 00 00\n");
    fclose(fp);
}

static int find_free_processor(void) {
    for (int i = 0; i < NP; i++)
        if (!proc_busy[i])
            return i;
    return -1;
}

static void load_process_memory(int proc_id, const char *prog_file, const char *data_file) {
    // Clear page table for this process
    for (int i = 0; i < NUM_LOGICAL_PAGES; i++)
        pageTable[proc_id][i] = -1;   // invalid

    // Load program
    FILE *pf = fopen(prog_file, "r");
    if (!pf) {
        perror("fopen program file");
        exit(EXIT_FAILURE);
    }

    int logical_addr = 0;
    int byte_count = 0;
    int bytes[4];

#if DEBUG_SCHEDULER
    printf("Loading program %s into processor %d:\n", prog_file, proc_id);
#endif

    while (logical_addr < 1024) {
        if (fscanf(pf, "%x", &bytes[byte_count]) != 1)
            break;

        if (logical_addr % PAGESIZE == 0) {
            int frame = getFreePage();
            int page_idx = logical_addr / PAGESIZE;   // 0 or 1
            pageTable[proc_id][page_idx] = frame;
        }

        int phys = getPhysicallAddress(proc_id, 1, logical_addr);
        write_byte(phys, (char)bytes[byte_count]);

        logical_addr++;
        byte_count++;

        if (byte_count == 4) {
#if DEBUG_SCHEDULER
            int start = logical_addr - 4;
            printf("byte file : %s byte code : %02X %02X %02X %02X\n",
                   prog_file,
                   (unsigned char)memory[getPhysicallAddress(proc_id, 1, start)],
                   (unsigned char)memory[getPhysicallAddress(proc_id, 1, start+1)],
                   (unsigned char)memory[getPhysicallAddress(proc_id, 1, start+2)],
                   (unsigned char)memory[getPhysicallAddress(proc_id, 1, start+3)]);
#endif
            byte_count = 0;
        }
    }
    fclose(pf);

    // Load data
    FILE *df = fopen(data_file, "r");
    if (df) {
        logical_addr = 0;
        while (fscanf(df, "%x", &bytes[0]) == 1 && logical_addr < 4096) {
            if (logical_addr % PAGESIZE == 0) {
                int frame = getFreePage();
                int page_idx = logical_addr / PAGESIZE + 1024/PAGESIZE;  // 2..9
                pageTable[proc_id][page_idx] = frame;
            }
            int phys = getPhysicallAddress(proc_id, 0, logical_addr);
            write_byte(phys, (char)bytes[0]);
            logical_addr++;
        }
        fclose(df);
    }
}

static void unload_process_memory(int proc_id, const char *data_out_file) {
    // Write data memory to file
    FILE *out = fopen(data_out_file, "w");
    if (!out) {
        perror("fopen data output");
        exit(EXIT_FAILURE);
    }
    for (int logical_addr = 0; logical_addr < 4096; logical_addr++) {
        int phys = getPhysicallAddress(proc_id, 0, logical_addr);
        unsigned char byte = (unsigned char)read_byte(phys);
        fprintf(out, "%02X", byte);
        if ((logical_addr + 1) % 4 == 0)
            fprintf(out, "\n");
        else
            fprintf(out, " ");
    }
    fclose(out);

    // Free all frames allocated to this process
    for (int i = 0; i < NUM_LOGICAL_PAGES; i++) {
        int frame = pageTable[proc_id][i];
        if (frame != -1) {
            freePages[frame] = 0;
            pageTable[proc_id][i] = -1;
        }
    }
}

static void assign_waiting(void) {
    for (int i = 0; i < num_tasks; i++) {
        if (tasks[i].finished || tasks[i].proc_id != -1)
            continue;
        int proc = find_free_processor();
        if (proc == -1)
            break;
        load_process_memory(proc, tasks[i].prog_file, tasks[i].data_file);
        reset(proc);
        tasks[i].proc_id = proc;
        proc_busy[proc] = 1;
        log_event("PID %d moved from Ready to Running on processor %d",
                  tasks[i].pid, proc);
#if DEBUG_SCHEDULER
        printf("[Scheduler] PID %d assigned to processor %d (program: %s)\n",
               tasks[i].pid, proc, tasks[i].base_name);
#endif
    }
}

static void ensure_data_dir(const char *data_path) {
    char temp[512];
    strncpy(temp, data_path, sizeof(temp) - 1);
    temp[sizeof(temp) - 1] = '\0';
    char *dir = dirname(temp);
    if (strcmp(dir, ".") != 0) {
        ensure_dir(dir);
    }
}

static void display_bytecode_file(const char *filename) {
    FILE *fp = fopen(filename, "r");
    if (!fp) {
        printf("Cannot open file: %s\n", filename);
        return;
    }
    int a, b, c, d;
    char line[256];
    while (fgets(line, sizeof(line), fp)) {
        if (sscanf(line, "%x %x %x %x", &a, &b, &c, &d) == 4) {
            printf("byte file : %s byte code : %02X %02X %02X %02X\n",
                   filename, a & 0xFF, b & 0xFF, c & 0xFF, d & 0xFF);
        }
    }
    fclose(fp);
}

static void loader(const char *prog_name, const char *data_name) {
    ensure_dir("bytecode");
    char base[256];
    get_base_name(prog_name, base, sizeof(base));

    char compiled_name[512];
    snprintf(compiled_name, sizeof(compiled_name), "bytecode/%s.byte", base);
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
        ensure_data_dir(data_path);
        FILE *check = fopen(data_path, "r");
        if (!check) {
            create_zero_data_file(data_path);
        } else {
            fclose(check);
        }
    }

    int proc = find_free_processor();
    if (num_tasks >= MAX_TASKS) {
        fprintf(stderr, "Error: maximum number of tasks (%d) reached. Cannot load %s\n", MAX_TASKS, base);
        return;
    }
    if (proc != -1) {
        load_process_memory(proc, compiled_name, data_path);
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
    } else {
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
                printf("  show <file>        - display contents of a bytecode file\n");
                printf("  exit               - terminate simulator\n");
            } else if (strncmp(input, "show ", 5) == 0) {
                char *fname = input + 5;
                while (*fname == ' ') fname++;
                if (*fname == '\0') {
                    printf("Usage: show <bytecode_file>\n");
                } else {
                    display_bytecode_file(fname);
                }
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
    ensure_dir("bytecode");

    init_free_pages();

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

#if DEBUG_SCHEDULER
            printf("[Scheduler] Running PID %d on processor %d: fetching from %s\n",
                   tasks[i].pid, proc, tasks[i].prog_file);
#endif

            process_instructions(proc, tasks[i].pid, TIME_SLICE);

            if (EndSim[proc]) {
                char outname[512];
                snprintf(outname, sizeof(outname), "data/%s_out.byte", tasks[i].base_name);
                unload_process_memory(proc, outname);
                close_log(proc);
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

    for (int i = 0; i < NP; i++) {
        close_log(i);
    }

    log_event("Simulator exiting");
    fclose(os_log_fp);
}