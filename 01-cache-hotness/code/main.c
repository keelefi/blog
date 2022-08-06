#define _GNU_SOURCE
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <linux/limits.h>
#include <sched.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/sysinfo.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

static int verbose = 2;

struct settings {
    size_t cache_line_size; // retrieved from sysfs
    size_t memory_total;
    size_t access_per_cache_line;

    size_t yield_count;

    char outfile[PATH_MAX];

    int concurrent_run;
    int fifo_priority;

    size_t cpu;
    ssize_t cpu_freq; // not configurable
};

struct results {
    struct timespec time;
    struct timespec time_parent;
    struct timespec time_child;
    struct timespec time_middle_parent;
    struct timespec time_middle_child;

    size_t vcsw_parent;
    size_t ivcsw_parent;
    size_t vcsw_child;
    size_t ivcsw_child;
};

void print_msg(int level, const char *format, ...)
{
    va_list args;
    va_start(args, format);

    if (level == 0)
    {
        vfprintf(stderr, format, args);
    }
    else if (level <= verbose)
    {
        vprintf(format, args);
    }
}

#define ERROR(...) (print_msg(0, __VA_ARGS__))
#define WARNING(...) (print_msg(1,  __VA_ARGS__))
#define INFO(...) (print_msg(2,  __VA_ARGS__))
#define DEBUG(...) (print_msg(3,  __VA_ARGS__))

size_t parse_size(const char *str)
{
    size_t str_length = strlen(str);
    size_t multiplier = 1;
    if ((str[str_length-1] == 'k') || (str[str_length-1] == 'K'))
    {
        multiplier = 1024;
    }
    else if ((str[str_length-1] == 'm') || (str[str_length-1] == 'M'))
    {
        multiplier = 1024 * 1024;
    }
    else if ((str[str_length-1] == 'g') || (str[str_length-1] == 'G'))
    {
        multiplier = 1024 * 1024 * 1024;
    }

    char *new_str = malloc(str_length);
    strncpy(new_str, str, str_length-1);
    new_str[str_length-1] = '\0';

    size_t result = atoi(new_str) * multiplier;

    free(new_str);

    return result;
}

ssize_t human_readable_size(size_t size, char *buf, size_t buf_size)
{
    ssize_t result;

    if (size < 1024)
    {
        result = snprintf(buf, buf_size-1, "%zu B", size);
    }
    else if (size < (1024 * 1024))
    {
        result = snprintf(buf, buf_size-1, "%zu kB", size / 1024);
    }
    else if (size < (1024 * 1024 * 1024))
    {
        result = snprintf(buf, buf_size-1, "%zu MB", size / (1024 * 1024));
    }
    else
    {
        result = snprintf(buf, buf_size-1, "%zu GB", size / (1024 * 1024 * 1024));
    }
    return result;
}

void show_help(const char *argv0)
{
    char memory_block_size_default[128];
    human_readable_size(getpagesize(), memory_block_size_default, sizeof(memory_block_size_default));

    printf("Usage: %s [-vbmilcfp]\n", argv0);
    printf("-v, --verbose[=VERBOSITY]\n");
    printf("    Set amount of verbosity: 0 for errors only, 1 for warnings, 2 for info (default), 3 for debug.\n");
    printf("-m, --memory_total\n");
    printf("    Set total amount of memory to allocate both in parent and child. Default is 4 MB.\n");
    printf("-a, --access_per_cache_line\n");
    printf("    Specify amount of memory accesses per cache line. Default is 1.\n");
    printf("-y, --yield_count\n");
    printf("    Set yield count. Defaults to 16384.\n");
    printf("-c, --concurrent[=yes|no]\n");
    printf("    Set concurrent or sequential run. If concurrent, both processes access memory concurrently (slower).\n");
    printf("    If unset, processes do sequential memory access, meaning the parent process runs first.\n");
    printf("    Defaults to yes.\n");
    printf("-f, --fifo_priority\n");
    printf("    Set the SCHED_FIFO priority. Defaults to 1.\n"); 
    printf("-c, --cpu\n");
    printf("    Choose the CPU core to run on. Defaults to cpu_count-1.\n");
    printf("-o, --outfile\n");
    printf("    Choose the CPU core to run on. Defaults to cpu_count-1.\n");
}

