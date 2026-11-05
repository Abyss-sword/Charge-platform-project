# Stage1 收尾候选基线（baseline.md）

编制时间：2026-09-10。执行包：`docs/superpowers/plans/stage1-completion-20260909/`（7文件，2026-09-09编制）。

## 来源与候选

| 对象 | 分支/引用 | 提交 |
| --- | --- | --- |
| 来源主干 | main | b1d3ab79af0d834667baf785b96c007f512111de |
| 成员1 | feature/member1-stage1-integration | 80697bd538add6f3fd888707245fa3b7551bda3e |
| 成员2 | feature/member2-ui | 9384bf0e82375a708b23a3608659a8e1dc6dcfb8 |
| 成员3 | feature/member3-simulator-analytics-dashboard | 32ff3654e04166a3a89f644e3c6f606a54363fe6 |
| 验收文档 | docs/stage1-integration-baseline | 00fd95761458499d8745c0a5d6fcedeb2877f49f |
| 候选工作树 | /home/bit/projects/charge_platform_stage1_candidate | integration/stage1-final-20260909 |

合并顺序：80697bd 基线 → 6fecae8 并入成员2（解决 docs/stage1-integration 两份成员1文档 add/add 冲突，取成员1权威版）→ 4a7528d 并入成员3（CMake 自动合并带入 clients/tests-ui 构建接线）→ c9738d8 并入验收文档。

git fetch origin 于 2026-09-10 失败（git@github.com Permission denied (publickey)），远端无法刷新；以上述本地 origin 引用与 00-README 记录一致为准（5/5 SHA 核对通过）。按 05 文档规则记录原因，未反复重试、未改 SSH 配置。

## 环境与工具版本（bitdev / 192.168.41.128，Ubuntu 22.04）

- Linux 6.8.0-138-generic；cmake 3.22.1；ninja 1.10.1；g++ 11.4.0；Qt 6.2.4（qmake6）；python3 3.10.12；sqlite3 3.37.2
- 2 核 / 7.7GiB 内存 / 根分区余 447G；构建统一 -j2
- Node.js：VM 原未安装；经用户授权以用户级解压方式安装到 ~/tools（不改全局环境），版本在安装后记录于 test-results.md
- Python 依赖：候选内 `.venv`（fastapi/uvicorn/pytest 等，按 analytics/pyproject.toml），不改系统环境
- GNOME 桌面会话活跃（:0）；Qt GUI 验证方式=主机操控 VM 窗口（用户已确认），自动化兜底 QT_QPA_PLATFORM=offscreen + GNOME D-Bus 截图

## 文件归属（一文件一写作者，总控兼成员1）

| 角色 | 可写 |
| --- | --- |
| 总控/成员1 | server/、shared/、database/、根CMakeLists.txt、cmake/、scripts/（公共）、tests/integration、tests/e2e、docs/stage1-release、正式协议文档 |
| 成员2 | clients/user、clients/admin、clients/shared/network、clients/shared/config、tests/ui（对应文件） |
| 成员3 | simulator/、analytics/、dashboard/ 及各自测试 |

顺序单Agent执行，无并行写冲突；跨目录问题汇总到总控统一处理。

## 旧环境保护范围（不触碰）

- 主工作树 /home/bit/projects/charge_platform_project（main，含 runtime/stage1-defense.sqlite 等旧演示库）
- 全部 .worktrees/ 下 8 个既有 worktree 及其分支（含 build-review/、build-defense/、analytics/models/ 等未跟踪产物）
- 未跟踪 .zcode/（主树）；不查看其中秘密、不删除、不加入提交
- 旧端口 48080 未被占用（记录时无进程监听）；本轮测试一律端口 0（系统分配）
- git remote origin 的 GitHub SSH key 无权限，仅本地操作，不推送

## 候选专用路径

- 数据库/头像/运行产物：/home/bit/projects/charge_platform_stage1_candidate/runtime/（git 忽略）
- 构建目录：build-stage1/、build-simulator-stage1/（git 忽略）
- 交付物：docs/stage1-release/（baseline/contract-delta/task-status/test-results/acceptance/runbook/known-limitations/rollback + inputs/）

