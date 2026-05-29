# VCFS (Version Control File System) Mimari ve Geliştirme Dokümanı

Bu doküman, Linux İşletim Sistemleri için dosya bazında versiyon kontrolüne sahip bir dosya sistemi (VCFS) geliştirilmesi amacıyla yapılan tüm mimari ve kodlama çalışmalarını özetlemektedir. Tezde belirtilen hedeflere ulaşmak için proje dört ana bileşen üzerinden inşa edilmiştir: **Çekirdek Modülü (Kernel Space)**, **Optimizasyon Servisi (Daemon)**, **Komut Satırı Arayüzü (CLI)** ve **Grafiksel Kullanıcı Arayüzü (Native GUI)**.

Her bir bileşen için hem yüksek seviyeli (yüzeysel) konseptler hem de düşük seviyeli (derin teknik) uygulama detayları aşağıda sunulmuştur.

---

## 1. Linux Çekirdek Modülü (Kernel Space)
**Dizin:** `dev-env/src-vcfs/`

### Yüzeysel Bakış (High-Level)
Standart dosya sistemleri (ext4, fat32 vb.) bir dosyanın üzerine yeni veri yazıldığında eski veriyi sonsuza dek siler. VCFS'in çekirdeği ise, bir dosya değiştirilmek veya silinmek istendiğinde eski halinin bir anlık görüntüsünü (snapshot) alır ve bunu gizli bir geçmiş zincirine ekler. Ayrıca kullanıcı dosyayı sildiğinde tamamen yok etmek yerine "Çöp Kutusu" (Trash) benzeri bir mantıkla dosyayı sadece görünmez yapar.

### Teknik ve Mimari Detaylar (Deep Technical)
* **VFS Entegrasyonu ve Inode Tasarımı:**
  Linux Sanal Dosya Sistemi (VFS) ile konuşabilmek için `simplefs` referans alınmış, ancak inode yapıları modifiye edilmiştir. `vcfs.h` içerisinde yer alan disk üzerindeki `struct vcfs_inode` yapısına şu "native" alanlar eklendi:
  * `uint32_t version_id`: Artımlı versiyon numarası.
  * `uint32_t version_timestamp`: Versiyonun oluşturulma zaman damgası (Time-shifting için).
  * `uint32_t prev_version_inode`: Geçmiş versiyonları birbirine bağlayan bağlı liste (Linked List) göstericisi.
  * `uint32_t is_deleted`: Çöp kutusu (Trash) mekanizması bayrağı.
  Blok hizalaması (alignment) bozulmasın diye yapı `128 byte`'a tamamlanacak şekilde `padding` eklendi. Böylece kernel panic ve bellek sızıntılarının önüne geçildi.

* **Extent-Level Copy-on-Write (CoW):**
  Git benzeri tam kopya (snapshot) yerine, disk IO'sunu azaltmak için `file.c` içerisindeki `vcfs_write` ve `vcfs_file_get_block` fonksiyonları değiştirildi. Bir veri bloğuna yazma isteği geldiğinde, modül o extent'in (veri bloğu dizisinin) `version_id`'sini kontrol eder. Eğer extent eski bir versiyona aitse, mevcut verinin **üzerine yazmak yerine (overwrite engellendi)** yeni bir blok tahsis edilir, eski veri yeni bloğa kopyalanır ve yazma işlemi yeni blok üzerinde yapılır.

* **Snapshot (Historical Inode) Yaratımı:**
  `vcfs_open` fonksiyonu içinde `O_WRONLY` (yazma) bayrağı tespit edildiğinde, `vcfs_create_version()` tetiklenir. Bu fonksiyon yeni bir inode alır, mevcut dosyanın metadata'sını ve blok referanslarını buraya kopyalar (backup) ve güncel dosyanın `prev_version_inode` alanını bu yeni kopya inode'a yönlendirerek tarihi bir zincir oluşturur.

---

## 2. Kullanıcı Alanı Optimizasyon Servisi (Daemon)
**Dizin:** `dev-env/vcfs-daemon/`

