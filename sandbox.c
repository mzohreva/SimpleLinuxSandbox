#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <sched.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <libgen.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <linux/unistd.h>
#include <dirent.h>
#include <time.h>
#include <grp.h>
#include <fcntl.h>

#define EXECUTABLE_MOUNTPOINT "/prog"
#define PERROR_EXIT(msg)    \
        do {                \
            perror(msg);    \
            exit(1);        \
        } while (0)

static char ** g_argv;
static int mount_procfs = 0;
static int mount_sysfs = 0;
static int mount_bin = 0;
static int create_dev_nodes = 0;
static int user = -1;
static int group = -1;
static int timeout = 0;       // Disabled by default. Unit: Milliseconds
static int debug_mode = 0;    // It can be enabled with command line option -d
static char temp_folder[] = "/tmp/sandbox_XXXXXX";
static char * to_mount[] = {"/etc", "/usr", "/lib", "/lib32", "/lib64", NULL};

/*
 * Print a debugging message if debug_mode != 0
 * The message is written to stderr. This function accepts a formatting
 * argument and variable number of arguments similar to printf()
 */
void printd(const char * format, ...)
{
    va_list args;
    if (debug_mode)
    {
        va_start (args, format);
        vfprintf (stderr, format, args);
        va_end (args);
    }
}

/*
 * Prints the usage message and exits program.
 * The message is written to stdout.
 */
static void usage(const char *name)
{
    puts("");
    printf("usage: %s -u userid -g groupid [OPTIONS] command [args...]\n", name);
    puts("");
    puts("OPTIONS:");
    puts("  -t timeout    kill the process after <timeout> milliseconds");
    puts("  -p            mount /proc");
    puts("  -s            mount /sys");
    puts("  -b            mount /bin");
    puts("  -m            create /dev nodes");
    puts("  -d            enable debug mode");
    puts("  -h            shows this message");
    puts("");
    puts("Executes command with the specified user and group in a virtual");
    puts("environment with very limited access to system resources.\n");
    puts("NOTE: always use an unprivileged userid and groupid\n");
    exit(1);
}

/*
 * Lists the contents of a directory similar to ls command
 * This is not recursive and is only meant for debugging purposes.
 * The output is written with printd()
 */
void ls_dir(const char * path)
{
    DIR * dir;
    struct dirent * ent;
    if ((dir = opendir (path)) != NULL)
    {
        // print all the files and directories within path
        while ((ent = readdir (dir)) != NULL)
        {
            if (ent->d_name[0] != '.')
                printd("%s ", ent->d_name);
        }
        closedir (dir);
        printd("\n");
    }
}

/*
 * Returns 1 if path exists, otherwise returns 0
 */
int path_exists(const char * path)
{
    struct stat s;
    int r = stat(path, &s);
    if (r < 0)
    {
        if (errno != ENOENT)
            perror("path_exists() failed");
        return 0;    // false
    }
    else
        return 1;    // true
}

/*
 * Mounts dir to a temporary folder inside temp_folder
 * with the following steps:
 *
 *  - Create a mount point inside the temp_folder for dir
 *  - Mount dir to the mount point created inside temp_folder with the
 *    following options: Bind, Recursive, ReadOnly
 */
void bind_mount(const char * dir)
{
    char path[1024] = { '\0' };
    char error_msg[1024] = { '\0' };

    if (path_exists(dir))
    {
        sprintf(path, "%s%s", temp_folder, dir);
        sprintf(error_msg, "bind_mount() - failed to mount %s", dir);
        // Create the temporary directory
        mkdir(path, S_IRWXU);
        // Mount the directory
        if (mount(dir, path, "", MS_BIND | MS_REC, "") < 0)
            PERROR_EXIT(error_msg);
        // Now make the mount point Read-Only.
        // Note that this cannot be mixed with the previous call to mount()
        if (mount(dir, path, "", MS_BIND | MS_REMOUNT | MS_RDONLY, "") < 0)
            PERROR_EXIT(error_msg);
    }
    else
        printd("bind_mount(): %s cannot be mounted because it does not exist.\n", dir);
}

