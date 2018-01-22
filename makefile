

all:
	gcc -g -c -fPIC vector.c -o vector.o
	gcc -g -c -I/opt/scorep/include -fPIC measurement_blob.c -o measurement_blob.o
	gcc -g -c -I/opt/scorep/include -fPIC netstats_plugin.c -o libnetstats_plugin.o
	gcc -g -shared -Wl,-soname,libnetstats_plugin.so -o libnetstats_plugin.so libnetstats_plugin.o vector.o measurement_blob.o -lpthread
	
	
	

#gcc -o aufgabe-netstats main.c vector.c
#command from libsensors_plugin
#/usr/bin/cc  -fPIC -D_GNU_SOURCE -g -std=gnu99 -pthread  -shared -Wl,-soname,libsensors_plugin.so -o libsensors_plugin.so CMakeFiles/sensors_plugin.dir/sensors_plugin.c.o  -L/home/jitschin/git/scorep_plugin_libsensors -lpthread -lsensors -Wl,-rpath,/home/jitschin/git/scorep_plugin_libsensors: 
