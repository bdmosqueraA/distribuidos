# distribuidos
el archivo distri.tar.gz es la misma carpeta pero comprimida.  
los comandos para ejecutar son los siguientes:  
se dirige a la ruta y crea la carpeta ejemplo: /home/user/Documentos/distri y descarga ahi los archivos.
en la consola se dirige a la ruta y ejecuta los camandos
  
#gcc -pthread -o server server.c -lcrypto
#gcc -pthread -o worker worker.c -lcrypto
luego en dos terminales ejecuta los comandos 
#./server 9000 archivo.txt 3 50000
y en la otra
#./worker 127.x.x.x 9000

9000 es el puerto donde se comunican, se puede cambiar a placer, 127.x.x.x cambia las x por la direccion ip del servidor, varia segun el pc