### Yüzeysel Bakış (High-Level)
Geçmiş versiyonları sürekli tutmak disk alanını hızlıca doldurabilir. Bu sorunu çözmek için arka planda sessizce çalışan bir servis (daemon) yazılmıştır. Bu servis, belirli aralıklarla diski tarar, eski versiyonları bulur ve diskte yer açmak için bunları sıkıştırır (Delta Compression). Kullanıcının hiçbir şeye tıklamasına veya komut girmesine gerek kalmaz.

### Teknik ve Mimari Detaylar (Deep Technical)
* **Daemonization ve Yaşam Döngüsü:**
  `vcfsd.c`, C dilinde standart UNIX daemon pratiklerine uygun olarak yazılmıştır. `fork()` edilip parent süreç kapatılır, `setsid()` ile yeni bir oturum başlatılır ve `umask(0)` ile dosya izinleri sıfırlanır. Syslog üzerinden sistem kayıtlarına (log) yazma yapar.
* **Kernel-Space İletişimi (IOCTL):**
  Sıkıştırma işlemi (zlib) işlemciyi çok yoracağından Kernel içinde yapılmamalıdır. Bu yüzden Daemon, User-Space'te çalışır ve Kernel ile `vcfs_ioctl.h` başlık dosyasında tanımlanan protokoller (IOCTL Magic Number: `v`) aracılığıyla haberleşir.
  * `VCFS_IOC_GET_VERSION_COUNT`: Tarama esnasında dosyanın versiyon sayısını çekirdekten okur.
  * `VCFS_IOC_COMPRESS_VERSION`: Thinning (seyreltme/sıkıştırma) politikasına uyan eski versiyonları kernel'e bildirir.
* **Periyodik Tarama (Recursive Directory Traversal):**
  Daemon, kendisine parametre olarak geçilen mount noktasını (ör. `/mnt/vcfs`) DFS (Depth-First Search) mantığı ile `readdir()` kullanarak tarar ve inode'lara sistem çağrıları (syscall) ile erişir.

---

## 3. Komut Satırı Arayüzü (CLI)
**Dizin:** `dev-env/vcfs-cli/`

### Yüzeysel Bakış (High-Level)
Teknik kullanıcıların (sistem yöneticileri, yazılımcılar vb.) dosya geçmişlerini terminal (konsol) üzerinden yönetebilmesi için Git benzeri bir araçtır. `vcfs log dosya.txt` yazarak dosyanın tüm geçmişini görebilir veya `vcfs checkout dosya.txt 2` yazarak dosyayı anında eski haline döndürebilirsiniz.

### Teknik ve Mimari Detaylar (Deep Technical)
* **POSIX Standartları ve C İmplementasyonu:**
  Uygulama, Linux otomasyon script'leriyle (bash, awk vb.) uyumlu çalışabilmesi için saf C dilinde yazılmıştır.
* **Bileşen Komutları (Sub-commands):**
  * `cmd_status`: `open()` ile alınan File Descriptor üzerinden Kernel'e `VCFS_IOC_GET_VERSION_COUNT` ioctl sinyali gönderir.
  * `cmd_log`: Kernel içerisinden dönen binary array verisini ( `struct vcfs_ioctl_version_info` ), User-Space tarafında `malloc` ile karşılar. Dosya boyutu ve tarih bilgilerini formatlayarak kullanıcıya tablo halinde sunar. Aktif (güncel) versiyonu ayrıca belirtir.
  * `cmd_checkout`: `VCFS_IOC_CHECKOUT_VERSION` ioctl sinyali ile doğrudan Kernel'e hedef ID'yi iletir. (Kernel tarafında `file.c` içerisindeki `vcfs_unlocked_ioctl` bu sinyali yakalayıp inode swap işlemini yapar).
  * `cmd_diff`: İki farklı versiyonu `VCFS_IOC_READ_VERSION` IOCTL'i ile kernel üzerinden 4KB'lık veri blokları halinde okur, geçici dosyalara (temp file) aktarır ve sistemdeki yerleşik `diff` komutunu kullanarak iki sürüm arasındaki farkı konsola yazdırır.
  * `cmd_trash`: Dizin (Directory) seviyesinde açılan dosya tanımlayıcısı ile `VCFS_IOC_GET_TRASH_LIST` veya `VCFS_IOC_CLEAN_TRASH` çağrılarını tetikler. Kernel, inode tablosunu tarayarak `is_deleted == 1` bayrağı taşıyan ve orijinal dosya adını koruyan kayıtları kullanıcıya liste olarak sunar.
  * `cmd_restore`: Hedeflenen çöp dosyasına ait Inode numarasını alarak `VCFS_IOC_RESTORE_TRASH` çağrısı yapar. Kernel, dizin bloğuna (dentry) bu inode numarasını ve orijinal dosya adını tekrar bağlayarak (link_inode) silinmiş dosyayı geri getirir.

