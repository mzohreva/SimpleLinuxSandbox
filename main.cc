// C++ STL headers
#include <iostream>
#include <vector>
#include <stdexcept>
#include <system_error>
// Linux system headers
#include <sched.h>
#include <unistd.h>
#include <stdlib.h>
#include <grp.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
// My headers
#include "util.h"
#include "log.h"

using namespace std;
using namespace util;

SimpleLogStream log;

struct Options
{
    unsigned int timeout_ms;
    uid_t uid;
    gid_t gid;
    bool debug;
    bool mount_proc;
    bool mount_sys;

    Options()
    {
        timeout_ms = 0;
        uid = getuid();
        gid = getgid();
        debug = false;
        mount_proc = false;
        mount_sys = false;
    }

    Options(const Options& o)
     : timeout_ms{o.timeout_ms},
       uid{o.uid}, gid{o.gid}, debug{o.debug},
       mount_proc{o.mount_proc}, mount_sys{o.mount_sys}
    {
    }

    static void Usage(const char* prog);
    static Options Parse(int& argc, char**& argv);
    void Log();
};

void Options::Usage(const char* prog)
{
    cerr << "Usage: " << prog << " [OPTIONS] COMMAND\n";
    cerr << "\n";
    cerr << "OPTIONS:\n";
    cerr << "    -d         Enable debug messages\n";
    cerr << "    -t T       Terminate program after T milliseconds\n";
    cerr << "    -u uid     Run command with user uid\n";
    cerr << "    -g gid     Run command with group gid\n";
    cerr << "    -p         Mount /proc\n";
    cerr << "    -s         Mount /sys\n";
    cerr << "\n";
}

Options Options::Parse(int& argc, char**& argv)
{
    int opt;
    Options options;
    while ((opt = getopt(argc, argv, "+dt:u:g:ps")) != -1) {
        switch (opt) {
            case 'd':   options.debug = true;   break;
            case 't':
            {
                int t = atoi(optarg);
                if (t > 0)
                {
                    options.timeout_ms = t;
                }
                else
                {
                    throw runtime_error("Error parsing options: timeout value must be positive");
                }
                break;
            }
            case 'u':   options.uid = atoi(optarg);     break;
            case 'g':   options.gid = atoi(optarg);     break;
            case 'p':   options.mount_proc = true;      break;
            case 's':   options.mount_sys = true;       break;
            default:
            {
                Usage(argv[0]);
                throw runtime_error("Error parsing options: unknown option");
            }
        }
    }
    argc -= optind;
    argv += optind;
    return options;
}

void Options::Log()
{
    log << boolalpha;
    log << "Options:\n";
    log << "  Timeout: " << timeout_ms << " ms\n";
    log << "  UID: " << uid << "\n";
    log << "  GID: " << gid << "\n";
    log << "  Debug: " << debug << "\n";
    log << "  Mount /proc: " << mount_proc << "\n";
    log << "  Mount /sys: " << mount_sys << "\n";
}

class Sandbox
{
  public:
    explicit Sandbox(Options options_) : options{options_}, ctor_pid{getpid()}
    {
        log << "\n[" << getpid() << "] Sandbox():\n";
        rootfs = CreateTempFolder("/tmp/sandbox_");
        ChangeMode(rootfs, 0755);
        log << " rootfs = " << rootfs << "\n";
        CreatePrivateMount(rootfs);
        for (auto& folder : to_mount)
        {
            if (PathExists(folder))
            {
                string mount_point = rootfs + folder;
                log << " Creating folder " << mount_point << "\n";
                CreateFolder(mount_point);
            }
        }
        program_mount_point = rootfs + program_path;
        log << " Creating program mount point " << program_mount_point << "\n";
        ofstream pfs(program_mount_point);
        pfs.close();
    }

    ~Sandbox()
    {
        if (getpid() == ctor_pid)
        {
            try { // We don't want to throw any exceptions from a dtor
                log << "\n[" << getpid() << "] ~Sandbox():\n";
                log << " ctor_pid = " << ctor_pid << "\n";
                log << " Deleting " << program_mount_point << "\n";
                DeleteFile(program_mount_point);
                for (auto& folder : to_mount)
                {
                    string mount_point = rootfs + folder;
                    if (PathExists(mount_point))
                    {
                        log << " Deleting " << mount_point << "\n";
                        DeleteFolder(mount_point);
                    }
                }
                log << " Unmounting rootfs @ " << rootfs << "\n";
                Unmount(rootfs);
                log << " Deleting rootfs @ " << rootfs << "\n";
                DeleteFolder(rootfs);
                log << "Finished cleanup\n";
            }
            catch (const exception& e) {
                log << "~Sandbox - error cleaning up: " << e.what() << "\n";
            }
        }
    }

