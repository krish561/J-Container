#define _GNU_SOURCE
#include <sched.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/sysmacros.h>
#include <signal.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>
#include <libgen.h>

#define STACK_SIZE (1024 * 1024)

static int checkpoint[2];
static pid_t shell_pid = -1;

struct container_config {
    char *rootfs;     // heap-allocated canonical path
    char **cmd_argv;
};

static int pivot_root(const char *new_root, const char *put_old) {
    return syscall(SYS_pivot_root, new_root, put_old);
}

static void sig_forward(int sig) {
    if (shell_pid > 0 && (sig == SIGTERM || sig == SIGINT || sig == SIGQUIT)) {
        kill(shell_pid, sig);
    }
}

/* Bind a host device node into the container path (container_path must exist) */
static int bind_device(const char *host_path, const char *container_path) {
    int fd = open(container_path, O_CREAT | O_WRONLY, 0666);
    if (fd >= 0) close(fd);
    if (mount(host_path, container_path, NULL, MS_BIND, NULL) < 0) {
        fprintf(stderr, "mount bind %s -> %s failed: %s\n", host_path, container_path, strerror(errno));
        return -1;
    }
    return 0;
}

static void run_process(char **argv) {
    /* Minimal environment for container process */
    setenv("PATH", "/bin:/usr/bin:/sbin", 1);
    setenv("TERM", "xterm", 1);
    setenv("HOME", "/root", 1);

    execvp(argv[0], argv);
    perror("execvp");
    _exit(127);
}

/* Single child function created by the one clone() call */
static int child_fn(void *arg) {
    struct container_config *config = arg;
    char *new_root = config->rootfs;
    char ch;

    /* 1) Wait for parent to write uid/gid maps & setgroups */
    close(checkpoint[1]);
    if (read(checkpoint[0], &ch, 1) != 1) {
        fprintf(stderr, "[child] failed to read checkpoint\n");
        return 1;
    }
    close(checkpoint[0]);

    /* 2) NOW create mount namespace (after uid/gid maps are set up) */
    if (unshare(CLONE_NEWNS) < 0) {
        perror("unshare mount namespace");
        return 1;
    }

    /* 3) Make everything private so mounts don't propagate */
    if (mount(NULL, "/", NULL, MS_PRIVATE | MS_REC, NULL) < 0) {
        perror("mount --make-rprivate /");
        return 1;
    }

    /* 4) Bind-mount the rootfs to itself */
    if (mount(new_root, new_root, NULL, MS_BIND | MS_REC, NULL) < 0) {
        fprintf(stderr, "mount rootfs (%s): %s\n", new_root, strerror(errno));
        return 1;
    }

    /* 5) Prepare a /dev inside rootfs (tmpfs) and bind a few host nodes */
    char path_buf[PATH_MAX];
    snprintf(path_buf, sizeof(path_buf), "%s/dev", new_root);
    if (mkdir(path_buf, 0755) < 0 && errno != EEXIST) {
        perror("mkdir new_root/dev");
        return 1;
    }

    if (mount("tmpfs", path_buf, "tmpfs", MS_NOSUID | MS_STRICTATIME, "mode=755") < 0) {
        fprintf(stderr, "mount tmpfs on %s: %s\n", path_buf, strerror(errno));
        return 1;
    }

    char container_dev[PATH_MAX];
    if (bind_device("/dev/null",    strcat(strcpy(container_dev, path_buf), "/null")) < 0) return 1;
    if (bind_device("/dev/zero",    strcat(strcpy(container_dev, path_buf), "/zero")) < 0) return 1;
    if (bind_device("/dev/random",  strcat(strcpy(container_dev, path_buf), "/random")) < 0) return 1;
    if (bind_device("/dev/urandom", strcat(strcpy(container_dev, path_buf), "/urandom")) < 0) return 1;

    /* 6) Create old_root and pivot_root */
    char put_old[PATH_MAX];
    snprintf(put_old, sizeof(put_old), "%s/old_root", new_root);
    if (mkdir(put_old, 0777) < 0 && errno != EEXIST) {
        perror("mkdir old_root");
        return 1;
    }

    if (chdir(new_root) < 0) { perror("chdir new_root"); return 1; }

    if (pivot_root(".", "old_root") < 0) {
        perror("pivot_root");
        return 1;
    }

    if (chdir("/") < 0) { perror("chdir / after pivot"); return 1; }

    /* 7) Standard mounts */
    if (mkdir("/proc", 0555) < 0 && errno != EEXIST) {
        perror("mkdir /proc");
        return 1;
    }
    if (mount("proc", "/proc", "proc", 0, NULL) < 0) {
        perror("mount /proc");
        return 1;
    }

    /* 8) Fix stdio symlinks (point dev/std* to /proc/self/fd/*) */
    unlink("/dev/stdin");  /* ignore errors */
    unlink("/dev/stdout");
    unlink("/dev/stderr");
    if (symlink("/proc/self/fd/0", "/dev/stdin") < 0) perror("symlink stdin");
    if (symlink("/proc/self/fd/1", "/dev/stdout") < 0) perror("symlink stdout");
    if (symlink("/proc/self/fd/2", "/dev/stderr") < 0) perror("symlink stderr");

    /* 9) Clean up old root */
    umount2("/old_root", MNT_DETACH);
    rmdir("/old_root");

    /* 10) Signals for PID 1 */
    struct sigaction sa = {0};
    sa.sa_handler = sig_forward;
    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGINT,  &sa, NULL);
    sigaction(SIGQUIT, &sa, NULL);

    /* 11) Fork the real process; this process remains PID 1 to reap zombies */
    shell_pid = fork();
    if (shell_pid == 0) {
        run_process(config->cmd_argv);
    }

    /* 12) Reaper loop */
    while (1) {
        int status;
        pid_t pid = waitpid(-1, &status, 0);
        if (pid < 0) {
            if (errno == EINTR) continue;
            perror("waitpid");
            return 1;
        }
        if (pid == shell_pid) {
            if (WIFEXITED(status)) return WEXITSTATUS(status);
            if (WIFSIGNALED(status)) return 128 + WTERMSIG(status);
            return 0;
        }
    }
}

