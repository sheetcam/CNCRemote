#!/bin/bash
ver=`linuxcnc_var LINUXCNCVERSION`
ver=`expr $ver : '\([0-9]*\.[0-9]*\.[0-9]*\)'`
if [ $? -ne 0 ]
then 
   echo LinuxCNC not found
   exit 1
fi
echo "//LinuxCNC version = $ver" > version.h
echo "//File auto generated by version.sh" >> version.h
IFS='.'; arr=($ver); unset IFS;
echo "#define LINUXCNC_VER ((${arr[0]} * 10000) + (${arr[1]} * 100) + ${arr[2]})" >> version.h
echo "#define LINUXCNC_PRE_JOINTS (LINUXCNC_VER < 20800)  //This is when axes and joints were separated" >> version.h
echo "#define LINUXCNC_MULTI_SPINDLE (LINUXCNC_VER >= 20800)  //This is when multi-spindle was added" >> version.h

