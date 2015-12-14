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