int parse_options(struct settings *settings, int argc, char **argv)
{
    const struct option long_options[] = {
        {"verbose", optional_argument, 0, 'v'},
        {"memory_total", required_argument, 0, 'm'},
        {"access_per_cache_line", required_argument, 0, 'a'},
        {"yield_count", required_argument, 0, 'y'},
        {"concurrent", optional_argument, 0, 'c'},
        {"fifo_priority", required_argument, 0, 'f'},
        {"cpu", required_argument, 0, 'p'},
        {"outfile", required_argument, 0, 'o'},
        {"help", no_argument, 0, 'h'},
        {0, 0, 0, 0},
    };
    const char *short_options = "hv:m:a:y:c:f:p:o:";

    int c;
    while ((c = getopt_long (argc, argv, short_options, long_options, NULL)) != -1)
    {
        switch (c)
        {
            case 'v':
                if (optarg == NULL)
                {
                    verbose = 1;
                }
                else
                {
                    int new_verbose = atoi(optarg);
                    if ((new_verbose > 3) || (new_verbose < 0))
                    {
                        printf("ERROR: verbose cannot be set to: %d\n", new_verbose);
                        printf("Allowed values for verbose are: 0, 1, 2, 3\n");
                        return -1;
                    }
                    else
                    {
                        verbose = new_verbose;
                    }
                }
                break;
            case 'm':
                settings->memory_total = parse_size(optarg);
                break;
            case 'a':
                settings->access_per_cache_line = atoi(optarg);
                break;
            case 'y':
                settings->yield_count = atoi(optarg);
                break;
            case 'c':
                if (optarg == NULL)
                {
                    settings->concurrent_run = true;
                }
                else
                {
                    if (strcmp(optarg, "yes") == 0)
                    {
                        settings->concurrent_run = true;
                    }
                    else if (strcmp(optarg, "no") == 0)
                    {
                        settings->concurrent_run = false;
                    }
                    else
                    {
                        printf("ERROR: concurrent cannot be set to '%s'\n", optarg);
                        printf("Allowed values for concurrent are: 'yes', 'no'\n");
                        return -1;
                    }
                }
                break;
            case 'f':
                settings->fifo_priority = atoi(optarg);
                break;
            case 'p':
                settings->cpu = atoi(optarg);
                break;
            case 'o':
                strcpy(settings->outfile, optarg);
                break;
            case 'h':
            case '?':
                show_help(argv[0]);
                return -1;
            default:
                printf("?? getopt returned character code 0%o ??\n", c);
        }
    }

    return 0;
}

int get_cpu_count()
{
    int cpu_count = get_nprocs();  // this is "expensive"
    //long cpu_count = sysconf(_SC_NPROCESSORS_ONLN);

    return cpu_count;
}

int get_cache_line_size()
{
    const char SYS_PATH[] = "/sys/devices/system/cpu/cpu0/cache/index0/coherency_line_size";
    int fd = open(SYS_PATH, O_RDONLY | O_CLOEXEC);
    if (fd == -1)
    {
        perror("open");
        return -1;
    }

    char buf[32];
    ssize_t bytes_read = read(fd, buf, sizeof(buf)-1);
    close(fd);
    if (bytes_read == -1)
    {
        perror("read");
        return -1;
    }

    char *endptr;
    ssize_t cache_line_size = strtol(buf, &endptr, 10);
    if (endptr != buf && (*endptr == '\0' || *endptr == '\n'))
    {
        return cache_line_size;
    }
    return -1;
}

ssize_t get_cpu_freq(int cpu, const char *path)
{
    ssize_t cpu_freq;

    int fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd == -1)
    {
        perror("open");
        return -1;
    }

    char buf[32];
    ssize_t bytes_read = read(fd, buf, sizeof(buf)-1);
    close(fd);
    if (bytes_read == -1)
    {
        perror("read");
        return -1;
    }

    char *endptr;
    cpu_freq = strtol(buf, &endptr, 10);
    if (endptr != buf && (*endptr == '\0' || *endptr == '\n'))
    {
        return cpu_freq;
    }
    return -1;
}

