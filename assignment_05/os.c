#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <sys/types.h>
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
#define DEBUG_SCHEDULER 0
/*
                 Process
                    │
          ┌─────────┼─────────┐
          ↓         ↓         ↓
       Running     Ready    Finished
          │         │         │
    proc_id >= 0  proc_id=-1 finished=1
    finished=0    finished=0 */
typedef struct Process {
    int pid;
    int proc_id;        /* -1 = waiting/ready */
    int finished;
    char prog_file[256];
    char data_file[256];
    char base_name[256];
} Process;

extern char current_prog_name[256];
static Process tasks[MAX_TASKS]; //process table
static int num_tasks = 0;  // how many entries used
static int next_pid = 1;   // PID Counter
static int proc_busy[NP] = {0};  // Per-processor occupancy
static int shell_exit = 0;  //set by "exit" command
static FILE *os_log_fp = NULL; //global log file

static int8_t pageTable[NP][NUM_LOGICAL_PAGES]; //per-process mapping
static char freePages[NUM_PHYSICAL_PAGES];  //physical fram bitmap


/* Makes every physical frame 0 and reserve page 0 for os and pointing to null.
   In case of accessing returning error.*/
static void init_free_pages(void) {
    for (int i = 0; i < NUM_PHYSICAL_PAGES; i++)
        freePages[i] = 0;
    freePages[0] = 1;
}


/* Finding the free page and returning the page index otherwise returning -1.*/
static int getFreePage(void) {
    for (int i = 1; i < NUM_PHYSICAL_PAGES; i++) {
        if (freePages[i] == 0) {
            freePages[i] = 1;
            return i;
        }
    }
    return -1;
}

/*Important function.
  Every instruction fetch and every daa acess goes throgh it.
  proc_id - which processor slot the access belong to
  isFetch - 1 for instruction fetch, 0 for data
  address - logical address (0...1023 for code,0...4095 for data)*/
int getPhysicallAddress(int proc_id, int isFetch, int address) {
    int page_num, offset;

    if (address < 0) {
        fprintf(stderr, "Negative address: %d\n", address);
        exit(EXIT_FAILURE);
    }
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
        page_num = address / PAGESIZE + 1024 / PAGESIZE;
    }
    offset = address % PAGESIZE;
    int8_t frame = pageTable[proc_id][page_num];
    if (frame == -1) {
        fprintf(stderr, "Page fault: process %d, isFetch=%d, address=%d\n",
                proc_id, isFetch, address);
        exit(EXIT_FAILURE);
    }
    return (int)frame * PAGESIZE + offset;
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
/* given path like programs/foo.s, returns the basename without extension: foo.*/
static void get_base_name(const char *prog_name, char *base, size_t base_len) {
    if (base_len == 0) return;
    char temp[256];
    strncpy(temp, prog_name, sizeof(temp) - 1);
    temp[sizeof(temp) - 1] = '\0';

    char *bname = basename(temp);
    strncpy(base, bname, base_len - 1);
    base[base_len - 1] = '\0';

    char *dot = strrchr(base, '.');
    if (dot && dot != base) *dot = '\0';
}
/* Creating empty file while no data file mentioned*/
static void create_zero_data_file(const char *data_path) {
    FILE *fp = fopen(data_path, "w");
    if (!fp) { perror("fopen create zero data"); exit(EXIT_FAILURE); }
    fprintf(fp, "00 00 00 00\n");
    fclose(fp);
}

static int find_free_processor(void) {
    for (int i = 0; i < NP; i++)
        if (!proc_busy[i]) return i;
    return -1;
}

/*called on failed load(to release partial allocations) and on process termination (after the data file has been written out.)*/
static void free_process_pages(int proc_id) {
    for (int i = 0; i < NUM_LOGICAL_PAGES; i++) {
        int frame = pageTable[proc_id][i];
        if (frame != -1) {
            freePages[frame] = 0;
            pageTable[proc_id][i] = -1;
        }
    }
}
/* Compiled bytecode and the data file into physical memory, using the page table for proc_id.
   Return 0 on success and -1 on failure.
   */