    void RunCommand(char* args[])
    {
        ForkCallWait([&]() { unshare_mount(args); });
    }

  private:
    static vector<string> to_mount;
    Options options;
    string rootfs;
    pid_t ctor_pid;
    string program_mount_point;
    static constexpr const char* program_path = "/program";

    void unshare_mount(char* args[])
    {
        try {
            log << "\n[" << getpid() << "] unshare_mount():\n";
            int flags = CLONE_NEWNS | CLONE_NEWIPC | CLONE_NEWUTS |
                        CLONE_NEWNET | CLONE_NEWPID | CLONE_SYSVSEM;
            // TODO: Add CLONE_NEWCGROUP for Linux 4.6+
            Unshare(flags);
            MarkMountPointPrivate("/");

            for (auto& folder : to_mount)
            {
                if (PathExists(folder))
                {
                    string mount_point = rootfs + folder;
                    log << " Mounting folder " << folder << " at " << mount_point << "\n";
                    BindMount(folder, mount_point);
                }
            }

            log << " Mounting program " << args[0] << " at " << program_mount_point << "\n";
            BindMount(args[0], program_mount_point);
            args[0] = strdup(program_path);

            ForkCallWait([&]() { chroot_run(args); });

            log << "\n Unmounting...\n";
            log << " Unmounting " << program_mount_point << "\n";
            Unmount(program_mount_point);

            for (auto& folder : to_mount)
            {
                string mount_point = rootfs + folder;
                if (PathExists(mount_point))
                {
                    log << " Unmounting " << mount_point << "\n";
                    Unmount(mount_point);
                }
            }
            log << "[" << getpid() << "] Finished!\n";
        }
        catch (exception& e) {
            log << "Exception in unshare_mount(): " << e.what() << "\n";
            exit(EXIT_FAILURE);
        }
    }

    void chroot_run(char* args[])
    {
        try {
            log << "\n[" << getpid() << "] chroot_run():\n";
            Chroot(rootfs);
            Chdir("/");

            if (options.mount_proc)
            {
                CreateFolder("/proc");
                MountSpecialFileSystem("/proc", "proc");
            }
            if (options.mount_sys)
            {
                CreateFolder("/sys");
                MountSpecialFileSystem("/sys", "sysfs");
            }

            if (options.timeout_ms > 0)
            {
                ForkExecWaitTimeout(args, [&]() { drop_privilege(); }, options.timeout_ms);
            }
            else
            {
                ForkExecWait(args, [&]() { drop_privilege(); });
            }

            if (options.mount_sys)
            {
                Unmount("/sys");
                DeleteFolder("/sys");
            }
            if (options.mount_proc)
            {
                Unmount("/proc");
                DeleteFolder("/proc");
            }
            log << "\n[" << getpid() << "] chroot_run() finished.\n";
        }
        catch (exception& e) {
            log << "Exception in chroot_run(): " << e.what() << "\n";
            exit(EXIT_FAILURE);
        }
    }

    void drop_privilege(void)
    {
        try
        {
            if (setgroups(0, nullptr) < 0)
            {
                throw system_error(errno, system_category(),
                                   "drop_privilege, setgroups() failed");
            }
            if (setgid(options.gid) < 0)
            {
                throw system_error(errno, system_category(),
                                   "drop_privilege, setgid() failed");
            }
            if (setuid(options.uid) < 0)
            {
                throw system_error(errno, system_category(),
                                   "drop_privilege, setuid() failed");
            }
        }
        catch(exception& e)
        {
            log << "Error: " << e.what() << "\n";
            exit(EXIT_FAILURE);
        }
    }
};

vector<string> Sandbox::to_mount{ "/bin", "/etc", "/lib", "/lib32", "/lib64", "/usr" };

int main(int argc, char* argv[])
{
    char* prog = argv[0];
    Options options = Options::Parse(argc, argv);
    if (options.debug)
    {
        log.SetOutput(&cerr);
    }
    if (argc < 1)
    {
        cerr << "Error: missing command to execute!\n\n";
        Options::Usage(prog);
        exit(EXIT_FAILURE);
    }
    if (options.uid == 0 || options.gid == 0)
    {
        cerr << "Error: could not determine a non-root uid/gid to drop privileges.\n\n";
        cerr << "You should either:\n";
        cerr << "- run this program as a set-user-id binary run by a non-root user\n";
        cerr << "- or specify uid and gid through -u and -g options\n\n";
        Options::Usage(prog);
        exit(EXIT_FAILURE);
    }
    options.Log();
    Sandbox s {options};
    s.RunCommand(argv);
    return 0;
}
