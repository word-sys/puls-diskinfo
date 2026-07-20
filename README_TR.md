# PULS DiskInfo

Linux için GTK4 ve C ile geliştirilmiş, profesyonel düzeyde kapsamlı bir depolama sağlığı ve S.M.A.R.T. izleme uygulaması. CrystalDiskInfo'dan ilham alınarak tasarlanmış olup kritik sağlık durumunu, sıcaklıkları ve ayrıntılı sürücü niteliklerini görüntüler.

---

## Özellikler

- **Çoklu Sürücü Desteği**: Birden fazla sürücüyü (NVMe SSD, SATA SSD, SSHD, HDD, USB vb.) listeler ve izler.
- **Sağlık Göstergeleri**: S.M.A.R.T. niteliklerine dayalı net, renk kodlu sağlık rozetleri (İyi, Dikkat, Kötü).
- **Gerçek Zamanlı Sıcaklık**: Güvenli aralıklarla dinamik sıcaklık termometre göstergesi.
- **S.M.A.R.T. Nitelikleri Tablosu**: Tüm standart niteliklerin renk kodlu, sıralanabilir listesi.
- **NVMe'ye Özgü Günlükler**: NVMe SSD'ler için kapsamlı sağlık ölçümleri (kullanılabilir yedek, kullanım yüzdesi, medya hataları vb.).
- **Polkit Entegrasyonu**: Temiz ayrıcalık ayrımı. Ana GUI ayrıcalıksız çalışır; S.M.A.R.T. verilerini sorgularken `pkexec` aracılığıyla küçük, denetlenebilir bir yardımcı çalıştırılabilir dosya çağrılır.
- **Türkçe / İngilizce Dil Desteği**: Başlık çubuğundaki dil düğmesiyle anında geçiş yapılabilir; tercih kaydedilir.

---

## Derleme Bağımlılıkları (Ubuntu 22.04+)

Aşağıdaki paketlerin kurulu olduğundan emin olun:

```bash
sudo apt update
sudo apt install -y \
  build-essential \
  meson \
  ninja-build \
  pkg-config \
  libgtk-4-dev \
  libadwaita-1-dev \
  libjson-glib-dev \
  smartmontools
```

---

## Derleme ve Çalıştırma

### 1. Derleme Yapılandırması
Meson kullanarak derleme dizinini oluşturun:
```bash
meson setup build
```

### 2. Derleme
Uygulamayı derleyin:
```bash
meson compile -C build
```

### 3. Yerel Çalıştırma
Derlenmiş ikili dosyayı doğrudan çalıştırın:
```bash
./build/src/puls-diskinfo
```

---

## Kurulum ve Paketleme

### Seçenek A: Taşınabilir Sürüm Arşivi
Yardımcı ikili dosyaları, masaüstü entegrasyonu ve kurma/kaldırma betiklerini içeren bağımsız bir sürüm paketi oluşturabilirsiniz:

```bash
./packaging/build-binary.sh
```
Arşiv `dist/puls-diskinfo-1.1.2-linux-x86_64.tar.gz` konumuna kaydedilir.

Arşivden kurulum için:
```bash
tar xzf dist/puls-diskinfo-1.1.2-linux-x86_64.tar.gz
cd puls-diskinfo-1.1.2-linux-x86_64
sudo ./install.sh
```

### Seçenek B: Debian Paketi (.deb)
```bash
./packaging/build-deb.sh
```

---

## Lisans

Bu proje GPL-3.0-or-later Lisansı kapsamında lisanslanmıştır. Ayrıntılar için `LICENSE` dosyasına bakın.
