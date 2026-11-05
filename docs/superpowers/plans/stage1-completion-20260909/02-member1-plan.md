# 成员1：公共基础、服务端改造与集成负责人的实施计划

> **For agentic workers:** 使用 `executing-plans` 按任务执行；需要并行时仅分发不重叠目录。所有新接口先读01文档。不得修改成员原分支、main和原演示库。

**Goal:** 补齐支撑完整三端联动的公共能力，保留现有锁价/事务/防重成果，为成员2和成员3提供可测试的统一接口。

**Architecture:** 在现有Service/Repository、TCP路由和SQLite上最小增量实现。计量、订单结算及设备异常原子提交；Qt/模拟器只调用公共协议。

**Tech Stack:** C++17、Qt6 Network/Sql/Test、SQLite、Boost DecimalValue、CMake/CTest、Python补充协议检查。

## 1. 文件所有权与依赖

成员1唯一可写：`server/`、`shared/`、`database/`、根 `CMakeLists.txt`、`cmake/`、公共 `scripts/`、服务端测试、正式协议和集成证据。成员2自己的子CMake可以修改，根入口由成员1归并。

当前入口：

- `server/app/main.cpp`：固定48080，读取数据库环境变量。
- `server/network/tcp_server.cpp/.h`：已有listen(address,0)和port()，可复用系统分配端口。
- `server/network/request_router.cpp/.h`：动作注册、授权与参数。
- `server/services/order_service.cpp/.h`、`telemetry_service.cpp/.h`、`wallet_service.cpp/.h`、`user_service.cpp/.h`、`admin_service.cpp/.h`。
- `server/repositories/order_repository.*`、`telemetry_repository.*`、`user_repository.*`、`wallet_repository.*`、`database.*`。
- `shared/domain/decimal_value.*`、`shared/protocol/error_code.h`、`frame_codec.*`、`message.h`。
- 已有迁移001–006；新增迁移采用接手时下一可用编号，不改旧迁移。
- 已有测试 `tests/integration/client_query_protocol_test.cpp`、`telemetry_protocol_test.cpp`、`payment_idempotency_test.cpp`、`reservation_race_test.cpp`、`alarm_ticket_test.cpp`、`admin_operations_protocol_test.cpp`、`stage1_supplement_check.py`。

下文新增文件名是建议交付路径，不表示现在已经存在；已有等价文件时复用，保持单一实现并在baseline记录映射。

## 2. M1-01：安全构建与独立测试入口（R1第一项）

**改动文件：** 根CMake、`server/app/main.cpp`、`tests/integration/stage1_supplement_check.py`；新增 `tests/integration/server_launch_config_test.py`，公共启动脚本最后由M1-07完成。

- [ ] 先构造失败检查：以不同构建目录的二进制启动；要求使用临时库、127.0.0.1、端口0。当前程序固定端口，应看到预期失败，不能因为旧端口恰好空闲而判通过。
- [ ] 用QCommandLineParser增加 `--host`、`--port`、`--db`，保留原默认行为；非法端口、空路径和监听失败返回非0。测试显式给端口0，listen成功后输出实际地址/端口作为就绪信息，不输出凭据。
- [ ] 补充脚本增加 `--server` 参数，使用所传构建产物，不固定 `build/`；给子进程分配临时库，读取实际就绪端口后连接。启动超时输出子进程日志，关闭仅本脚本创建的进程。
- [ ] 不采用“先找空端口、关闭探针、再假定还空闲”的方案；复用服务端bind(0)实际端口。不得为了测试先停掉48080上的旧服务。
- [ ] 根CMake按 `clients/shared/ui → clients/user → clients/admin → tests/ui` 的实际依赖接入；对应源码来自集成后的成员2。测试目录受BUILD_TESTING控制；缺少必要模块时配置明确失败，不用if(EXISTS)悄悄跳过还宣称整组构建完成。
- [ ] 将补充脚本注册CTest，使用 `${Python3_EXECUTABLE}` 和实际 `$<TARGET_FILE:charge_platform_server>`。以find_package结果确定解释器，不写死某人路径。
- [ ] 注册新启动配置测试，验证并行启动两个隔离服务得到不同端口、两个临时库互不影响，退出后无遗留子进程。

**通过条件：** 从新build-stage1目录能生成服务端和Qt双端；ctest -N可见UI和补充测试；测试不访问默认运行库和旧服务。历史45项脚本的逐项断言要保留，不把失败项删掉来“整合”。

## 3. M1-02：用户会话、归属与请求状态

**文件：** 新增 `server/services/user_session_service.*`、对应迁移和 `tests/integration/user_authorization_protocol_test.cpp`；修改router/user/wallet/order查询路径、协议与CMake。