---

## 4. Native Grafiksel Kullanıcı Arayüzü (GUI)
**Dizin:** `dev-env/vcfs-gui/`

### Yüzeysel Bakış (High-Level)
Terminal kullanmayı bilmeyen veya tercih etmeyen normal kullanıcılar için geliştirilmiş, Apple Time Machine benzeri görsel bir penceredir. Sol tarafta dosyaları görebilir, ortada o dosyanın versiyonlarının ne zaman oluşturulduğunu görebilir ve tek tıkla eski halini önizleyip geri yükleyebilirsiniz. Python gibi ağır diller yerine direkt C ile yazıldığı için çok hafiftir, bilgisayarı kasmaz.

### Teknik ve Mimari Detaylar (Deep Technical)
* **Zero-Overhead (Sıfır Yük) GTK3 Mimarisi:**
  Tezde vurgulanan "daha az kaynak tüketimi" hedefini %100 sağlamak adına Python (PyQt) gibi yorumlayıcı barındıran yapılar yerine saf C dili ve GTK+ kütüphanesi tercih edilmiştir. Böylece uygulama milisaniyeler içerisinde başlar ve OS seviyesinde native (doğal) widget'lar kullanır. Bellek kullanımı sadece 10-20 MB bandında kalır.
* **Layout ve Signal/Slot Mekanizması:**
  Arayüz, `GtkPaned` kullanılarak 3 ana sütuna (Tree View, List Box, Text View) bölünmüştür.
  * `GtkTreeView`: Sol panelde dosya ağacını tutar. `GtkTreeStore` model-view-controller mimarisine uygun tasarlanmıştır.
  * `GtkListBox`: Orta panelde her versiyon için bir row (satır) üretir. Bu satırlar dinamik olarak bir `GtkBox` içine etiketler ve Checkout butonu konularak yerleştirilir.
* **Direct IOCTL Entegrasyonu:**
  Uygulama C ile yazıldığı için, CLI aracında kullanılan Kernel IOCTL başlıkları doğrudan GUI koduna (`main.c`) include edilebilmektedir. Bu, araya köprü kütüphaneleri konulmasını (JNI, C-Types vb.) engeller ve maksimum performansta Kernel-to-GUI (Çekirdekten-Arayüze) veri akışı sağlar.

---

## Sonuç Özeti
Bu proje mimarisi ile sıradan bir ext4/fat sisteminin üzerine katman eklemek yerine; "Inode tabanlı geçmiş zinciri" (Linked-list Inode) ve "Alan Paylaşımlı" (Copy-on-Write) mekanizmaları **doğrudan çekirdek (kernel) içine yedirilmiştir.** İşlemciyi yoran sıkıştırma (compression) süreci arka plana (Daemon) atılarak darboğazlar engellenmiş; CLI ve GTK GUI sayesinde her türden kullanıcının sisteme rahatça erişebilmesi garanti altına alınmıştır.

---

## 5. Yaşanan Problemler ve Çözümleri