static int load_process_memory(int proc_id, const char *prog_file, const char *data_file) {
    for (int i = 0; i < NUM_LOGICAL_PAGES; i++)
        pageTable[proc_id][i] = -1;

    FILE *pf = fopen(prog_file, "r");
    if (!pf) { perror("fopen program file"); return -1; }

    int logical_addr = 0;
    int byte_count = 0;
    int bytes[4];

    while (logical_addr < 1024) {
        if (fscanf(pf, "%x", &bytes[byte_count]) != 1) break;

        if (logical_addr % PAGESIZE == 0) {
            int frame = getFreePage();
            if (frame < 0) {
                fclose(pf);
                free_process_pages(proc_id);
                fprintf(stderr, "Out of physical memory while loading program %s\n", prog_file);
                return -1;
            }
            pageTable[proc_id][logical_addr / PAGESIZE] = (int8_t)frame;
        }
        int phys = getPhysicallAddress(proc_id, 1, logical_addr);
        write_byte(phys, (char)bytes[byte_count]);
        logical_addr++;
        if (++byte_count == 4) byte_count = 0;
    }
    fclose(pf);

    FILE *df = fopen(data_file, "r");
    if (df) {
        logical_addr = 0;
        while (fscanf(df, "%x", &bytes[0]) == 1 && logical_addr < 4096) {
            if (logical_addr % PAGESIZE == 0) {
                int frame = getFreePage();
                if (frame < 0) {
                    fclose(df);
                    free_process_pages(proc_id);
                    fprintf(stderr, "Out of physical memory while loading data %s\n", data_file);
                    return -1;
                }
                pageTable[proc_id][logical_addr / PAGESIZE + 1024 / PAGESIZE] = (int8_t)frame;
            }
            int phys = getPhysicallAddress(proc_id, 0, logical_addr);
            write_byte(phys, (char)bytes[0]);
            logical_addr++;
        }
        fclose(df);
    }
    return 0;
}

static void unload_process_memory(int proc_id, const char *data_out_file) {
    FILE *out = fopen(data_out_file, "w");
    if (!out) { perror("fopen data output"); exit(EXIT_FAILURE); }

    for (int logical_addr = 0; logical_addr < 4096; logical_addr++) {
        int page_idx = logical_addr / PAGESIZE + 1024 / PAGESIZE;
        unsigned char byte;
        if (pageTable[proc_id][page_idx] == -1) {
            byte = 0x00;
        } else {
            int phys = getPhysicallAddress(proc_id, 0, logical_addr);
            byte = (unsigned char)read_byte(phys);
        }
        fprintf(out, "%02X", byte);
        if ((logical_addr + 1) % 4 == 0) fprintf(out, "\n");
        else                              fprintf(out, " ");
    }
    fclose(out);
    free_process_pages(proc_id);
}

static void assign_waiting(void) {
    for (int i = 0; i < num_tasks; i++) {
        if (tasks[i].finished || tasks[i].proc_id != -1) continue;

        int proc = find_free_processor();
        if (proc == -1) break;

        if (load_process_memory(proc, tasks[i].prog_file, tasks[i].data_file) != 0) {
            /* Not enough memory right now; try other ready tasks. */
            continue;
        }
        reset(proc);
        tasks[i].proc_id = proc;
        proc_busy[proc] = 1;
        log_event("PID %d moved from Ready to Running on processor %d",
                  tasks[i].pid, proc);
    }
}

static void ensure_data_dir(const char *data_path) {
    char temp[512];
    strncpy(temp, data_path, sizeof(temp) - 1);
    temp[sizeof(temp) - 1] = '\0';
    char *dir = dirname(temp);
    if (strcmp(dir, ".") != 0) ensure_dir(dir);
}

/* Once compiled .byte file and prints each 4-byte line.*/
static void display_bytecode_file(const char *filename) {
    FILE *fp = fopen(filename, "r");
    if (!fp) { printf("Cannot open file: %s\n", filename); return; }
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
    if (num_tasks >= MAX_TASKS) {
        fprintf(stderr, "Error: maximum number of tasks (%d) reached. Cannot load %s\n",
                MAX_TASKS, prog_name);
        return;
    }

    ensure_dir("bytecode");
    char base[256];
    get_base_name(prog_name, base, sizeof(base));

    char compiled_name[512];
    snprintf(compiled_name, sizeof(compiled_name), "bytecode/%s.byte", base);

    if (compile(prog_name, compiled_name) != 0) {
        fprintf(stderr, "Compilation failed for %s; task not loaded.\n", prog_name);
        return;
    }

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
        if (!check) create_zero_data_file(data_path);
        else        fclose(check);
    }

    int proc = find_free_processor();
    int assigned = -1;
    if (proc != -1) {
        if (load_process_memory(proc, compiled_name, data_path) == 0)
            assigned = proc;
    }

    tasks[num_tasks].pid = next_pid++;
    tasks[num_tasks].finished = 0;
    strncpy(tasks[num_tasks].prog_file, compiled_name, sizeof(tasks[num_tasks].prog_file) - 1);
    tasks[num_tasks].prog_file[sizeof(tasks[num_tasks].prog_file) - 1] = '\0';
    strncpy(tasks[num_tasks].data_file, data_path, sizeof(tasks[num_tasks].data_file) - 1);
    tasks[num_tasks].data_file[sizeof(tasks[num_tasks].data_file) - 1] = '\0';
    strncpy(tasks[num_tasks].base_name, base, sizeof(tasks[num_tasks].base_name) - 1);
    tasks[num_tasks].base_name[sizeof(tasks[num_tasks].base_name) - 1] = '\0';

    if (assigned != -1) {
        reset(assigned);
        tasks[num_tasks].proc_id = assigned;
        proc_busy[assigned] = 1;
        log_event("PID %d created, program %s, assigned to processor %d (state: Running)",
                  tasks[num_tasks].pid, base, assigned);
        printf("Loaded PID %d on processor %d (program: %s, data: %s)\n",
               tasks[num_tasks].pid, assigned, compiled_name, data_path);
    } else {
        tasks[num_tasks].proc_id = -1;
        log_event("PID %d created, program %s, placed in Ready queue (state: Ready)",
                  tasks[num_tasks].pid, base);
        printf("No free processor, PID %d queued\n", tasks[num_tasks].pid);
    }
    num_tasks++;
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
            status = "Finished"; strcpy(proc_str, "-");
        } else if (tasks[i].proc_id != -1) {
            status = "Running";
            snprintf(proc_str, sizeof(proc_str), "P%d", tasks[i].proc_id);
        } else {
            status = "Ready"; strcpy(proc_str, "-");
        }
        printf("%-5d %-20s %-12s %-10s\n",
               tasks[i].pid, tasks[i].base_name, proc_str, status);
    }
    printf("=================================\n\n");
}

