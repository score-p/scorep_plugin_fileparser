# Fileparser Plugin

Fileparser Plugin is a shared library to be used with Score-P. It is capable of logging any value available via standard FileDescriptors on a machine. I.e. one can log:
* the CPU clock rate from /proc/cpuinfo
* the current temperature of CPU core 0 from /sys/class/hwmon/hwmon0/temp1_input
* the current count of bytes downloaded from /proc/net/netstat

and many more parameters

---
### Dependencies
* Score-P and gcc
* libpthread
* GNU make

### Compilation

Just run `make`, really that is sufficient.

Or alternatively, if you like it more complicated use cmake
```
mkdir build
cd build
cmake ..
make
```
---
### Example usage
For the following example the compiled `libfileparser_plugin.so` and the program that you used scorep on need to be in the same directory.
```
export SCOREP_METRIC_PLUGINS="fileparser_plugin"
export LD_LIBRARY_PATH="$PWD"
export SCOREP_ENABLE_TRACING=true
export SCOREP_ENABLE_PROFILING=false
export SCOREP_METRIC_FILEPARSER_PLUGIN_PERIOD=10000
export SCOREP_METRIC_FILEPARSER_PLUGIN="CPU Clock 1:double@/proc/cpuinfo+c=2;r=7;s= ;p,CPU Clock 2:double@/proc/cpuinfo+c=2;r=34;s= ;p,CPU Clock 3:double@/proc/cpuinfo+c=2;r=61;s= ;p,CPU Clock 4:double@/proc/cpuinfo+c=2;r=88;s= ;p,Load AVG:double@/proc/loadavg+c=1;r=0;s= ,MemFree:int@/proc/meminfo+c=1;r=1;s= ,netstat:int@/proc/net/netstat+c=7;r=3s= ;d,dev    :int@/proc/net/dev+c=1;r=3;d,Core#0Temp:int@/sys/class/hwmon/hwmon0/temp1_input+c=0;r=0;p,Core#1Temp:int@/sys/class/hwmon/hwmon1/temp1_input+;p"

export SCOREP_EXPERIMENT_DIRECTORY="scorep_fileparser_trace"

./my-program-which-was-compiled-using-scorep.bin
```

# Parameters to Fileparser Plugin
fileparser plugin takes parameters from either of these variables:
* SCOREP_METRIC_FILEPARSER_PLUGIN
* SCOREP_METRIC_FILEPARSER_PLUGIN_PERIOD

The latter can be given a time in microseconds denoting the length of the intervals at which a value will be read and logged.
The former defines which values are being read.

The format definition for **SCOREP_METRIC_FILEPARSER_PLUGIN** is as follows:

```
SCOREP_METRIC_FILEPARSER_PLUGIN=<variable>[','<variable>]*
<variable> = [ <variablename> ':' ] [ <field-datatype> '@' ] <path-to-file> '+' <field-declaration>
<variablename> = any char except ',' ':' and '\0'
<field-datatype> = ( int | int_hex | uint | uint_hex | float | double | <binary-datatype> ) '@'
<binary-datatype> = int8_bin | int16_bin | int32_bin | int64_bin | uint8_bin | uint16_bin | uint32_bin | uint64_bin | float_bin | double_bin
<path-to-file> = path to the file, may not contain '+' nor ','
<field-declaration> = <field-parameter> [';'<field-parameter>]*
<field-parameter> = (('C' | 'c' | 'R' | 'r' | 'L' | 'l' | 'B' | 'b')  '=' <field-value> ) | ('S' | 's' = <field-separator>) | 'D' | 'd' | 'P' | 'p' | 'A' | 'a'
<field-value> = ('1' | '2' | '3' | '4' | '5' | '6' | '7' | '8' | '9' | '0')*
<field-separator> = any char except ';' or ','
```
Wherein `variablename` denotes the name the recorded logging data will be assigned. The logging data will be registered under such a name to Score-P and consequently will be shown under that name in the metric selection of any GUI displaying Score-P traces.

