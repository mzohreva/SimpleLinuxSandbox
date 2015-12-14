# SimpleLinuxSandbox
A simple sandbox using Linux namespaces

```
usage: simple_sandbox -u userid -g groupid [OPTIONS] command [args...]

OPTIONS:
  -t timeout    kill the process after <timeout> milliseconds
  -p            mount /proc
  -s            mount /sys
  -b            mount /bin
  -m            create /dev nodes
  -d            enable debug mode
  -h            shows this message

Executes command with the specified user and group in a virtual
environment with very limited access to system resources.

NOTE: always use an unprivileged userid and groupid
```

# Installation:

Compile the program with `make` and install by `sudo make install`. 
Note that the sandbox's executable should be a SETUID binary owned by root to 
be able to work properly. The Makefile's default target uses `sudo chown root:root ...`
and `sudo chmod +s ...`, so the system will probably ask for your password.


# Sample Usage:

First, you need to find an unprivileged user id and its corresponsing group id.
Most Linux distribution already have an unprivileged user usually named `nobody`.
You can find out the user id of `nobody` on your Linux distribution with the following command:

```
$ cat /etc/passwd | grep nobody
```

On my Ubuntu installation, the above command produces the following output:

```
nobody:x:65534:65534:nobody:/nonexistent:/usr/sbin/nologin
```

To find out the default group name for `nobody`, use:

```
$ groups nobody
```

On my Ubuntu installation, user `nobody` belongs to the group `nogroup`. To find out the group id of `nogroup`, use:

```
$ cat /etc/group | grep nogroup
```

So, using user id of 65534 and group id of 65534 in my case, to run `bash` inside the sandbox:

```
[user@machine ~]$ simple_sandbox -u 65534 -g 65534 -psmb /bin/bash
```

If everything works properly you should see an output like this:

```
[nobody@machine /]$ id
uid=65534(nobody) gid=65534(nogroup) groups=65534(nogroup)
[nobody@machine /]$
[nobody@machine /]$
[nobody@machine /]$ ls -l
total 1032
drwxr-xr-x   2 root root    4096 Oct 14 10:10 bin
drwxr-xr-x   2 root root    4096 Dec 14 14:10 dev
drwxr-xr-x 158 root root   12288 Dec  9 14:22 etc
drwxr-xr-x  26 root root    4096 Feb 27  2015 lib
drwxr-xr-x   2 root root    4096 Feb 27  2015 lib64
dr-xr-xr-x 191 root root       0 Dec 14 14:10 proc
-rwxr-xr-x   1 root root 1021112 Oct  7  2014 prog
dr-xr-xr-x  13 root root       0 Dec 14 14:10 sys
drwxr-xr-x  10 root root    4096 Apr 16  2014 usr
[nobody@machine /]$
```
