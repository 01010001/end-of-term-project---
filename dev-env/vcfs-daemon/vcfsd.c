#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <syslog.h>
#include <dirent.h>
#include <sys/ioctl.h>
#include <errno.h>
#include <zlib.h>

#include "vcfs_ioctl.h"

#define DAEMON_NAME "vcfsd"
#define SLEEP_INTERVAL 60 /* Scan every 60 seconds */

/* Simulates delta compression for an old version */
void compress_version(const char *filepath, __u32 version_id) {
    int fd = open(filepath, O_RDWR);
    if (fd < 0) {
        syslog(LOG_ERR, "Failed to open file for compression: %s", filepath);
        return;
    }

    struct vcfs_ioctl_compress_args args;
    args.version_id = version_id;
    
    syslog(LOG_INFO, "Initiating compression for %s (Version ID: %u)", filepath, version_id);

    /* In a full implementation, we would read the old version data, 
     * diff it against the newer version, compress the delta via zlib, 
     * and pass it back to the kernel. 
     * For this architectural POC, we trigger the IOCTL to notify the kernel 
     * that this version can be marked as compressed/thinned.
     */
    if (ioctl(fd, VCFS_IOC_COMPRESS_VERSION, &args) < 0) {
        syslog(LOG_WARNING, "IOCTL compression trigger failed for %s (V: %u): %s", filepath, version_id, strerror(errno));
    } else {
        syslog(LOG_INFO, "Successfully compressed version %u of %s", version_id, filepath);
    }

    close(fd);
}

void process_file(const char *filepath) {
    int fd = open(filepath, O_RDONLY);
    if (fd < 0) return;

    __u32 version_count = 0;
    if (ioctl(fd, VCFS_IOC_GET_VERSION_COUNT, &version_count) == 0) {
        if (version_count > 1) {
            syslog(LOG_INFO, "File %s has %u versions. Checking for optimization...", filepath, version_count);
            /* Here we would typically fetch the version list and compress versions 
               that are older than a specific threshold (Thinning policy) */
            
            /* Example: compress the oldest version (simulated) */
            compress_version(filepath, 1); 
        }
    }
    close(fd);
}

void scan_directory(const char *dir_path) {
    DIR *dir;
    struct dirent *entry;

    if (!(dir = opendir(dir_path)))
        return;

    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_type == DT_DIR) {
            char path[1024];
            if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
                continue;
            snprintf(path, sizeof(path), "%s/%s", dir_path, entry->d_name);
            scan_directory(path);
        } else if (entry->d_type == DT_REG) {
            char filepath[1024];
            snprintf(filepath, sizeof(filepath), "%s/%s", dir_path, entry->d_name);
            process_file(filepath);
        }
    }
    closedir(dir);
}

static void daemonize() {
    pid_t pid, sid;

    pid = fork();
    if (pid < 0) exit(EXIT_FAILURE);
    if (pid > 0) exit(EXIT_SUCCESS);

    umask(0);

    sid = setsid();
    if (sid < 0) exit(EXIT_FAILURE);

    if ((chdir("/")) < 0) exit(EXIT_FAILURE);

    close(STDIN_FILENO);
    close(STDOUT_FILENO);
    close(STDERR_FILENO);
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        printf("Usage: %s <mount_point>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    char *mount_point = argv[1];

    daemonize();
    openlog(DAEMON_NAME, LOG_PID, LOG_DAEMON);
    syslog(LOG_NOTICE, "VCFS Optimization Daemon started on %s.", mount_point);

    while (1) {
        syslog(LOG_INFO, "Starting background scan of %s...", mount_point);
        scan_directory(mount_point);
        syslog(LOG_INFO, "Scan complete. Sleeping for %d seconds.", SLEEP_INTERVAL);
        sleep(SLEEP_INTERVAL);
    }

    closelog();
    return EXIT_SUCCESS;
}
