#ifndef _UTIL_D9E2673EFADA464D9659570557AD587E
#define _UTIL_D9E2673EFADA464D9659570557AD587E

#include <string>
#include <functional>

namespace util
{
    bool PathExists(std::string path);

    void DeleteFile(std::string path);

    /* Fails if the folder is not empty */
    void DeleteFolder(std::string path);

    void ChangeMode(std::string path, unsigned short mode);

    /* Returns the actual folder path that is created after
     * appending 6 random characters to path_prefix */
    std::string CreateTempFolder(std::string path_prefix);

    void CreateFolder(std::string path, unsigned short mode = 0755);

    /* Both source and dest must exist. Requires root */
    void BindMount(std::string source, std::string dest);

    void Unmount(std::string dest);

    void MarkMountPointPrivate(std::string path);

    void CreatePrivateMount(std::string path);

    void MountSpecialFileSystem(std::string path, std::string fs);

    void Chroot(std::string new_root);

    void Chdir(std::string path);

    using Task = std::function<void(void)>;

    void ForkExecWait(char* args[], Task beforeExec);

    void ForkExecWaitTimeout(char* args[], Task beforeExec, unsigned int timeout_ms);

    void ForkCallWait(Task task);

    void Unshare(int flags);
}

#endif
