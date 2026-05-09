sistemler.com CID-v6 cihazları için açık kaynaklı linux driver'ı


yapma sebebim: sistemi linuxa geçirirken sistemler.com un sağladığı dll sadece windowsta çalışıyordu.
bu yüzden tersine mühendislikle usb capturelarını inceleyerek, bu repoyu oluşturdum.



kullanım:

releases kısmından en son sürüü indirip kurun.

cidv6d servisinin çalıştığından emin olun.

bilgisayar açılır açılmaz servir çalışmaya başlayıp gelen aramaları
```
/run/cidv6d.sock
```
dosyasına 
```json
{"timestamp":"2026-05-09T04:55:45+0300","line":1,"number":"05123456789","name":""}
```
formatında gönderecektir.

bu socketa bağlanmak için programı çalıştıran kullanıcının `cidv6d` grubunda bulunduğundan emin olun.

örnek:
```bash
sudo usermod -aG cidv6d kullanici
```