ssize_t get_cpu_freq_cpuinfo(const struct settings *settings)
{
    char SYS_PATH[128];
    if (snprintf(SYS_PATH, sizeof(SYS_PATH)-1, "/sys/devices/system/cpu/cpu%zu/cpufreq/cpuinfo_cur_freq", settings->cpu) < 0)
    {
        perror("snprintf");
        return -1;
    }
    DEBUG("Reading CPU frequency from: %s\n", SYS_PATH);

    return get_cpu_freq(settings->cpu, SYS_PATH);
}

ssize_t get_cpu_freq_scaling(const struct settings *settings)
{
    char SYS_PATH[128];
    if (snprintf(SYS_PATH, sizeof(SYS_PATH)-1, "/sys/devices/system/cpu/cpu%zu/cpufreq/scaling_cur_freq", settings->cpu) < 0)
    {
        perror("snprintf");
        return -1;
    }
    DEBUG("Reading CPU frequency from: %s\n", SYS_PATH);

    return get_cpu_freq(settings->cpu, SYS_PATH);
}

int cpu_freq_to_str(ssize_t cpu_freq, char *buf, size_t buf_size)
{
    ssize_t gigahz = cpu_freq / (1000 * 1000);
    ssize_t megahz = cpu_freq / 1000;
    int result = snprintf(buf, buf_size-1, "%ld.%03ld GHz", gigahz, megahz);
    return result < 0 ? -1 : 0;
}

ssize_t get_cache_size(const char *base_path)
{
    char SYS_PATH[PATH_MAX];
    snprintf(SYS_PATH, sizeof(SYS_PATH), "%s/type", base_path);
    int fd_type = open(SYS_PATH, O_RDONLY | O_CLOEXEC);
    if (fd_type == -1)
    {
        perror("open");
        return -1;
    }
    char buf[32];
    memset(buf, 0, sizeof(buf));
    ssize_t bytes_read = read(fd_type, buf, sizeof(buf)-1);
    close(fd_type);
    if (bytes_read == -1)
    {
        perror("read");
        return -1;
    }
    if (buf[bytes_read-1] == '\n')
    {
        buf[bytes_read-1] = 0;
    }
    DEBUG("Cache type is: %s\n", buf);
    if (strcmp(buf, "Instruction") == 0)
    {
        // skip instruction cache, we are only interested in data caches
        return 0;
    }
    else if ((strcmp(buf, "Data") != 0) && (strcmp(buf, "Unified") != 0))
    {
        ERROR("Invalid cache type: %s\n", buf);
        return -1;
    }

    snprintf(SYS_PATH, sizeof(SYS_PATH), "%s/size", base_path);
    int fd = open(SYS_PATH, O_RDONLY | O_CLOEXEC);
    if (fd == -1)
    {
        perror("open");
        return -1;
    }

    bytes_read = read(fd, buf, sizeof(buf)-1);
    close(fd);
    if (bytes_read == -1)
    {
        perror("read");
        return -1;
    }
    if (buf[bytes_read-1] == '\n')
    {
        buf[bytes_read-1] = 0;
    }
    DEBUG("Cache size is: %s\n", buf);

    size_t cache_size = parse_size(buf);
    DEBUG("Cache size retrieved: %zu\n", cache_size);

    return cache_size;
}

int get_cache_sizes(size_t cpu, size_t *cache_sizes, size_t buf_size)
{
    char BASE_PATH[PATH_MAX];
    snprintf(BASE_PATH, sizeof(BASE_PATH), "/sys/devices/system/cpu/cpu%zu/cache/", cpu);

    DIR *cache_dir = opendir(BASE_PATH);
    if (!cache_dir)
    {
        perror("opendir");
        return -1;
    }

    int highest_index = -1;
    struct dirent *cache_dirent;
    errno = 0;
    while ((cache_dirent = readdir(cache_dir)) != NULL)
    {
        int index;
        int scanned = sscanf(cache_dirent->d_name, "index%d", &index);
        if (scanned == 1)
        {
            char CACHE_PATH[PATH_MAX];
            if (snprintf(CACHE_PATH, sizeof(CACHE_PATH), "%s/%s/", BASE_PATH, cache_dirent->d_name) > PATH_MAX)
            {
                perror("snprintf");
                return -1;
            }
            ssize_t cache_size = get_cache_size(CACHE_PATH);
            if (cache_size > 0)
            {
                cache_sizes[index] = cache_size;
                DEBUG("Cache[%d] size is: %zu\n", index, cache_size);

                if (index > highest_index)
                {
                    highest_index = index;
                }
            }
        }
    }
    if (errno)
    {
        perror("readdir");
        return -1;
    }

    closedir(cache_dir);
    return highest_index+1;
}