/* -------------------- main / parent -------------------- */
int main(int argc, char *argv[]) {
    if (argc < 4 || strcmp(argv[1], "run") != 0) {
        fprintf(stderr, "Usage: %s run <rootfs> <cmd> [args...]\n", argv[0]);
        return 1;
    }

    /* canonicalize the rootfs path here (critical) */
    char real_root[PATH_MAX];
    if (!realpath(argv[2], real_root)) {
        perror("realpath(rootfs)");
        return 1;
    }

    if (pipe(checkpoint) < 0) { perror("pipe"); return 1; }

    /* allocate config on heap so the pointer remains valid for child */
    struct container_config *config = malloc(sizeof(*config));
    if (!config) { perror("malloc config"); return 1; }
    config->rootfs = strdup(real_root);
    config->cmd_argv = &argv[3];

    char *stack = malloc(STACK_SIZE);
    if (!stack) { perror("malloc stack"); free(config); return 1; }

    /* Big-bang clone: create user + pid namespace (mount ns comes later) */
    int flags = CLONE_NEWUSER | CLONE_NEWPID | SIGCHLD;
    pid_t child_pid = clone(child_fn, stack + STACK_SIZE, flags, config);
    if (child_pid < 0) {
        perror("clone");
        free(stack);
        free(config->rootfs);
        free(config);
        return 1;
    }

    /* REQUIRED on Ubuntu rootless: deny setgroups before GID mapping */
    char path[256];
    snprintf(path, sizeof(path), "/proc/%d/setgroups", (int)child_pid);
    int fd = open(path, O_WRONLY);
    if (fd >= 0) {
        if (write(fd, "deny", 4) < 0) perror("write setgroups");
        close(fd);
    } else {
        if (errno != ENOENT) perror("open setgroups");
    }

    /* Map GID then UID (helpers must exist: newgidmap/newuidmap installed) */
    char cmd[512];
    snprintf(cmd, sizeof(cmd), "newgidmap %d 0 %d 1", (int)child_pid, getgid());
    if (system(cmd) != 0) {
        fprintf(stderr, "newgidmap failed\n");
        kill(child_pid, SIGKILL);
        free(stack);
        free(config->rootfs);
        free(config);
        return 1;
    }

    snprintf(cmd, sizeof(cmd), "newuidmap %d 0 %d 1", (int)child_pid, getuid());
    if (system(cmd) != 0) {
        fprintf(stderr, "newuidmap failed\n");
        kill(child_pid, SIGKILL);
        free(stack);
        free(config->rootfs);
        free(config);
        return 1;
    }

    /* Wake child */
    close(checkpoint[0]);
    if (write(checkpoint[1], "1", 1) < 1) perror("write checkpoint");
    close(checkpoint[1]);

    int status;
    waitpid(child_pid, &status, 0);

    free(stack);
    free(config->rootfs);
    free(config);

    if (WIFEXITED(status)) return WEXITSTATUS(status);
    return 1;
}
