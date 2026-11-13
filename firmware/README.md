# Software de Entretiempo

Firmware para la **WT32-SC01 Plus** con ESP32-S3, pantalla táctil de 480 × 320 y ranura microSD. En el conector de expansión, lleva A del EC11 a GPIO 10, B a GPIO 11, el pulsador a GPIO 12 y los comunes a GND. El firmware activa las resistencias pull-up. Si el giro queda invertido, intercambia A y B. [Ficha de la placa](https://docs.makehub.tw/wt32-sc01plus/WT32-SC01%2BPLUS%2BDatasheet-V1.5%2BEN.pdf)

## Primera carga con el firmware preparado

Si tienes `build/entretiempo-flash-completo.bin`, puedes cargar esa imagen sin recompilar. Incluye el cargador de arranque, la tabla de particiones y la aplicación. Contiene las credenciales locales del aparato.

Conecta la placa por USB-C con un cable de datos. Comprueba el puerto que aparece en `/dev/serial/by-id/` o `/dev/ttyACM*` y sustituye el ejemplo por ese puerto. Con el entorno de ESP-IDF activo, ejecuta desde la raíz del repositorio:

```sh
python -m esptool --chip esp32s3 --port /dev/ttyACM0 flash_id
python -m esptool --chip esp32s3 --port /dev/ttyACM0 write_flash 0x0 firmware/build/entretiempo-flash-completo.bin
idf.py -C firmware -p /dev/ttyACM0 monitor
```

Para el primer arranque, extrae `build/demo-microsd.zip` en la raíz de la microSD FAT32. La carpeta `entretiempo` debe quedar directamente en la tarjeta. La galería de `build/demo-microsd-5000.zip` sirve para la prueba de rendimiento y se prepara en una tarjeta o carpeta de pruebas aparte.

## Compilar y cargar

Desde la raíz del repositorio, con [ESP-IDF 5.5.6](https://github.com/espressif/esp-idf/releases/tag/v5.5.6) activo y Node.js instalado:

```sh
python3 tools/provision.py --name Daniela --birth 2005-11-14
npm --prefix firmware/web ci
npm --prefix firmware/web run build
idf.py -C firmware reconfigure build
idf.py -C firmware -p /dev/ttyACM0 flash monitor
```

Introduce la contraseña de configuración acordada cuando la pida el script. El panel abre directamente desde la red local. El script escribe un archivo local ignorado por Git. Los binarios personalizados incorporan el perfil y estas credenciales.

Conecta un cable USB-C de datos y usa el puerto que aparezca en el equipo. Si necesitas entrar en modo descarga, mantén BOOT, pulsa RESET y suelta BOOT. Si el USB de la unidad no expone un puerto de datos, usa la interfaz de depuración indicada en la ficha con el adaptador del fabricante o un adaptador UART TTL de **3,3 V**, TX y RX cruzados y GND común. Con un adaptador UART, selecciona el puerto correspondiente, por ejemplo `/dev/ttyUSB0`. [Guía de conexión serie de Espressif](https://docs.espressif.com/projects/esp-idf/en/v5.5.6/esp32s3/get-started/establish-serial-connection.html)

## Primera conexión

1. Conecta el móvil a `time_machine` y abre el aviso de la red. Si el sistema no abre el portal, visita `http://192.168.4.1`.
2. Desde el panel puedes añadir fotos con la microSD montada, cambiar el avatar y configurar dos redes de 2,4 GHz. Usa «Solo guardar» para registrar una red que esté fuera de alcance. El aparato prueba las redes guardadas al arrancar y vuelve a su red de configuración si falla la conexión. Tras conectarse, vuelve a poner el móvil en la misma red doméstica y abre por HTTP la dirección IP que aparece en Entretiempo.
3. Si el equipo pierde el acceso a la red, esperará un intento de reconexión antes de levantar su red de configuración. Mantén el pulsador aproximadamente dos segundos para activarla o cerrarla manualmente.

El panel se sirve por HTTP en la red local. La lectura de EXIF y la compresión se hacen en el navegador, con archivos de uno en uno. Si el navegador no puede decodificar un HEIC, expórtalo a JPEG para subirlo. La ubicación guarda un nombre editable y, si se han confirmado, las coordenadas GPS. El reloj se sincroniza por NTP en la red doméstica y toma la hora del navegador si arrancó sin fecha válida.

## Cargar y editar la microSD

Usa una tarjeta FAT32 y apaga Entretiempo antes de extraerla. Para importar imágenes desde el ordenador, instala Pillow 10 o posterior en tu entorno de Python.

```sh
python3 tools/sd_library.py /ruta/a/microsd --demo
python3 tools/sd_library.py /ruta/a/microsd ~/Fotos/*.jpg
```

Usa `--demo 5000` sobre una tarjeta de pruebas para comprobar el rendimiento con una colección grande. El modo demo incluye una nota larga y una foto sin fecha. Puedes usar `--date AAAA-MM-DD` al importar para fijar una fecha o `--no-gps` para omitir coordenadas.

Cada foto ocupa un JPEG y un JSON con el mismo nombre en `entretiempo/00` a `entretiempo/3f`. Al editar el JSON, conserva `id` y `rev`. Puedes cambiar `date` (AAAA-MM-DD o cadena vacía), `place`, `lat`, `lon` y `note` (hasta 4.096 bytes UTF-8). Para eliminar la foto directamente en la tarjeta, borra su JPEG y sus ficheros de ficha. La tarjeta se vuelve a leer al arrancar. El JPEG de la biblioteca es una copia adaptada a la pantalla.

## Navegar por las fotos

Al girar despacio, cada paso de la manivela avanza una sola foto, que gira como una tarjeta sobre un eje horizontal. Con giros continuos más rápidos, la distancia por paso crece progresivamente hasta 16 fotos. Tras una pausa o al invertir el giro, el primer paso vuelve a ser de una sola foto. La transición dura unos 145 ms y cambia la imagen cuando se ve de canto. El punto ámbar se desliza por la escala, con una marca por año y rótulos cada cinco años. Durante un giro rápido, el cursor sigue la selección y la foto final se muestra al frenar.

## Pruebas en la placa

- Comprueba la orientación de la pantalla y toca las cuatro esquinas. Verifica que un paso de la manivela mueve una foto y que un giro rápido recorre la escala.
- Con 5.000 fotos, mide el tiempo hasta el cambio de fecha y hasta la foto final. Los objetivos iniciales son 50 ms para la barra y 300 ms para la foto final tras parar. Repite mientras se sube una foto y al invertir el giro.
- Abre una nota larga con el pulsador, desplázala con la manivela y prueba escribir, guardar y cancelar en el teclado. Comprueba también el conflicto entre una ficha abierta en el navegador y una modificación desde la pantalla.
- Prueba una contraseña Wi-Fi incorrecta, recupera el portal y configura la red correcta. Comprueba NTP, la sincronización desde el móvil y la IP en pantalla.
- En una tarjeta de prueba, interrumpe una escritura y comprueba que el siguiente arranque recupera la última ficha válida. Haz también una edición desde el ordenador y comprueba que el arranque la recoge.