int get_cache_sizes_str(char *buf, size_t buf_size, size_t cpu, bool human)
{
    ssize_t cache_sizes[10];
    memset(cache_sizes, 0, sizeof(cache_sizes));
    int cache_count = get_cache_sizes(cpu, cache_sizes, sizeof(cache_sizes));
    DEBUG("Cache count is: %d\n", cache_count);
    if (cache_count <= 0)
    {
        return -1;
    }

    buf[0] = '[';
    size_t result_size = 1;
    for (size_t i = 0; i < cache_count; ++i)
    {
        if (cache_sizes[i] > 0)
        {
            char cache_buf[128];
            if (human)
            {
                human_readable_size(cache_sizes[i], cache_buf, sizeof(cache_buf));
            }
            else
            {
                snprintf(cache_buf, sizeof(cache_buf), "%zu", cache_sizes[i]);
            }
            int chars_written = snprintf(buf+result_size, buf_size-result_size, "%s, ", cache_buf);
            if (chars_written > (buf_size-result_size))
            {
                ERROR("Cache size string truncated\n");
                return -1;
            }
            result_size += chars_written;
        }
    }
    if (result_size == 1)
    {
        ERROR("No caches found\n");
        return -1;
    }
    snprintf(buf+result_size-2, buf_size-result_size, "]");

    return 0;
}

int set_affinity(int cpu)
{
    int cpu_dest = cpu - 1;

    cpu_set_t cpu_set;
    CPU_ZERO(&cpu_set);
    CPU_SET(cpu_dest, &cpu_set);

    if (sched_setaffinity(0, sizeof(cpu_set_t), &cpu_set))
    {
        perror("sched_setaffinity");
        return -1;
    }

    INFO("CPU affinity set to: %d\n", cpu_dest);

    return 0;
}

int set_fifo_scheduling(int priority)
{
    struct sched_param params = { .sched_priority = priority };

    if (sched_setscheduler(0, SCHED_FIFO, &params))
    {
        perror("sched_setscheduler");
        return -1;
    }

    INFO("Scheduling algorithm set to: SCHED_FIFO\n");

    return 0;
}

int open_pipes(int parent_pipefds[], int child_pipefds[])
{
    if (pipe(parent_pipefds) == -1)
    {
        perror("pipe");
        return -1;
    }

    if (pipe(child_pipefds) == -1)
    {
        perror("pipe");
        return -1;
    }

    return 0;
}

int synchronize(bool is_child, char phase, int parent_pipefds[], int child_pipefds[])
{
    int write_fd = is_child ? child_pipefds[1] : parent_pipefds[1];
    int read_fd = is_child ? parent_pipefds[0] : child_pipefds[0];

    if (write(write_fd, &phase, 1) == -1)
    {
        perror("write");
        return -1;
    }

    char buf;
    if (read(read_fd, &buf, 1) == -1)
    {
        perror("read");
        return -1;
    }

    if (buf != phase)
    {
        ERROR("ERROR: in synchronize(), expected %c but got %c\n", phase, buf);
        return -1;
    }

    return 0;
}

void initialize_settings(struct settings *settings)
{
    settings->cache_line_size = get_cache_line_size();
    settings->memory_total = 4 * 1024 * 1024; // 4 MiB
    settings->access_per_cache_line = 1;

    settings->yield_count = 16;

    settings->concurrent_run = true;
    settings->fifo_priority = 1;

    settings->cpu = get_cpu_count() - 1;
    settings->cpu_freq = -1;

    strcpy(settings->outfile, "");
}

