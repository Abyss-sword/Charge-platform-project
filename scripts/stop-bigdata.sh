#!/usr/bin/env bash
# 停止单节点 YARN+HDFS（T02）：先 YARN 后 HDFS，等待真实退出，幂等。
set -euo pipefail
java_home="${JAVA_HOME:-/home/bit/opt/jdk1.8.0_261}"
hadoop_home="${HADOOP_HOME:-/home/bit/opt/hadoop-3.2.1}"
[ "$(id -u)" = "0" ] && { echo "禁止root运行停止脚本"; exit 2; }
[ -x "$hadoop_home/sbin/stop-dfs.sh" ] || { echo "缺少 $hadoop_home/sbin/stop-dfs.sh" >&2; exit 2; }
export JAVA_HOME="$java_home" HADOOP_HOME="$hadoop_home" HADOOP_CONF_DIR="$hadoop_home/etc/hadoop"
jps_bin="$java_home/bin/jps"
daemon_running() { "$jps_bin" 2>/dev/null | awk '{print $2}' | grep -qx "$1"; }
if daemon_running ResourceManager || daemon_running NodeManager; then
  "$hadoop_home/sbin/stop-yarn.sh"
fi
if daemon_running NameNode || daemon_running DataNode; then
  "$hadoop_home/sbin/stop-dfs.sh"
fi
i=0
while [ "$i" -lt 30 ]; do
  if daemon_running NameNode || daemon_running DataNode || daemon_running ResourceManager || daemon_running NodeManager; then
    i=$((i+1)); sleep 2
  else
    echo "BIGDATA=STOPPED"
    exit 0
  fi
done
echo "仍有Hadoop进程未退出，请查 $hadoop_home/logs" >&2
"$jps_bin" >&2 || true
exit 1
