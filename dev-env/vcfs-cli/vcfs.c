#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <linux/types.h>
#include <errno.h>
#include <time.h>
#include <sys/stat.h>

/* Kernel modülü ile aynı IOCTL tanımlamalarını kullanıyoruz */
#define VCFS_IOCTL_MAGIC 'v'

#define VCFS_IOC_GET_VERSION_COUNT _IOR(VCFS_IOCTL_MAGIC, 1, __u32)

struct vcfs_ioctl_version_info {
    __u32 version_id;
    __u32 timestamp;
    __u32 size;
    __u32 is_compressed;
};
#define VCFS_IOC_GET_VERSIONS _IOWR(VCFS_IOCTL_MAGIC, 2, struct vcfs_ioctl_version_info)
#define VCFS_IOC_CHECKOUT_VERSION _IOW(VCFS_IOCTL_MAGIC, 4, __u32)

/* IOCTL structures for Trash Management */
struct vcfs_ioctl_trash_info {
    __u32 inode_no;
    __u32 delete_timestamp;
    __u32 size;
    char filename[32];
};

struct vcfs_ioctl_trash_list_args {
    __u32 count;
    struct vcfs_ioctl_trash_info *items;
};

#define VCFS_IOC_GET_TRASH_COUNT _IOR(VCFS_IOCTL_MAGIC, 5, __u32)
#define VCFS_IOC_GET_TRASH_LIST  _IOWR(VCFS_IOCTL_MAGIC, 6, struct vcfs_ioctl_trash_list_args)
#define VCFS_IOC_RESTORE_TRASH   _IOW(VCFS_IOCTL_MAGIC, 7, __u32)
#define VCFS_IOC_CLEAN_TRASH     _IO(VCFS_IOCTL_MAGIC, 8)

struct vcfs_ioctl_read_args {
    __u32 version_id;
    char *buf;
    __u32 count;
};
#define VCFS_IOC_READ_VERSION _IOWR(VCFS_IOCTL_MAGIC, 9, struct vcfs_ioctl_read_args)

void print_usage() {
    printf("VCFS (Version Control File System) CLI Araçları\n");
    printf("Kullanım:\n");
    printf("  vcfs log <dosya_yolu>                   : Dosyanın tüm versiyon tarihçesini listeler (ID, Tarih, Boyut).\n");
    printf("  vcfs checkout <dosya_yolu> <v_id>       : Dosyayı belirtilen versiyon ID'sine geri döndürür.\n");
    printf("  vcfs diff <dosya_yolu> <v_id1> <v_id2>  : İki versiyon arasındaki farkları gösterir.\n");
    printf("  vcfs restore <inode_no>                 : Silinmiş bir dosyayı çöp kutusundan geri yükler (Inode No ile).\n");
    printf("  vcfs trash --list / --clean             : Çöp kutusundaki dosyaları listeler veya temizler.\n");
    printf("  vcfs status <dosya_yolu>                : Dosyanın anlık durumu ve korunma durumu hakkında bilgi verir.\n");
}

int cmd_status(const char *filepath) {
    int fd = open(filepath, O_RDONLY);
    if (fd < 0) {
        perror("Dosya açılamadı");
        return -1;
    }

    __u32 count = 0;
    if (ioctl(fd, VCFS_IOC_GET_VERSION_COUNT, &count) < 0) {
        perror("IOCTL hatası (VCFS modülü yüklü olmayabilir veya desteklemiyor)");
        close(fd);
        return -1;
    }

    printf("Dosya: %s\n", filepath);
    printf("Mevcut Versiyon Sayısı (Geçmiş dahil): %u\n", count);

    close(fd);
    return 0;
}