**Note** that datatypes int_hex and uint_hex will assume the input data is in form hexadecimal (to the base 16).
`field-datatype` will be interpreted towards either uint64_t, double or int64_t (default). All the read/logged values of one variable definition will be parsed as that datatype. This means that read values will be truncated according to the specified datatype.
`field-parameter` are additional parameters. Such may be a specification of either:
* `r`/`R`/`l`/`L` to specify a line number(i.e. row)
* `C`/`c` to specify the field number (i.e. column) in a line
* `S`/`s` to specify the inter-field-separator/delimiter of columns in a line
* `B`/`b` to specify an offset in bytes if a binary read is to be performed, i.e. a binary-datatype has been specified
* `D`/`d` to specify that an initial value should be read and subsequential reads be logged as offsets to the initial value
* `P`/`p` to specify that the metric shall be considered as a series of measure points
* `A`/`a` to specify that the metric shall be considered continuous, in a GUI a line may be drawn between measure points (this is the default if `p` is not specified)

**Note:** lines, columns and also byteOffset are numbered from 0. So in order to read out the 3th column from the 5th line one would specify `c=2;l=4` (or `c=2;r=4` since *r* and *l* are interchangeable).

For example the following specification:
```
export SCOREP_METRIC_FILEPARSER_PLUGIN="netstat InOctets:int@/proc/net/netstat+c=7;r=3;s= ;a;d"
```
Will log an `int` value from the file `/proc/net/netstat/` under the label `netstat InOctets`. On the 4th line it will read the 8th column (as delimited with ' ').  It will read an initial value, and all further reads will be a delta to the initially read value (option `d`).  In the GUI Vampir, the metric's graph will be shown as a graph with a continuous line as this has been requested (with option `a`).

Another example:
```
export SCOREP_METRIC_FILEPARSER_PLUGIN="CPU Clock 1:double@/proc/cpuinfo+c=2;r=7;s= ;p,CPU Clock 2:double@/proc/cpuinfo+c=2;r=34;s= ;p"
```
This will log the current CPU Mhz of the first two CPU Cores. Both values will be read and logged as `double` and be displayed as single point readings (`p`). The actual values can be read from file `/proc/cpuinfo` from the 3th column in the lines 8 and 35.

Fancy example:
```
export SCOREP_METRIC_FILEPARSER_PLUGIN="CPU Clock 1:double@/proc/cpuinfo+c=2;r=7;s= ;p,CPU Clock 2:double@/proc/cpuinfo+c=2;r=34;s= ;p,CPU Clock 3:double@/proc/cpuinfo+c=2;r=61;s= ;p,CPU Clock 4:double@/proc/cpuinfo+c=2;r=88;s= ;p,Load AVG:double@/proc/loadavg+c=1;r=0;s= ,MemFree:int@/proc/meminfo+c=1;r=1;s= ,netstat:int@/proc/net/netstat+c=7;r=3s= ;d,dev    :int@/proc/net/dev+c=1;r=3;d,Core#0Temp:int@/sys/class/hwmon/hwmon0/temp1_input+c=0;r=0;p,Core#1Temp:int_hex@/sys/class/hwmon/hwmon1/temp1_input+;p"
```
This will log the Mhz count of CPU 1, CPU 2, CPU 3, CPU 4, the system load, the count of kb of free memory, the InOctets value from /proc/net/netstat, some value from /proc/net/dev and the cpu core temperatures of Core 1 and Core 2.

# Can this plugin read binary?
Yes.

For example it could read the second byte of temperature string from the first CPU core. Note that in this example `b=1`is required to specify the offset at which a data value is to be read.
```
export SCOREP_METRIC_FILEPARSER_PLUGIN="CPU Temp:uint8_bin@/sys/class/hwmon/hwmon0/temp1_input+b=1"
```
