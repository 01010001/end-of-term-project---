# VCFS - Hedef Linux Sistemleri Kurulum ve Dağıtım Rehberi

Bu doküman, VCFS (Version Control File System) projesinin test ortamı olan Docker/QEMU dışında, gerçek bir Linux işletim sistemine (Fedora, Ubuntu, Debian vb.) nasıl derlenip kurulacağını ve kalıcı hale getirileceğini adım adım açıklamaktadır. 

**Önemli Not:** Bu işlemler projenin kaynak kodlarına dokunmaz, yalnızca var olan kodları hedef sistemin Kernel sürümüne göre derleyerek uygun sistem klasörlerine yerleştirir.

---

## 1. Sistem Gereksinimleri ve Bağımlılıkların Kurulması

VCFS bileşenlerinin (Kernel Modülü, CLI, Daemon ve GUI) gerçek bir makinede derlenebilmesi için C derleyicisine, güncel Linux Kernel başlık dosyalarına (headers) ve GTK3/zlib gibi kütüphanelere ihtiyaç vardır.

**Fedora / RHEL Tabanlı Sistemler İçin:**
```bash
sudo dnf update
sudo dnf install gcc make git kernel-devel kernel-headers zlib-devel gtk3-devel
```

**Ubuntu / Debian Tabanlı Sistemler İçin:**
```bash
sudo apt update
sudo apt install build-essential linux-headers-$(uname -r) zlib1g-dev libgtk-3-dev
```

---

## 2. Linux Çekirdek Modülünün (Kernel Module) Kurulumu

Çekirdek modülleri, her zaman hedef makinenin **kendi** Kernel versiyonuna göre derlenmelidir.

1. `src-vcfs` dizinine gidin ve modülü derleyin:
```bash
cd dev-env/src-vcfs
make clean
make
```

2. Modülü canlı çekirdeğe yükleyin ve test edin:
```bash
sudo insmod vcfs.ko
lsmod | grep vcfs
```
*(Eğer bir hata alırsanız, `dmesg | tail` komutu ile Kernel loglarını kontrol edebilirsiniz.)*

3. Format aracını (`mkfs.vcfs`) sistem yoluna kopyalayarak her yerden erişilebilir yapın:
```bash
sudo cp mkfs.vcfs /usr/local/sbin/
sudo chmod +x /usr/local/sbin/mkfs.vcfs
```

**(Opsiyonel) Kalıcı Yükleme:**
Sistem her yeniden başladığında modülün otomatik yüklenmesini istiyorsanız `.ko` dosyasını sistem modül klasörüne kopyalayıp bağımlılıkları güncelleyin:
```bash
sudo cp vcfs.ko /lib/modules/$(uname -r)/kernel/fs/
sudo depmod -a
echo "vcfs" | sudo tee /etc/modules-load.d/vcfs.conf
```

---

## 3. Komut Satırı Aracının (CLI) Kurulumu

Kullanıcıların `vcfs log`, `vcfs checkout` gibi komutları herhangi bir dizindeyken çalıştırabilmesi için uygulamanın `/usr/local/bin` dizinine taşınması gerekir.

1. CLI aracını derleyin:
```bash
cd dev-env/vcfs-cli
make clean
make
```

2. Çalıştırılabilir dosyayı sisteme kurun:
```bash
sudo cp vcfs /usr/local/bin/
sudo chmod +x /usr/local/bin/vcfs
```

Kurulumu doğrulamak için terminalinize `vcfs` yazmanız yeterlidir. Menü karşınıza çıkacaktır.

---

## 4. Optimizasyon Servisinin (Daemon) Kurulumu ve Systemd Entegrasyonu

Daemon aracı (`vcfsd`), sistemin arka planında sürekli çalışması gereken bir servistir. Bunu Linux'un modern servis yöneticisi olan **Systemd** ile kurgulamak en sağlıklı yöntemdir.

1. Daemon aracını derleyin:
```bash
cd dev-env/vcfs-daemon
make clean
make
```

2. Çalıştırılabilir dosyayı sisteme kurun:
```bash
sudo cp vcfsd /usr/local/sbin/
sudo chmod +x /usr/local/sbin/vcfsd
```

3. **Systemd Servisi Oluşturma:**
`vcfsd`'nin otomatik başlaması ve çökmesi durumunda tekrar ayağa kalkması için bir servis dosyası oluşturun:
```bash
sudo nano /etc/systemd/system/vcfsd.service
```

İçerisine şu satırları yapıştırın (VCFS'yi `/mnt/vcfs_disk` dizinine mount ettiğinizi varsayıyoruz):
```ini
[Unit]
Description=VCFS Optimization and Compression Daemon
After=local-fs.target

[Service]
Type=forking
ExecStart=/usr/local/sbin/vcfsd /mnt/vcfs_disk
Restart=on-failure
RestartSec=5s

[Install]
WantedBy=multi-user.target
```

4. Servisi etkinleştirin ve başlatın:
```bash
sudo systemctl daemon-reload
sudo systemctl enable vcfsd
sudo systemctl start vcfsd
```
Logları kontrol etmek için: `sudo journalctl -u vcfsd -f`

---

## 5. Grafiksel Kullanıcı Arayüzünün (GUI) Derlenmesi ve Çalıştırılması

Masaüstü ortamı (GNOME, XFCE vb.) kurulu bir Linux makinesinde GUI'yi derleyip çalıştırabilirsiniz.

1. GUI aracını derleyin:
```bash
cd dev-env/vcfs-gui
make clean
make
```

2. Uygulamayı başlatın:
```bash
./vcfs-gui
```

**(Opsiyonel) Masaüstü Kısayolu Oluşturma:**
Uygulamayı başlatlatıcıya (Uygulamalar menüsü) eklemek için bir `.desktop` dosyası oluşturabilirsiniz:
```bash
nano ~/.local/share/applications/vcfs-gui.desktop
```

İçerisine şu bilgileri ekleyin:
```ini
[Desktop Entry]
Name=VCFS Time Machine
Comment=Version Control File System GUI
Exec=/tam/yol/dev-env/vcfs-gui/vcfs-gui
Terminal=false
Type=Application
Categories=Utility;System;
```

---

## Gerçek Sistemde Diski Mount Etme

Modülleri kurduktan sonra gerçek bilgisayarınızda (örneğin `/dev/sdb` isminde boş bir diskiniz veya USB belleğiniz varsa) şu şekilde mount edebilirsiniz:

```bash
# 1. Formatla
sudo mkfs.vcfs /dev/sdb

# 2. Bağlama noktasını oluştur ve bağla
sudo mkdir -p /mnt/vcfs_disk
sudo mount -t vcfs /dev/sdb /mnt/vcfs_disk

# 3. İzinleri ayarla (Opsiyonel)
sudo chown -R $USER:$USER /mnt/vcfs_disk
```

Artık VCFS, ana bilgisayarınızda bir USB bellek veya harici disk gibi native olarak çalışmaya hazırdır!