int cmd_log(const char *filepath) {
    int fd = open(filepath, O_RDONLY);
    if (fd < 0) {
        perror("Dosya açılamadı");
        return -1;
    }

    __u32 count = 0;
    if (ioctl(fd, VCFS_IOC_GET_VERSION_COUNT, &count) < 0) {
        perror("IOCTL hatası");
        close(fd);
        return -1;
    }

    if (count == 0) {
        printf("Bu dosya için kayıtlı bir versiyon geçmişi bulunmuyor.\n");
        close(fd);
        return 0;
    }

    struct vcfs_ioctl_version_info *versions = malloc(sizeof(struct vcfs_ioctl_version_info) * count);
    if (!versions) {
        perror("Bellek hatası");
        close(fd);
        return -1;
    }

    if (ioctl(fd, VCFS_IOC_GET_VERSIONS, versions) < 0) {
        perror("Versiyon detayları okunamadı");
        free(versions);
        close(fd);
        return -1;
    }

    printf("Versiyon Geçmişi: %s\n", filepath);
    printf("----------------------------------------------------------------------------------\n");
    printf("Versiyon ID | Tarih               | Boyut      | Sıkıştırılmış mı? | Durum\n");
    printf("----------------------------------------------------------------------------------\n");

    for (__u32 i = 0; i < count; i++) {
        time_t ts = versions[i].timestamp;
        struct tm *tm_info = localtime(&ts);
        char time_buf[26];
        strftime(time_buf, 26, "%Y-%m-%d %H:%M:%S", tm_info);

        printf("v%-10u | %-19s | %-10u | %-17s | %s\n", 
            versions[i].version_id, 
            time_buf, 
            versions[i].size,
            versions[i].is_compressed ? "Evet" : "Hayır",
            (i == 0) ? "* Aktif Versiyon *" : "");
    }

    free(versions);
    close(fd);
    return 0;
}

int cmd_checkout(const char *filepath, __u32 target_version) {
    int fd = open(filepath, O_RDWR);
    if (fd < 0) {
        perror("Dosya açılamadı (Yazma izniniz olduğundan emin olun)");
        return -1;
    }

    printf("Checkout işlemi başlatılıyor... Hedef versiyon: %u\n", target_version);

    if (ioctl(fd, VCFS_IOC_CHECKOUT_VERSION, &target_version) < 0) {
        perror("Checkout işlemi başarısız");
        close(fd);
        return -1;
    }

    printf("Başarılı: Dosya %u numaralı versiyona geri döndürüldü.\n", target_version);
    
    close(fd);
    return 0;
}

