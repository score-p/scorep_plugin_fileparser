

all:
	gcc -g -c -fPIC vector.c -o vector.o
	gcc -g -c -I/opt/scorep/include -fPIC measurement_blob.c -o measurement_blob.o
	gcc -g -c -I/opt/scorep/include -fPIC fileparser_plugin.c -o libfileparser_plugin.o
	gcc -g -shared -Wl,-soname,libfileparser_plugin.so -o libfileparser_plugin.so libfileparser_plugin.o vector.o measurement_blob.o -lpthread
	
	
	

#command from libsensors_plugin
#/usr/bin/cc  -fPIC -D_GNU_SOURCE -g -std=gnu99 -pthread  -shared -Wl,-soname,libsensors_plugin.so -o libsensors_plugin.so CMakeFiles/sensors_plugin.dir/sensors_plugin.c.o  -L$HOME/git/scorep_plugin_libsensors -lpthread -lsensors -Wl,-rpath,$HOME/git/scorep_plugin_libsensors: 