按01文档4.1、4.5完成，先用真实TcpServer和临时库写以下测试，再实现：

| 用例 | 输入/步骤 | 必须结果 |
| --- | --- | --- |
| U01 | 手机号A登录 | 返回userId、userToken、expiresAt；令牌不写明文库/日志 |
| U02 | 不带/乱填/过期token访问本人资料或钱包 | AUTH_INVALID，无数据和写入 |
| U03 | A的token携带B的userId | 拒绝；B余额/资料不变 |
| U04 | A的token读取/停止/支付B的orderId | 拒绝；B订单不变，不泄露详情 |
| U05 | 用户token访问管理员动作、adminToken替代userToken | 拒绝跨角色使用 |
| U06 | 冻结A后尝试预约/开始 | USER_FROZEN；停止、查询、结清既有单仍可完成 |
| U07 | 同充值requestId查询REQUEST_STATUS | 仅本人能查到COMMITTED；其他人拒绝 |
| U08 | 原充值成功→重登录换token→复用原ID同参数重试 | 仍仅一笔充值，不重复赠送登录余额 |
| U09 | 原requestId金额/动作不同 | DUPLICATE_REQUEST；库无部分变更 |

- [ ] 在路由进入业务方法前解析身份，不能先查幂等结果再做权限。
- [ ] 查询orderId关联userId并核对所有权，覆盖每个读写动作，不仅WALLET_PAY。
- [ ] REQUEST_STATUS读取已提交防重记录，不通过余额差推断成功。未找到返回NOT_FOUND状态对象；失败响应与业务状态不得混淆。
- [ ] 修改已有网络测试客户端取token并传入；保持原业务断言，再增加越权断言。
- [ ] 更新delivery-samples完整外壳和错误示例，表明旧客户端须同步更新，不能部署后悄悄保留无认证写入口。

**交接：** 给M2一个匿名教学登录、查询资料、支付及REQUEST_STATUS的真实往返样例；给M3设备token配置方式，不向其授予用户钱包权限。手机登录仅教学身份选择，不宣传能阻止知道其他手机号的人模拟登录。

## 4. M1-03：统一设备计量、异常结算和管理禁用

**文件：** order/telemetry/admin Services及Repository、router、追加迁移；新增 `tests/integration/order_metering_protocol_test.cpp` 与 `device_override_test.cpp`，扩充telemetry/alarm相关测试。

先定义内部事务边界：单次请求只BEGIN一次。把“按最后有效读数冻结账单”的逻辑抽成接收同一数据库事务的内部操作，正常停止和故障停止复用；不能在TelemetryService事务里调用会再次BEGIN的公开stop()。

推荐执行顺序：

1. [ ] 失败测试：PILE_TELEMETRY目前不改变订单用量，测试仅通过真实网络上报，不调用OrderService::recordTelemetry注入。
2. [ ] 追加订单表计基准及最近应用读数的迁移；对已有未完成、无基准订单，不伪造历史起点。旧运行库保持不动；迁移测试证明旧数据保留。没有可靠基准的历史充电单阻止继续自动计量并显示需人工结算，不默认为0费用。
3. [ ] 开始事务记录新鲜基准和锁价；开始前无/过期读数返回DEVICE_TIMEOUT，预约保持。
4. [ ] 单条有效遥测在同一事务更新设备记录、接收心跳、订单增量、状态与告警工单。
5. [ ] 高温/离线停止活动充电，冻结金额；迟到或停止后的增量不能改变账单。预约故障允许取消/过期但保持桩异常。
6. [ ] 关闭正式网络ORDER_TELEMETRY写入；设备token校验覆盖每次PILE请求。
7. [ ] 追加管理员禁用覆盖标记，正常遥测不得清掉；支付释放、取消、过期、重启均用一致的设备可用性推导。
8. [ ] 更新查询返回额外状态说明，M2/M3显示“待支付占用”“管理禁用”和“物理故障”，不新增另一套订单状态。

固定计量用例（时钟注入，避免真实等15分钟）：

