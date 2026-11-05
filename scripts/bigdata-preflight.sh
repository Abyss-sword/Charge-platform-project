#!/usr/bin/env bash
# 大数据环境只读预检（T02）：核验版本、资源、端口与 HDFS 目录，不做任何修改。
set -euo pipefail
java_home="${JAVA_HOME:-/home/bit/opt/jdk1.8.0_261}"
hadoop_home="${HADOOP_HOME:-/home/bit/opt/hadoop-3.2.1}"
fail=0
note() { printf '%s\n' "$*"; }
mark() { if [ "$1" = 0 ]; then printf '%s=OK\n' "$2"; else printf '%s=FAIL\n' "$2"; fail=1; fi; }
[ "$(id -u)" = "0" ] && { echo "PREFLIGHT=FAIL 禁止root运行预检"; exit 2; }
[ -x "$java_home/bin/java" ] || { echo "缺少 $java_home/bin/java"; exit 2; }
[ -x "$hadoop_home/bin/hadoop" ] || { echo "缺少 $hadoop_home/bin/hadoop"; exit 2; }
export JAVA_HOME="$java_home" HADOOP_HOME="$hadoop_home" HADOOP_CONF_DIR="$hadoop_home/etc/hadoop"
note "USER=$(id -un)"
note "ARCH=$(uname -m)"
note "CPU=$(nproc)"
free -h | sed -n 2p | awk '{print "MEM_TOTAL="$2}'
disk_kb=$(df -Pk /home/bit | awk 'NR==2{print $4}')
note "DISK_AVAIL_KB=$disk_kb"
if [ "$disk_kb" -lt 10485760 ]; then echo "/home/bit 剩余不足10GiB，批处理可能失败"; mark 1 DISK; else mark 0 DISK; fi
ver_java="$("$java_home/bin/java" -version 2>&1 | head -1)"
note "JAVA=$ver_java"
case "$ver_java" in *1.8.0_261*) mark 0 JAVA_VERSION ;; *) mark 1 JAVA_VERSION ;; esac
ver_hadoop="$("$hadoop_home/bin/hadoop" version 2>/dev/null | head -1)"
note "HADOOP=$ver_hadoop"
case "$ver_hadoop" in *"3.2.1"*) mark 0 HADOOP_VERSION ;; *) mark 1 HADOOP_VERSION ;; esac
if command -v ss >/dev/null 2>&1; then
  for p in 9000 9870 9864 8088 8042; do
    if ss -ltn 2>/dev/null | grep -qE ":$p\b"; then note "PORT_${p}=LISTENING"; else note "PORT_${p}=FREE"; fi
  done
  mark 0 PORTS
else
  mark 1 PORTS
fi
if "$java_home/bin/jps" 2>/dev/null | grep -q NameNode; then
  if "$hadoop_home/bin/hdfs" dfsadmin -report 2>/dev/null | grep -qF "Live datanodes (1"; then mark 0 HDFS; else mark 1 HDFS; fi
  if "$hadoop_home/bin/hdfs" dfs -test -d /charge_platform/staging 2>/dev/null; then mark 0 HDFS_DIRS; else mark 1 HDFS_DIRS; fi
  if "$hadoop_home/bin/yarn" node -list 2>/dev/null | grep -q RUNNING; then mark 0 YARN; else mark 1 YARN; fi
else
  note "HDFS=STOPPED"
  note "YARN=STOPPED"
  note "HDFS_DIRS=UNKNOWN"
  mark 0 PREFLIGHT_SCOPE
fi
if [ "$fail" = 0 ]; then echo "PREFLIGHT=OK"; else echo "PREFLIGHT=FAIL"; exit 1; fi
