#!/usr/bin/env bash
# 启动单节点 HDFS+YARN（T02）：幂等、绝不 format、等待真实就绪。
set -euo pipefail
java_home="${JAVA_HOME:-/home/bit/opt/jdk1.8.0_261}"
hadoop_home="${HADOOP_HOME:-/home/bit/opt/hadoop-3.2.1}"
[ "$(id -u)" = "0" ] && { echo "禁止root运行启动脚本"; exit 2; }
[ -x "$hadoop_home/sbin/start-dfs.sh" ] || { echo "缺少 $hadoop_home/sbin/start-dfs.sh，先完成04指南安装" >&2; exit 2; }
[ -x "$java_home/bin/java" ] || { echo "缺少 $java_home/bin/java" >&2; exit 2; }
export JAVA_HOME="$java_home" HADOOP_HOME="$hadoop_home" HADOOP_CONF_DIR="$hadoop_home/etc/hadoop"
jps_bin="$java_home/bin/jps"
daemon_running() { "$jps_bin" 2>/dev/null | awk '{print $2}' | grep -qx "$1"; }
wait_for() { # wait_for <描述> <重试次数> <命令...>
  local label="$1" tries="$2"; shift 2
  local i=0
  while [ "$i" -lt "$tries" ]; do
    if "$@" >/dev/null 2>&1; then return 0; fi
    i=$((i+1)); sleep 2
  done
  echo "等待超时: $label" >&2
  return 1
}
if daemon_running NameNode && daemon_running DataNode; then
  echo "HDFS=ALREADY_RUNNING"
else
  "$hadoop_home/sbin/start-dfs.sh"
fi
wait_for "Live DataNode" 30 "$hadoop_home/bin/hdfs" dfsadmin -report 2>/dev/null || { echo "HDFS=FAIL"; exit 1; }
echo "HDFS=STARTED"
if daemon_running ResourceManager && daemon_running NodeManager; then
  echo "YARN=ALREADY_RUNNING"
else
  "$hadoop_home/sbin/start-yarn.sh"
fi
wait_for "RUNNING NodeManager" 30 "$hadoop_home/bin/yarn" node -list || { echo "YARN=FAIL"; exit 1; }
echo "YARN=STARTED"
for d in raw staging rejected hourly features artifacts; do
  "$hadoop_home/bin/hdfs" dfs -mkdir -p "/charge_platform/$d"
done
echo "HDFS_DIRS=OK"
echo "BIGDATA=STARTED"