| 编号 | 场景 | 核对 |
| --- | --- | --- |
| E01 | 基准100.00→开始→100.10→108.35→停止 | 本单8.35，1.23锁价得到10.27 |
| E02 | 同时间同内容重复两次 | 业务记录不重复、费用不增加 |
| E03 | 同时间不同值/时间倒退/电量倒退 | 明确拒绝，订单/最新读数均不变 |
| E04 | 开始中途站点改9.99 | 本单仍按1.23，下一单按新价 |
| E05 | 正常增量后70℃ | 本条合法增量计入后冻结；单PENDING_PAYMENT、桩FAULT、一个活动告警和工单 |
| E06 | 断联到阈值＋调度间隔 | 按最后有效读数冻结，不估算失联能量；桩OFFLINE |
| E07 | E05/E06之后恢复遥测 | 不恢复CHARGING订单，不改已冻结金额；仍保持占用直到支付 |
| E08 | 支付故障桩订单 | 仅解除订单占用，桩仍FAULT/禁用；不能变IDLE |
| E09 | 第二单基准108.35→110.00 | 新单1.65，不继承前单8.35 |
| E10 | 管理员禁用→正常遥测→模拟重启 | 仍禁用；明确解除且健康后才IDLE |
| E11 | 事务中工单写入故意失败 | 遥测、订单金额、状态整体回滚 |
| E12 | 两客户端预约同桩 | 仍只一单成功；现有部分唯一索引和事务保护未退化 |
| E13 | 未授权ORDER_TELEMETRY/PILE | 拒绝，无用量变化 |

参数化服务器时钟并复用已有Clock模式；不在生产逻辑写测试专用sleep或把超时阈值改大来通过。

## 5. M1-04：头像上传及查询

**文件：** 新增 `server/services/avatar_service.*` 和 `tests/integration/avatar_protocol_test.cpp`，router/user查询/协议；必要时追加数据库引用字段，不用运行库原位试验。

- [ ] 先失败测试两台逻辑客户端：A上传合法小PNG，重新登录后GET取回，资料有avatarId。
- [ ] 增加01文档4.4规定的动作、限额、类型/尺寸检查、随机资源ID和安全存储根配置。
- [ ] 客户端只发内容，不发服务器路径；错误base64、超限、伪扩展名、损坏图片、超大像素、路径穿越、越权访问都拒绝。
- [ ] 用同目录临时文件+原子重命名及数据库引用更新；失败不破坏旧头像。测试写入失败和数据库失败两条路径。
- [ ] 响应与完整请求不超帧长；资料缺头像用默认占位图，不是Unavailable页。
- [ ] 明确512KiB是本轮可安全经TCP发送的限额更新，替换旧样例“2MiB本地路径”限制说明。

## 6. M1-05：公共接口审查与错误码

- [ ] 为每个action登记参数、权限、响应类型、正常/非法/越权/状态冲突测试；总体数量随新增动作真实统计，不固定35个。
- [ ] 检查管理总览/总营收/趋势不是全部rows结构，M2按真实DTO解析。
- [ ] 用户昵称和余额更新后必须能重新查询；订单历史有分页边界、空列表；STATION_DETAIL返回本桩列表。
- [ ] 请求日志记录requestId、action和结果，不记录userToken/deviceToken/adminToken、头像base64或完整个人手机号。
- [ ] DB忙或内部错误返回清楚失败并回滚，客户端可核对而非假成功；批量列表限制沿用现有边界，禁止无限读全库。
- [ ] 更新 `docs/protocol.md`、`docs/data-dictionary.md`、`docs/stage1-integration/delivery-samples.md` 和gap-matrix，明确01文档中哪些新增契约已落实。

## 7. M1-06：与两个模块的交接

给M2：新登录token、用户/头像/请求状态样例；全部页面动作映射；断线后应查询什么；管理禁用和待支付占用的显示语义。

给M3：只允许PILE计量、deviceToken配置、正确pileId、基准要求、重复/倒退规则、异常自动结算；用量和金额的只读数据接口；新迁移字段含义。

两者均收到相同 `contract-delta.md` 和契约测试；接口未过时不要要求前端先“写死数据绕过”。如果接手时已有更新，按差异验证，不重复实现相同能力。

## 8. M1-07：启动、集成和交付

新增公共启动器建议 `scripts/stage1-stack.py`，接受 `start/status/stop` 与专用配置。配置包含候选根、数据库绝对路径、服务端/Qt/FastAPI/Web启动命令、绑定地址和端口，不包含Git跟踪的密钥。

启动器先检查全部文件/端口/数据库路径，再按服务端→模拟器正常基准→API→Web→Qt顺序启动。每步检查就绪；失败只回收本次创建且身份匹配的子进程。stop读取本运行记录和进程实际命令/启动信息，不盲目kill一个可能复用的PID；遇到不匹配保留并报告。禁止 `pkill python/node/charge*`。

迁移和数据初始化是显式准备命令，不在每次start中重置。旧库备份另存，部署需新授权。交付runbook应能由一人操作，不依赖成员2/3现场同步敲命令。

## 9. 成员1完成标准

新服务端用例、原20项对应功能与补充脚本断言均通过；新根构建包含Qt和模块测试；权限、计量、头像和请求状态可通过真实TCP复现。公开文档与代码一致。最后还必须按第05文档整组验收，单独成员1完成不能替代全组通过。