static void handle_shell_command(char *input) {
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
    } else if (strcmp(input, "show") == 0) {
        printf("Usage: show <bytecode_file>\n");
    } else if (strncmp(input, "show ", 5) == 0) {
        char *fname = input + 5;
        while (*fname == ' ') fname++;
        if (*fname == '\0') printf("Usage: show <bytecode_file>\n");
        else                display_bytecode_file(fname);
    } else {
        char *space = strchr(input, ' ');
        if (space) {
            *space = '\0';
            loader(input, space + 1);
        } else {
            loader(input, NULL);
        }
    }
}

static void shell(void) {
    static char buf[4096];
    static size_t len = 0;
    static int prompt_shown = 0;

    if (!prompt_shown) {
        printf("$ ");
        fflush(stdout);
        prompt_shown = 1;
    }

    fd_set rfds;
    FD_ZERO(&rfds);
    FD_SET(STDIN_FILENO, &rfds);
    struct timeval tv = {0, 0};
    if (select(STDIN_FILENO + 1, &rfds, NULL, NULL, &tv) <= 0)
        return;

    char tmp[512];
    ssize_t n = read(STDIN_FILENO, tmp, sizeof(tmp));
    if (n < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) return;
        perror("read");
        return;
    }
    if (n == 0) {
        shell_exit = 1;
        return;
    }

    if (len + (size_t)n >= sizeof(buf)) {
        /* Overflow — discard stale partial line. */
        len = 0;
        prompt_shown = 0;
    }
    memcpy(buf + len, tmp, (size_t)n);
    len += (size_t)n;

    size_t consumed = 0;
    for (size_t i = 0; i < len; i++) {
        if (buf[i] != '\n') continue;

        size_t line_len = i - consumed;
        if (line_len > 0 && buf[consumed + line_len - 1] == '\r')
            line_len--;  /* strip CR */

        char line[512];
        size_t copy_len = (line_len < sizeof(line) - 1) ? line_len : sizeof(line) - 1;
        memcpy(line, buf + consumed, copy_len);
        line[copy_len] = '\0';

        consumed = i + 1;
        handle_shell_command(line);
        if (shell_exit) return;
    }

    if (consumed > 0) {
        memmove(buf, buf + consumed, len - consumed);
        len -= consumed;
        prompt_shown = 0;
    }
}

void os_run(int argc, char** argv) {
    ensure_dir("logs");
    ensure_dir("data");
    ensure_dir("bytecode");

    init_free_pages();

    os_log_fp = fopen("logs/os.log", "a");
    if (!os_log_fp) { perror("fopen os.log"); exit(EXIT_FAILURE); }
    log_event("Simulator started");

    int i = 1;
    while (i < argc) {
        const char *prog = argv[i];
        const char *data = (i + 1 < argc) ? argv[i + 1] : NULL;
        loader(prog, data);
        i += 2;
    }

    if (argc == 1)
        printf("No programs specified. Use the shell to load programs.\n");

    while (!shell_exit) {
        for (int i = 0; i < num_tasks; i++) {
            if (tasks[i].finished || tasks[i].proc_id == -1) continue;
            int proc = tasks[i].proc_id;

            strncpy(current_prog_name, tasks[i].base_name, sizeof(current_prog_name) - 1);
            current_prog_name[sizeof(current_prog_name) - 1] = '\0';

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

    for (int i = 0; i < NP; i++)
        close_log(i);

    log_event("Simulator exiting");
    fclose(os_log_fp);
}
