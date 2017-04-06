// C headers
extern "C" {
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/mount.h>
#include <sys/wait.h>
#include <signal.h>
#include <libgen.h>
}
// C++ headers
#include <iostream>
#include <system_error>
#include "util.h"

using namespace std;
using namespace util;

bool util::PathExists(string path)
{
    struct stat s;
    int r = lstat(path.c_str(), &s);
    if (r < 0)
    {
        if (errno != ENOENT)
        {
            throw system_error(errno, system_category(), "PathExists, lstat() failed");
        }
        return false;
    }
    return true;
}

bool util::IsRegularFile(string path)
{
    struct stat s;
    int r = lstat(path.c_str(), &s);
    if (r < 0)
    {
        if (errno != ENOENT)
        {
            throw system_error(errno, system_category(), "IsRegularFile, lstat() failed");
        }
        return false;   // Does not exist
    }
    return S_ISREG(s.st_mode);
}

bool util::IsDirectory(string path)
{
    struct stat s;
    int r = lstat(path.c_str(), &s);
    if (r < 0)
    {
        if (errno != ENOENT)
        {
            throw system_error(errno, system_category(), "IsDirectory, lstat() failed");
        }
        return false;   // Does not exist
    }
    return S_ISDIR(s.st_mode);
}

string util::BaseName(string path)
{
    char* copy = strdup(path.c_str());
    string bn = basename(copy);
    free(copy);
    return bn;
}

void util::DeleteFile(string path)
{
    if (unlink(path.c_str()) < 0)
    {
        throw system_error(errno, system_category(), "DeleteFile, unlink() failed");
    }
}

void util::DeleteFolder(string path)
{
    if (rmdir(path.c_str()) < 0)
    {
        throw system_error(errno, system_category(), "DeleteFolder, rmdir() failed");
    }
}

void util::ChangeMode(string path, unsigned short mode)
{
    mode_t old_mask = umask(0);
    if (chmod(path.c_str(), mode) < 0)
    {
        umask(old_mask); // umask does not change errno
        throw system_error(errno, system_category(), "ChangeMode, chmod() failed");
    }
    umask(old_mask);
}

string util::CreateTempFolder(string path_prefix)
{
    char buffer[256];
    const char* X = "XXXXXX";
    if ((path_prefix.size() + strlen(X) + 1) > sizeof(buffer))
    {
        throw runtime_error("path prefix is too long!");
    }
    snprintf(buffer, sizeof(buffer), "%s%s", path_prefix.c_str(), X);
    if (mkdtemp(buffer) == NULL)
    {
        throw system_error(errno, system_category(), "CreateTempFolder, mkdtemp() failed");
    }
    return string(buffer);
}

void util::CreateFolder(string path, unsigned short mode)
{
    mode_t old_mask = umask(0);
    if (mkdir(path.c_str(), mode) < 0)
    {
        umask(old_mask); // umask does not change errno
        throw system_error(errno, system_category(), "CreateFolder, mkdir() failed");
    }
    umask(old_mask);
}

void util::BindMount(string source, string dest)
{
    if (mount(source.c_str(), dest.c_str(), "", MS_BIND | MS_REC, "") < 0)
    {
        throw system_error(errno, system_category(), "BindMount, 1st mount() failed");
    }
    if (mount(source.c_str(), dest.c_str(), "", MS_BIND | MS_REMOUNT | MS_RDONLY | MS_PRIVATE, "") < 0)
    {
        throw system_error(errno, system_category(), "BindMount, 2nd mount() failed");
    }
}

void util::Unmount(string dest)
{
    if (umount(dest.c_str()) < 0)
    {
        throw system_error(errno, system_category(), "Unmount, umount() failed");
    }
}

void util::MarkMountPointPrivate(string path)
{
    if (mount(path.c_str(), path.c_str(), "", MS_REMOUNT | MS_PRIVATE, "") < 0)
    {
        throw system_error(errno, system_category(), "MarkMountPointPrivate, mount() failed");
    }
}

void util::CreatePrivateMount(string path)
{
    if (mount(path.c_str(), path.c_str(), "", MS_BIND | MS_REC, "") < 0)
    {
        throw system_error(errno, system_category(), "CreatePrivateMount, mount() failed");
    }
    MarkMountPointPrivate(path);
}

void util::MountSpecialFileSystem(string path, string fs)
{
    if (mount(fs.c_str(), path.c_str(), fs.c_str(), 0, "") < 0)
    {
        throw system_error(errno, system_category(), "MountSpecialFileSystem, mount() failed");
    }
}

void util::Chroot(string new_root)
{
    if (chroot(new_root.c_str()) < 0)
    {
        throw system_error(errno, system_category(), "Chroot, chroot() failed");
    }
}

void util::Chdir(string path)
{
    if (chdir(path.c_str()) < 0)
    {
        throw system_error(errno, system_category(), "Chdir, chdir() failed");
    }
}

void util::ForkExecWait(char* args[], Task beforeExec)
{
    pid_t pid = fork();
    if (pid == 0)
    {
        // Child
        beforeExec();
        execv(args[0], args);
        cerr << "Error in execv: " << strerror(errno) << endl;
        exit(EXIT_FAILURE);
    }
    else if (pid > 0)
    {
        // Parent
        if (waitpid(pid, NULL, 0) < 0)
        {
            throw system_error(errno, system_category(), "ForkExecWait, waitpid() failed");
        }
    }
    else
    {
        // Error, in parent
        throw system_error(errno, system_category(), "ForkExecWait, fork() failed");
    }
}

void util::ForkExecWaitTimeout(char* args[], Task beforeExec, unsigned int timeout_ms)
{
    pid_t timer_pid = fork();
    if (timer_pid == 0)
    {
        struct timespec req;
        req.tv_sec = timeout_ms / 1000;
        req.tv_nsec = (timeout_ms % 1000) * 1000000;
        // Sleep for the amount of timeout
        nanosleep(&req, NULL);
        _exit(0);
    }
    else if (timer_pid < 0)
    {
        throw system_error(errno, system_category(), "ForkExecWaitTimeout, failed to fork() timer process");
    }
    pid_t child_pid = fork();
    if (child_pid == 0)
    {
        // Child
        beforeExec();
        execv(args[0], args);
        cerr << "Error in execv: " << strerror(errno) << endl;
        exit(EXIT_FAILURE);
    }
    else if (child_pid < 0)
    {
        // Error, in parent
        throw system_error(errno, system_category(), "ForkExecWaitTimeout, failed to fork() child process");
    }
    // Parent: wait
    pid_t x = waitpid(-1, NULL, 0);
    if (x == child_pid)
    {
        kill(timer_pid, SIGKILL);
        waitpid(-1, NULL, 0);
    }
    else if (x == timer_pid)
    {
        kill(child_pid, SIGKILL);
        waitpid(-1, NULL, 0);
    }
    else
    {
        throw runtime_error("ForkExecWaitTimeout, Could not determine which child process exitted");
    }
}

void util::ForkCallWait(Task task)
{
    pid_t pid = fork();
    if (pid == 0)
    {
        // Child
        task();
        exit(EXIT_SUCCESS);
    }
    else if (pid > 0)
    {
        // Parent
        if (waitpid(pid, NULL, 0) < 0)
        {
            throw system_error(errno, system_category(), "ForkCallWait, waitpid() failed");
        }
    }
    else
    {
        // Error, in parent
        throw system_error(errno, system_category(), "ForkCallWait, fork() failed");
    }
}

void util::Unshare(int flags)
{
    if (unshare(flags) < 0)
    {
        throw system_error(errno, system_category(), "Unshare, unshare() failed");
    }
}