int cmd_diff(const char *filepath, __u32 v1, __u32 v2) {
    int fd = open(filepath, O_RDONLY);
    if (fd < 0) {
        perror("Dosya açılamadı");
        return -1;
    }

    char tmp1[64], tmp2[64];
    snprintf(tmp1, sizeof(tmp1), "/tmp/vcfs_diff_v%u", v1);
    snprintf(tmp2, sizeof(tmp2), "/tmp/vcfs_diff_v%u", v2);

    int f1 = open(tmp1, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    int f2 = open(tmp2, O_WRONLY | O_CREAT | O_TRUNC, 0644);

    if (f1 < 0 || f2 < 0) {
        perror("Geçici dosyalar oluşturulamadı");
        close(fd);
        return -1;
    }

    char *buf = malloc(4096);
    struct vcfs_ioctl_read_args args;
    
    /* Read v1 */
    args.version_id = v1;
    args.buf = buf;
    args.count = 4096; /* For POC, diffing first 4KB */
    if (ioctl(fd, VCFS_IOC_READ_VERSION, &args) == 0 && args.count > 0)
        write(f1, buf, args.count);

    /* Read v2 */
    args.version_id = v2;
    args.buf = buf;
    args.count = 4096;
    if (ioctl(fd, VCFS_IOC_READ_VERSION, &args) == 0 && args.count > 0)
        write(f2, buf, args.count);

    close(f1);
    close(f2);
    free(buf);
    close(fd);

    printf("Diff for %s (v%u vs v%u):\n", filepath, v1, v2);
    
    char cmd[256];
    snprintf(cmd, sizeof(cmd), "diff -u %s %s", tmp1, tmp2);
    system(cmd);

    unlink(tmp1);
    unlink(tmp2);
    return 0;
}

int cmd_trash(const char *action) {
    int dir_fd = open(".", O_RDONLY | O_DIRECTORY);
    if (dir_fd < 0) {
        perror("Dizin açılamadı");
        return -1;
    }

    if (strcmp(action, "--list") == 0) {
        __u32 count = 0;
        if (ioctl(dir_fd, VCFS_IOC_GET_TRASH_COUNT, &count) < 0) {
            perror("Çöp kutusu sayılamadı");
            close(dir_fd);
            return -1;
        }

        if (count == 0) {
            printf("Çöp kutusu boş.\n");
            close(dir_fd);
            return 0;
        }

        struct vcfs_ioctl_trash_list_args args;
        args.count = count;
        args.items = malloc(sizeof(struct vcfs_ioctl_trash_info) * count);

        if (ioctl(dir_fd, VCFS_IOC_GET_TRASH_LIST, &args) < 0) {
            perror("Çöp kutusu listesi alınamadı");
            free(args.items);
            close(dir_fd);
            return -1;
        }

        printf("Çöp Kutusundaki Dosyalar\n");
        printf("------------------------------------------------------------------\n");
        printf("Inode      | Dosya Adı           | Boyut      | Silinme Tarihi\n");
        printf("------------------------------------------------------------------\n");
        for (__u32 i = 0; i < args.count; i++) {
            time_t ts = args.items[i].delete_timestamp;
            struct tm *tm_info = localtime(&ts);
            char time_buf[26];
            strftime(time_buf, 26, "%Y-%m-%d %H:%M:%S", tm_info);

            printf("%-10u | %-19s | %-10u | %s\n", 
                args.items[i].inode_no, 
                args.items[i].filename, 
                args.items[i].size,
                time_buf);
        }
        free(args.items);
    } else if (strcmp(action, "--clean") == 0) {
        if (ioctl(dir_fd, VCFS_IOC_CLEAN_TRASH) < 0) {
            perror("Çöp kutusu temizlenemedi");
        } else {
            printf("Başarılı: Çöp kutusu temizlendi.\n");
        }
    } else {
        printf("Geçersiz argüman: %s\n", action);
    }

    close(dir_fd);
    return 0;
}

int cmd_restore(const char *inode_str) {
    int dir_fd = open(".", O_RDONLY | O_DIRECTORY);
    if (dir_fd < 0) {
        perror("Dizin açılamadı");
        return -1;
    }

    __u32 target_ino = (__u32)atoi(inode_str);
    if (ioctl(dir_fd, VCFS_IOC_RESTORE_TRASH, &target_ino) < 0) {
        perror("Geri getirme (Restore) işlemi başarısız");
        close(dir_fd);
        return -1;
    }

    printf("Başarılı: %u inode numaralı dosya çöp kutusundan geri yüklendi.\n", target_ino);
    close(dir_fd);
    return 0;
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        print_usage();
        return EXIT_FAILURE;
    }

    const char *command = argv[1];

    if (strcmp(command, "status") == 0) {
        if (argc < 3) {
            printf("Hata: Eksik argüman.\nKullanım: vcfs status <dosya_yolu>\n");
            return EXIT_FAILURE;
        }
        return cmd_status(argv[2]);
    } else if (strcmp(command, "log") == 0) {
        if (argc < 3) {
            printf("Hata: Eksik argüman.\nKullanım: vcfs log <dosya_yolu>\n");
            return EXIT_FAILURE;
        }
        return cmd_log(argv[2]);
    } else if (strcmp(command, "checkout") == 0) {
        if (argc < 4) {
            printf("Hata: Eksik argüman.\nKullanım: vcfs checkout <dosya_yolu> <versiyon_id>\n");
            return EXIT_FAILURE;
        }
        __u32 version = (__u32)atoi(argv[3]);
        return cmd_checkout(argv[2], version);
    } else if (strcmp(command, "diff") == 0) {
        if (argc < 5) {
            printf("Hata: Eksik argüman.\nKullanım: vcfs diff <dosya_yolu> <v_id1> <v_id2>\n");
            return EXIT_FAILURE;
        }
        __u32 v1 = (__u32)atoi(argv[3]);
        __u32 v2 = (__u32)atoi(argv[4]);
        return cmd_diff(argv[2], v1, v2);
    } else if (strcmp(command, "trash") == 0) {
        if (argc < 3) {
            printf("Hata: Eksik argüman.\nKullanım: vcfs trash --list / --clean\n");
            return EXIT_FAILURE;
        }
        return cmd_trash(argv[2]);
    } else if (strcmp(command, "restore") == 0) {
        if (argc < 3) {
            printf("Hata: Eksik argüman.\nKullanım: vcfs restore <inode_no>\n");
            return EXIT_FAILURE;
        }
        return cmd_restore(argv[2]);
    } else {
        printf("Hata: Bilinmeyen komut '%s'\n", command);
        print_usage();
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