Geliştirme ve test süreçlerinde karşılaşılan başlıca teknik zorluklar ve bu zorluklara getirilen köklü çözümler aşağıda listelenmiştir:

* **Initramfs ve QEMU Mount Sorunları:** 
  İlk mimaride, çekirdek modülleri ve CLI araçları harici bir ext2 disk imajı (`modules.img`) üzerinden QEMU sanal makinesine aktarılıyordu. Ancak QEMU'nun minimalist yapısı nedeniyle bu imajın bağlanması (mount) sırasında "Invalid argument" ve "Not a directory" hataları alındı. Çözüm olarak, test betiği (`qemu-setup.sh`) tamamen revize edildi ve derlenen modüller doğrudan QEMU'nun RAM diskine (initramfs) gömüldü. Bu sayede harici disklere olan bağımlılık ortadan kalktı.
* **Derleme ve Tip Tanımlaması Eksiklikleri:** 
  Kullanıcı alanı (User-Space) olan CLI aracı derlenirken `__u32` gibi çekirdek tipleri tanınmadığı için hata alındı. `<linux/types.h>` kütüphanesinin CLI arayüzüne dahil edilmesiyle bu sorun aşıldı. Ayrıca çekirdek seviyesindeki `implicit declaration` hatalarını çözmek için `bitmap.h` eklendi.
* **Epoch Zaman (1970) ve Boş Snapshot Mantıksal Hatası (Timestamp & Empty Snapshot Bug):** 
  Yeni oluşturulan bir dosya kaydedildiğinde, `version_timestamp` değerinin set edilmesi unutulduğu için ilk versiyonun tarihi UNIX Epoch (1 Ocak 1970) olarak görünüyordu. Ayrıca, dosya ilk kez oluşturulmak üzere açıldığında, henüz içi boşken (0 byte) anlık görüntüsü alınıyordu. Çekirdekteki `vcfs_create_version` mantığı `if (ci->version_id == 0 && inode->i_size == 0) return 0;` şartı ile güncellenerek gereksiz boş snapshot oluşumu engellendi ve zaman damgası güncellemeleri düzeltildi.
* **Çöp Kutusu (Trash) Eviction Hatası:** 
  Bir dosya `rm` komutu ile dizinden silindiğinde, standart Linux VFS mimarisi `i_nlink` (bağlantı sayısı) sıfıra düştüğü an inode'u ve veri bloklarını fiziksel olarak tamamen siliyordu (`evict_inode`). Bu nedenle `vcfs trash --list` boş dönüyordu. Çözüm olarak `vcfs_unlink` fonksiyonuna müdahale edildi; dosya dizinden koparıldı ancak `i_nlink` sıfıra indirilmedi ve sadece `is_deleted = 1` bayrağı set edildi. Böylece VFS'in Trash'teki dosyaları imha etmesi Kernel seviyesinde engellendi.
* **CLI Restore Komutu Argüman Uyuşmazlığı:** 
  Silinen dosyalar mevcut dizin ağacında bulunmadığı için dosya adı (filename) üzerinden değil, Inode Numarası üzerinden geri yüklenmeliydi (örn: `vcfs restore 3`). CLI'ın yardım menüsünde yanlışlıkla `<dosya_yolu>` parametresi istendiği belirtildiği için kullanıcılar "Invalid argument" hatası yaşıyordu. CLI parametre kontrol mekanizması düzeltilerek menü uyumlu hale getirildi.
* **QEMU ve Busybox Autocomplete Kısıtlaması:** 
  Kullanıcı, CLI komutları için (örn: `vcfs c` -> `checkout`) tab ile otomatik tamamlama (autocomplete) beklemekteydi. Ancak QEMU içindeki hafif siklet Busybox `ash` kabuğu (shell), standart Bash'teki alt-komut tamamlama (bash-completion) eklentisine sahip değildir. Bu nedenle tamamlama yalnızca ana program isimlerinde çalışır, bu durum bir hata değil ortam kısıtlamasıdır.
