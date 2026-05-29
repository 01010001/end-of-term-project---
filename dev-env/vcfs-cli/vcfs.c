#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <linux/types.h>
#include <errno.h>
#include <time.h>

/* Kernel modülü ile aynı IOCTL tanımlamalarını kullanıyoruz */
#define VCFS_IOCTL_MAGIC 'v'

#define VCFS_IOC_GET_VERSION_COUNT _IOR(VCFS_IOCTL_MAGIC, 1, __u32)

struct vcfs_ioctl_version_info {
    __u32 version_id;
    __u32 timestamp;
    __u32 is_compressed;
};
#define VCFS_IOC_GET_VERSIONS _IOWR(VCFS_IOCTL_MAGIC, 2, struct vcfs_ioctl_version_info)
#define VCFS_IOC_CHECKOUT_VERSION _IOW(VCFS_IOCTL_MAGIC, 4, __u32)
#define VCFS_IOC_RESTORE_TRASH _IO(VCFS_IOCTL_MAGIC, 5)

void print_usage() {
    printf("VCFS (Version Control File System) CLI Araçları\n");
    printf("Kullanım:\n");
    printf("  vcfs log <dosya_yolu>                   : Dosyanın versiyon geçmişini gösterir.\n");
    printf("  vcfs checkout <dosya_yolu> <versiyon>   : Dosyayı belirtilen versiyona döndürür.\n");
    printf("  vcfs status <dosya_yolu>                : Dosyanın anlık versiyon durumunu gösterir.\n");
    printf("  vcfs restore <dosya_yolu>               : Çöp kutusundan dosyayı geri getirir.\n");
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

    /* Versiyon dizisini Kernel'den çekiyoruz */
    struct vcfs_ioctl_version_info *versions = malloc(sizeof(struct vcfs_ioctl_version_info) * count);
    if (!versions) {
        perror("Bellek hatası");
        close(fd);
        return -1;
    }

    /* Argüman olarak dizinin kendisini (pointer) veriyoruz */
    if (ioctl(fd, VCFS_IOC_GET_VERSIONS, versions) < 0) {
        perror("Versiyon detayları okunamadı");
        free(versions);
        close(fd);
        return -1;
    }

    printf("Versiyon Geçmişi: %s\n", filepath);
    printf("------------------------------------------------------------------------\n");
    printf("Versiyon ID | Tarih               | Sıkıştırılmış mı? | Durum\n");
    printf("------------------------------------------------------------------------\n");

    for (__u32 i = 0; i < count; i++) {
        time_t ts = versions[i].timestamp;
        struct tm *tm_info = localtime(&ts);
        char time_buf[26];
        strftime(time_buf, 26, "%Y-%m-%d %H:%M:%S", tm_info);

        printf("v%-10u | %-19s | %-17s | %s\n", 
            versions[i].version_id, 
            time_buf, 
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

int cmd_restore(const char *filepath) {
    /* Çöp kutusundaki dosyalar genellikle is_deleted flag'i ile işaretlenmiştir.
     * Bu dosyaya erişmek için özel bir Kernel flag'i ile open() yapmak gerekir,
     * ya da dizin seviyesinde bir ioctl çağrısı yapılır.
     * Basit bir POC olarak, dizin üzerinden IOCTL çağrısı farzediyoruz. */
     
    char dirpath[1024];
    strcpy(dirpath, filepath);
    char *last_slash = strrchr(dirpath, '/');
    const char *filename = filepath;

    if (last_slash != NULL) {
        *last_slash = '\0';
        filename = last_slash + 1;
    } else {
        strcpy(dirpath, ".");
    }

    int dir_fd = open(dirpath, O_RDONLY | O_DIRECTORY);
    if (dir_fd < 0) {
        perror("Dosyanın bulunduğu dizin açılamadı");
        return -1;
    }

    /* Filename'i kernel'e bildirmek için ioctl argümanı kullanıyoruz */
    if (ioctl(dir_fd, VCFS_IOC_RESTORE_TRASH, filename) < 0) {
        perror("Geri getirme (Restore) işlemi başarısız");
        close(dir_fd);
        return -1;
    }

    printf("Başarılı: %s dosyası çöp kutusundan geri yüklendi.\n", filename);
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
    } else if (strcmp(command, "restore") == 0) {
        if (argc < 3) {
            printf("Hata: Eksik argüman.\nKullanım: vcfs restore <dosya_yolu>\n");
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