int configure(struct settings *settings)
{
    char buf[128];

    if (set_affinity(settings->cpu))
    {
        return -1;
    }

    settings->cpu_freq = get_cpu_freq_cpuinfo(settings);
    if (settings->cpu_freq == -1)
    {
        return -1;
    }
    if (cpu_freq_to_str(settings->cpu_freq, buf, sizeof(buf)))
    {
        return -1;
    }
    INFO("CPU freq: %s\n", buf);

    if (set_fifo_scheduling(settings->fifo_priority))
    {
        return -1;
    }

    return 0;
}

void print_settings(const struct settings *settings)
{
    char buf[128];
    INFO("Concurrent run: %s\n", settings->concurrent_run ? "yes" : "no");
    INFO("Cache line size: %zu\n", settings->cache_line_size);
    char cache_sizes_str[100];
    get_cache_sizes_str(cache_sizes_str, sizeof(cache_sizes_str), settings->cpu, true);
    INFO("Cache sizes: %s\n", cache_sizes_str);
    human_readable_size(settings->memory_total, buf, sizeof(buf));
    INFO("Memory total: %s\n", buf);
    INFO("Accesses per cache line: %zu\n", settings->access_per_cache_line);
    INFO("Yield count: %zu\n", settings->yield_count);
}

int write_file(const struct settings *settings, const struct results *results)
{
    int fd = open(settings->outfile, O_WRONLY | O_CLOEXEC | O_CREAT | O_EXCL, 0644);
    if (fd == -1)
    {
        perror("open");
        return -1;
    }

    char cache_sizes_str[100];
    get_cache_sizes_str(cache_sizes_str, sizeof(cache_sizes_str), settings->cpu, false);

    dprintf(fd, "{\n");
    dprintf(fd, "   \"general\": {\n");
    dprintf(fd, "       \"algorithm\": \"SCHED_FIFO\",\n");
    dprintf(fd, "   },\n");
    dprintf(fd, "   \"cpu\": {\n");
    dprintf(fd, "       \"id\": %zu,\n", settings->cpu);
    dprintf(fd, "       \"cpu_freq\": %zu,\n", settings->cpu_freq);
    dprintf(fd, "       \"cache_line_size\": %zu,\n", settings->cache_line_size);
    dprintf(fd, "       \"cache_sizes\": %s,\n", cache_sizes_str);
    dprintf(fd, "   },\n");
    dprintf(fd, "   \"settings\": {\n");
    dprintf(fd, "       \"concurrent\": %s,\n", settings->concurrent_run ? "true" : "false");
    dprintf(fd, "       \"memory\": %zu,\n", settings->memory_total);
    dprintf(fd, "       \"yield_count\": %zu,\n", settings->yield_count);
    dprintf(fd, "       \"access_per_cache_line\": %zu,\n", settings->access_per_cache_line);
    dprintf(fd, "   },\n");
    dprintf(fd, "   \"result\": {\n");
    dprintf(fd, "       \"time\": %ld.%09ld,\n", results->time.tv_sec, results->time.tv_nsec);
    dprintf(fd, "       \"time_parent\": %ld.%09ld,\n", results->time_parent.tv_sec, results->time_parent.tv_nsec);
    dprintf(fd, "       \"time_child\": %ld.%09ld,\n", results->time_child.tv_sec, results->time_child.tv_nsec);
    dprintf(fd, "       \"time_middle_parent\": %ld.%09ld,\n", results->time_middle_parent.tv_sec, results->time_middle_parent.tv_nsec);
    dprintf(fd, "       \"time_middle_child\": %ld.%09ld,\n", results->time_middle_child.tv_sec, results->time_middle_child.tv_nsec);
    dprintf(fd, "       \"vcsw_parent\": %zu,\n", results->vcsw_parent);
    dprintf(fd, "       \"ivcsw_parent\": %zu,\n", results->ivcsw_parent);
    dprintf(fd, "       \"vcsw_child\": %zu,\n", results->vcsw_child);
    dprintf(fd, "       \"ivcsw_child\": %zu,\n", results->ivcsw_child);
    dprintf(fd, "   }\n");
    dprintf(fd, "}\n");

    return 0;
}