/*
 * Cleanup temp files and folders created for mount points.
 */
void cleanup()
{
    #define REMOVE_TEMP_SUBDIR(subdir)                          \
            do {                                                \
                sprintf(path, "%s%s", temp_folder, subdir);     \
                if (path_exists(path))                          \
                    if (rmdir(path) < 0)                        \
                        perror(path);                           \
            } while (0)

    #define REMOVE_TEMP_FILE(file)                              \
            do {                                                \
                sprintf(path, "%s%s", temp_folder, file);       \
                if (path_exists(path))                          \
                    if (remove(path) < 0)                       \
                        perror(path);                           \
            } while (0)

    int i;
    char path[1024] = { '\0' };

    i = 0;
    while (to_mount[i] != NULL)
    {
        REMOVE_TEMP_SUBDIR(to_mount[i]);
        i++;
    }
    if (mount_procfs)
    {
        REMOVE_TEMP_SUBDIR("/proc");
    }
    if (mount_sysfs)
    {
        REMOVE_TEMP_SUBDIR("/sys");
    }
    if (mount_bin)
    {
        REMOVE_TEMP_SUBDIR("/bin");
    }
    if (create_dev_nodes)
    {
        REMOVE_TEMP_FILE("/dev/urandom");
        REMOVE_TEMP_FILE("/dev/random");
        REMOVE_TEMP_FILE("/dev/zero");
        REMOVE_TEMP_FILE("/dev/null");
        REMOVE_TEMP_SUBDIR("/dev");
    }
    // Remove /prog
    REMOVE_TEMP_FILE(EXECUTABLE_MOUNTPOINT);
    // Remove the temp_folder itself
    REMOVE_TEMP_SUBDIR("");

    #undef REMOVE_TEMP_FILE
    #undef REMOVE_TEMP_SUBDIR
}

/*
 * Prepares the virtual environment and executes the program with execve().
 */
int prepare_environment_and_execute(void* arg)
{
    #define CREATE_DEV_NODE(node, mode, major, minor)                       \
            do {                                                            \
                sprintf(path, "%s/dev/%s", temp_folder, node);              \
                if (mknod(path, S_IFCHR | mode, makedev(major, minor)) < 0) \
                    PERROR_EXIT("failed to create /dev/" node);             \
            } while (0)

    int i;
    char path[1024] = { '\0' };

    i = 0;
    while (to_mount[i] != NULL)
    {
        bind_mount(to_mount[i]);
        i++;
    }
    if (mount_bin)
    {
        bind_mount("/bin");
    }
    if (create_dev_nodes)
    {
        sprintf(path, "%s/dev", temp_folder);
        mkdir(path, 0755);

        CREATE_DEV_NODE("null", 0666, 1, 3);
        CREATE_DEV_NODE("zero", 0666, 1, 5);
        CREATE_DEV_NODE("random", 0666, 1, 8);
        CREATE_DEV_NODE("urandom", 0666, 1, 9);
    }
    // Mount the executable
    sprintf(path, "%s%s", temp_folder, EXECUTABLE_MOUNTPOINT);
    FILE* fh = fopen(path, "w");
    fclose(fh);
    if (mount(g_argv[0], path, "", MS_BIND, "") < 0)
        PERROR_EXIT("failed to mount the executable");

    if (chroot(temp_folder) < 0)
        PERROR_EXIT("chroot() failed");

    if (chdir("/") < 0)
        PERROR_EXIT("chdir() failed");

    if (mount_procfs)
    {
        mkdir("/proc", S_IRWXU);
        if (mount("proc", "/proc", "proc", 0, "") < 0)
            perror("mounting /proc failed");
    }
    if (mount_sysfs)
    {
        mkdir("/sys", S_IRWXU);
        if (mount("sys", "/sys", "sysfs", 0, "") < 0)
            perror("mounting /sys failed");
    }
    ls_dir("/");
    printd("PID: %d\n", getpid());
    if (group >= 0)
    {
        gid_t gid = (gid_t) group;
        printd("Setting groupid to %d\n", group);
        if (setregid(group, group) < 0)
            PERROR_EXIT("Failed to set groupid");
        if (setgroups(1, &gid) < 0)
            PERROR_EXIT("Failed to set supplementary groups");
    }
    if (user >=0)
    {
        printd("Setting userid to %d\n", user);
        if (setreuid(user, user) < 0)
            PERROR_EXIT("Failed to set userid");
    }
    // NOTE: we are not changing g_argv[0] (the original program path)
    execve(EXECUTABLE_MOUNTPOINT, g_argv, __environ);
    perror("execve failed");
    return -1;

    #undef CREATE_DEV_NODE
}