int main(int argc, char *argv[])
{
    char buf[128];
    bool is_child = false;

    struct settings settings;
    initialize_settings(&settings);

    if (parse_options(&settings, argc, argv))
    {
        exit(EXIT_FAILURE);
    }

    if (configure(&settings))
    {
        show_help(argv[0]);
        exit(EXIT_FAILURE);
    }

    print_settings(&settings);

    int parent_pipefds[2];
    int child_pipefds[2];
    if (open_pipes(parent_pipefds, child_pipefds))
    {
        exit(EXIT_FAILURE);
    }

    pid_t child_pid = fork();
    if (child_pid == -1)
    {
        perror("fork");
        exit(EXIT_FAILURE);
    }

    if (child_pid == 0)
    {
        is_child = true;
    }

    size_t cache_line_count = settings.memory_total / settings.cache_line_size;
    size_t **memory_blocks = malloc(cache_line_count * sizeof(size_t *));
    for (size_t i = 0; i < cache_line_count; ++i)
    {
        memory_blocks[i] = malloc(settings.cache_line_size);

        // make sure memory is mapped to real memory, not just virtual
        // otherwise we could end up having page faults during the actual benchmark, distorting the measurements
        for (size_t k = 0; k < settings.access_per_cache_line; ++k)
        {
            memory_blocks[i][k] = 0; 
        }
    }

    if (synchronize(is_child, '1', parent_pipefds, child_pipefds))
    {
        exit(EXIT_FAILURE);
    }

    struct timespec time_start;
    if (clock_gettime(CLOCK_MONOTONIC, &time_start))
    {
        perror("clock_gettime");
        exit(EXIT_FAILURE);
    }

    for (size_t i = 0; i < settings.yield_count; ++i)
    {
        if (is_child && !settings.concurrent_run)
        {
            sched_yield();
            continue;
        }

        for (size_t j = 0; j < cache_line_count; ++j)
        {
            for (size_t k = 0; k < settings.access_per_cache_line; ++k)
            {
                memory_blocks[j][k]++;
            }
        }

        sched_yield();
    }

    struct timespec time_middle;
    if (clock_gettime(CLOCK_MONOTONIC, &time_middle))
    {
        perror("clock_gettime");
        exit(EXIT_FAILURE);
    }
    long time_diff_middle_ns = time_middle.tv_nsec - time_start.tv_nsec +
                        (time_middle.tv_sec - time_start.tv_sec) * 1000 * 1000 * 1000;
    struct timespec time_diff_middle = {
            .tv_sec = time_diff_middle_ns / (1000 * 1000 * 1000),
            .tv_nsec = time_diff_middle_ns % (1000 * 1000 * 1000)
    };

    if (is_child && !settings.concurrent_run)
    {
        for (size_t i = 0; i < settings.yield_count; ++i)
        {
            for (size_t j = 0; j < cache_line_count; ++j)
            {
                for (size_t k = 0; k < settings.access_per_cache_line; ++k)
                {
                    memory_blocks[j][k]++;
                }
            }
        }
    }

    struct timespec time_finished;
    if (clock_gettime(CLOCK_MONOTONIC, &time_finished))
    {
        perror("clock_gettime");
        exit(EXIT_FAILURE);
    }

    for (size_t i = 0; i < cache_line_count; ++i)
    {
        free(memory_blocks[i]);
    }
    free(memory_blocks);

    long time_diff_ns = time_finished.tv_nsec - time_start.tv_nsec +
                        (time_finished.tv_sec - time_start.tv_sec) * 1000 * 1000 * 1000;
    struct timespec time_diff = {
            .tv_sec = time_diff_ns / (1000 * 1000 * 1000),
            .tv_nsec = time_diff_ns % (1000 * 1000 * 1000)
    };

    if (is_child)
    {
        if (write(child_pipefds[1], &time_diff_middle, sizeof(time_diff_middle)) == -1)
        {
            perror("write");
            exit(EXIT_FAILURE);
        }
        if (write(child_pipefds[1], &time_diff, sizeof(time_diff)) == -1)
        {
            perror("write");
            exit(EXIT_FAILURE);
        }
        exit(EXIT_SUCCESS);
    }

    struct timespec time_diff_middle_child;
    struct timespec time_diff_child;
    if (read(child_pipefds[0], &time_diff_middle_child, sizeof(time_diff_middle_child)) == -1)
    {
        perror("read");
        exit(EXIT_FAILURE);
    }
    if (read(child_pipefds[0], &time_diff_child, sizeof(time_diff_child)) == -1)
    {
        perror("read");
        exit(EXIT_FAILURE);
    }

    INFO("Execution time middle parent: %ld.%09ld s\n", time_diff_middle.tv_sec, time_diff_middle.tv_nsec);
    INFO("Execution time middle child: %ld.%09ld s\n", time_diff_middle_child.tv_sec, time_diff_middle_child.tv_nsec);
    INFO("Execution time parent: %ld.%09ld s\n", time_diff.tv_sec, time_diff.tv_nsec);
    INFO("Execution time child: %ld.%09ld s\n", time_diff_child.tv_sec, time_diff_child.tv_nsec);

    long time_diff_average_ns = ((time_diff.tv_sec + time_diff_child.tv_sec) * 1000 * 1000 * 1000 +
                                time_diff.tv_nsec + time_diff_child.tv_nsec) / 2;
    struct timespec time_diff_average = {
            .tv_sec = time_diff_average_ns / (1000 * 1000 * 1000),
            .tv_nsec = time_diff_average_ns % (1000 * 1000 * 1000)
    };

    INFO("Execution time average: %ld.%09ld s\n", time_diff_average.tv_sec, time_diff_average.tv_nsec);

    ssize_t cpu_freq_finish = get_cpu_freq_cpuinfo(&settings);
    if (settings.cpu_freq != cpu_freq_finish)
    {
        printf("[%s] CPU freq at start is different than at finish!\n", is_child ? "CHILD" : "PARENT");
        printf("[%s] Turn off freq scaling for more reliable results\n", is_child ? "CHILD" : "PARENT");
        if (cpu_freq_to_str(settings.cpu_freq, buf, sizeof(buf)))
        {
            exit(EXIT_FAILURE);
        }
        printf("[%s] CPU freq at start: %s\n", is_child ? "CHILD" : "PARENT", buf);
        if (cpu_freq_to_str(cpu_freq_finish, buf, sizeof(buf)))
        {
            exit(EXIT_FAILURE);
        }
        printf("[%s] CPU freq at finish: %s\n", is_child ? "CHILD" : "PARENT", buf);
    }

    if (!is_child)
    {
        wait(NULL);
    }

    struct rusage rusage_parent;
    if (getrusage(RUSAGE_SELF, &rusage_parent))
    {
        perror("getrusage");
        exit(EXIT_FAILURE);
    }
    struct rusage rusage_child;
    if (getrusage(RUSAGE_CHILDREN, &rusage_child))
    {
        perror("getrusage");
        exit(EXIT_FAILURE);
    }

    INFO("Parent voluntary context switches: %zu\n", rusage_parent.ru_nvcsw);
    INFO("Parent involuntary context switches: %zu\n", rusage_parent.ru_nivcsw);
    INFO("Child voluntary context switches: %zu\n", rusage_child.ru_nvcsw);
    INFO("Child involuntary context switches: %zu\n", rusage_child.ru_nivcsw);

    struct results results = {
        .time = time_diff_average,
        .time_parent = time_diff,
        .time_child = time_diff_child,
        .time_middle_parent = time_diff_middle,
        .time_middle_child = time_diff_middle_child,
        .vcsw_parent = rusage_parent.ru_nvcsw,
        .ivcsw_parent = rusage_parent.ru_nivcsw,
        .vcsw_child = rusage_child.ru_nvcsw,
        .ivcsw_child = rusage_child.ru_nivcsw,
    };
    if (strlen(settings.outfile) > 0)
    {
        if (write_file(&settings, &results))
        {
            exit(EXIT_FAILURE);
        }
    }

    exit(EXIT_SUCCESS);
}