/*
 * Creates a temp_folder inside /tmp and sets appropriate permissions on it
 * This folder will be used to create a virtual environment (mounting /lib, ...)
 */
void create_temp_folder()
{
    printd("Temp folder pattern: %s\n", temp_folder);
    if (mkdtemp(temp_folder) == NULL)
    {
        PERROR_EXIT("failed to create temp folder");
    }
    else
    {
        chmod(temp_folder, 0755);
        printd("Temp folder: %s\n", temp_folder);
    }
}

int execute_command()
{
    char stack[8192];
    pid_t child, timer_pid;
    int status = 0;

    create_temp_folder();
    child = clone(prepare_environment_and_execute,
                  stack + sizeof (stack),
                  CLONE_NEWNET | CLONE_NEWPID | CLONE_NEWNS |
                  CLONE_NEWIPC | CLONE_NEWUTS,
                  NULL);

    if (child == -1)
    {
        perror("failed to clone");
        return -1;
    }

    // Fork a new child to handle timeout if timeout > 0
    if (timeout > 0)
    {
        timer_pid = fork();
        if (timer_pid == 0)
        {
            // Remember that timeout is in milliseconds
            struct timespec req;
            req.tv_sec = timeout / 1000;
            req.tv_nsec = (timeout % 1000) * 1000000;
            // Sleep for the amount of timeout
            nanosleep(&req, NULL);
            _exit(0);
        }
        else if (timer_pid < 0)
        {
            perror("failed to fork timer process");
        }
    }

    // Wait on child process(es) to finish, and kill the other
    pid_t exitted_child = waitpid(-1, &status, __WALL);
    if (timeout > 0)
    {
        if (exitted_child == timer_pid)
        {
            kill(child, SIGKILL);
            // Collect child
            waitpid(-1, &status, __WALL);
        }
        else if (exitted_child == child)
        {
            kill(timer_pid, SIGKILL);
            // Collect timer
            waitpid(-1, NULL, __WALL);
        }
        else
        {
            perror("unknown return value for waitpid");
        }
    }
    printd("child_pid = %d, timer_pid = %d, exitted_child = %d\n", child, timer_pid, exitted_child);
    cleanup();
    return status;
}

int main(int argc, char *argv[])
{
    int c;
    char * name;

    name = basename(argv[0]);
    opterr = 0;
    while ((c = getopt(argc, argv, "+u:g:t:psbmdh")) != EOF)
    {
        switch (c)
        {
            case 'u':   user = atoi(optarg);    break;
            case 'g':   group = atoi(optarg);   break;
            case 't':   timeout = atoi(optarg); break;
            case 'p':   mount_procfs = 1;       break;
            case 's':   mount_sysfs = 1;        break;
            case 'b':   mount_bin = 1;          break;
            case 'm':   create_dev_nodes = 1;   break;
            case 'd':   debug_mode = 1;         break;
            case 'h':   usage(name);            break;  // will exit
            default:
                printf("Unknown option %c\n", optopt);
                usage(name);
                break;
        }
    }

    if (user == -1 || group == -1)
    {
        printf("Unspecified userid and/or groupid\n");
        usage(name);
    }

    g_argv = argv + optind;
    argc = argc - optind;
    if (argc)
    {
        umask(0);
        return execute_command();
    }
    else
    {
        usage(name);
    }
    return 0;
}
