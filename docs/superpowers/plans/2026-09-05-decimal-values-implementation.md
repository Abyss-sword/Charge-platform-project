# Decimal Values Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 将金额、余额、单价和电量从整数分/Wh模型迁移为内部高精度十进制、数据库两位小数字符串、协议两位小数字符串，并按四舍五入规则完成计算和展示。

**Architecture:** 在`shared/domain`建立唯一的`DecimalValue`值对象，Boost.Multiprecision负责十进制计算，所有服务和仓库只通过该对象处理金额与电量。SQLite保存固定两位文本，TCP JSON保存固定两位字符串；迁移004负责把旧数据原子转换，协议和分析契约提升至v2。

**Tech Stack:** C++17、Qt 6.2、Boost.Multiprecision、SQLite 3.37、CMake/Ninja、Qt Test、Bash。

---

## 文件责任图

**新增文件**

- `shared/domain/decimal_value.h`：十进制解析、两位四舍五入、格式化和运算的唯一入口。
- `shared/domain/decimal_value.cpp`：高精度十进制实现。
- `tests/unit/decimal_value_test.cpp`：四舍五入、非法输入和运算测试。
- `database/migrations/004_decimal_values.sql`：旧整数列到十进制文本列的原子迁移。
- `analytics/contracts/charge_event_v2.schema.json`：第二版分析数据契约。

**修改文件**

- `CMakeLists.txt`：注册十进制实现与测试。
- `server/repositories/{user,order,wallet}_repository.*`：改读写两位十进制文本。
- `server/services/{user,station,order,wallet,admin}_service.*`：使用十进制值对象和新单位。
- `server/network/request_router.cpp`：严格读取和输出十进制字符串。
- `database/seeds/demo.sql`：模拟数据改为新列和新单位。
- `scripts/export-analysis-sample.sh`：导出v2字段。
- `tests/helpers/test_database.h`及现有业务测试：统一新字段与新断言。
- `docs/protocol.md`、`docs/analytics-data-contract.md`、`docs/data-dictionary.md`：发布协议v2。
- `README.md`：更新项目名称、状态、使用方法和十进制规则。
- `docs/test-results/stage-1-member1-acceptance.md`：记录本次变更后的复验结果。

## Task 1：安装并验证十进制依赖

**Files:** 无代码修改。

- [ ] **Step 1：确认依赖当前缺失**

Run:

```bash
test -f /usr/include/boost/multiprecision/cpp_dec_float.hpp
```

Expected: 命令失败，证明测试环境确实缺少Boost头文件。

- [ ] **Step 2：安装Ubuntu依赖**

Run:

```bash
sudo apt-get update
sudo apt-get install -y libboost-dev
```

Expected: 安装成功，无未配置软件包。

- [ ] **Step 3：验证头文件**

Run:

```bash
test -f /usr/include/boost/multiprecision/cpp_dec_float.hpp
dpkg-query -W -f='${Status} ${Version}\n' libboost-dev
```

Expected: 文件存在，状态为`install ok installed`。

## Task 2：以失败测试冻结十进制规则

**Files:**

- Create: `tests/unit/decimal_value_test.cpp`
- Modify: `CMakeLists.txt`

- [ ] **Step 1：新增测试目标，但暂不实现生产代码**

测试类必须包含以下用例：

```cpp
private slots:
    void roundsHalfUp();
    void roundsNegativeHalfUp();
    void formatsExactlyTwoDecimals();
    void rejectsInvalidDecimalText();
    void multipliesAndRoundsOnce();
```

关键断言：

```cpp
QCOMPARE(DecimalValue::parseRounded(QStringLiteral("10.2705"))->toString(),
         QStringLiteral("10.27"));
QCOMPARE(DecimalValue::parseRounded(QStringLiteral("10.275"))->toString(),
         QStringLiteral("10.28"));
QCOMPARE(DecimalValue::parseRounded(QStringLiteral("8.344"))->toString(),
         QStringLiteral("8.34"));
QCOMPARE(DecimalValue::parseRounded(QStringLiteral("8.345"))->toString(),
         QStringLiteral("8.35"));
QCOMPARE(DecimalValue::parseRounded(QStringLiteral("-10.275"))->toString(),
         QStringLiteral("-10.28"));
QCOMPARE(DecimalValue::parseRounded(QStringLiteral("10"))->toString(),
         QStringLiteral("10.00"));
QVERIFY(!DecimalValue::parseRounded(QStringLiteral("1e3")));
QVERIFY(!DecimalValue::parseRounded(QStringLiteral("NaN")));
QVERIFY(!DecimalValue::parseRounded(QStringLiteral("10元")));
QCOMPARE(multiplyRounded(*DecimalValue::parseRounded(QStringLiteral("8.35")),
                         *DecimalValue::parseRounded(QStringLiteral("1.23"))).toString(),
         QStringLiteral("10.27"));
```

在`CMakeLists.txt`中加入`decimal_value_test`并链接`charge_domain`。

- [ ] **Step 2：运行并确认按预期失败**

Run:

```bash
cmake -S . -B build -G Ninja -DBUILD_TESTING=ON
cmake --build build --target decimal_value_test
```

Expected: 因`shared/domain/decimal_value.h`不存在而编译失败。失败原因必须是缺少新能力，而不是CMake语法错误。

- [ ] **Step 3：保留失败测试，暂不提交**

失败测试与Task 3实现一起提交，避免在历史中留下无法构建的中间提交。

## Task 3：实现统一十进制值对象

**Files:**

- Create: `shared/domain/decimal_value.h`
- Create: `shared/domain/decimal_value.cpp`
- Modify: `CMakeLists.txt`
- Test: `tests/unit/decimal_value_test.cpp`

- [ ] **Step 1：建立最小公共接口**

头文件接口固定为：

```cpp
class DecimalValue
{
public:
    DecimalValue();
    static std::optional<DecimalValue> parseRounded(const QString &text);
    static std::optional<DecimalValue> parseCanonical(const QString &text);
    QString toString() const;
    bool isNegative() const;
    bool isZero() const;

    friend bool operator==(const DecimalValue &, const DecimalValue &);
    friend bool operator<(const DecimalValue &, const DecimalValue &);
    friend DecimalValue operator+(const DecimalValue &, const DecimalValue &);
    friend DecimalValue operator-(const DecimalValue &, const DecimalValue &);
    friend DecimalValue multiplyRounded(const DecimalValue &, const DecimalValue &);

private:
    using Number = boost::multiprecision::cpp_dec_float_50;
    explicit DecimalValue(Number value);
    Number value_ = 0;
};
```

`parseRounded`接受普通十进制文本并立即四舍五入；`parseCanonical`只接受`-?[0-9]+\.[0-9]{2}`格式，供数据库读取；`toString`固定输出两位。

- [ ] **Step 2：实现统一四舍五入**

核心只保留一个实现：

```cpp
boost::multiprecision::cpp_dec_float_50 roundToTwo(
    boost::multiprecision::cpp_dec_float_50 value)
{
    using Number = boost::multiprecision::cpp_dec_float_50;
    const Number scaled = value * 100;
    const Number units = scaled < 0
        ? boost::multiprecision::ceil(scaled - Number("0.5"))
        : boost::multiprecision::floor(scaled + Number("0.5"));
    return units / 100;
}
```

在实现中先用`QRegularExpression`拒绝科学计数、空值、单位字符和非有限文本，再构造`cpp_dec_float_50`。`multiplyRounded`先完成高精度乘法，只在最终结果处调用一次`roundToTwo`。

- [ ] **Step 3：构建并运行新测试**

```bash
cmake --build build --target decimal_value_test
ctest --test-dir build -R decimal_value_test --output-on-failure
```

Expected: 新测试全部通过。

- [ ] **Step 4：运行原有领域测试**

```bash
ctest --test-dir build -R "geo_test|order_state_test" --output-on-failure
```

Expected: 通过。

- [ ] **Step 5：提交实现**

```bash
git add CMakeLists.txt shared/domain/decimal_value.h shared/domain/decimal_value.cpp tests/unit/decimal_value_test.cpp
git commit -m "feat: add precise decimal value type"
```

## Task 4：先用数据库测试定义迁移结果

**Files:**

- Modify: `tests/integration/schema_test.cpp`
- Test: `tests/integration/schema_test.cpp`

- [ ] **Step 1：把结构断言改为新字段**

测试必须确认以下字段存在且类型为`TEXT`：

```text
user_info.balance_yuan
station_info.price_yuan_per_kwh
charge_order.energy_kwh
charge_order.amount_yuan
charge_order.unit_price_yuan_per_kwh
wallet_transaction.amount_yuan
pile_telemetry.energy_kwh
```

并确认旧字段不存在：

```text
balance_cents
price_cents_per_kwh
energy_wh
amount_cents
unit_price_cents_per_kwh
```

- [ ] **Step 2：增加旧库迁移样例**

在只执行001至003的临时数据库中插入：

```sql
UPDATE user_info SET balance_cents=10005 WHERE id=1;
UPDATE station_info SET price_cents_per_kwh=123 WHERE id=1;
UPDATE charge_order SET energy_wh=8345,amount_cents=1027,
    unit_price_cents_per_kwh=123 WHERE id='ORD-DEMO-001';
```

执行004后断言：

```text
balance_yuan = "100.05"
price_yuan_per_kwh = "1.23"
energy_kwh = "8.35"
amount_yuan = "10.27"
```

- [ ] **Step 3：运行并确认失败**

```bash
cmake --build build --target schema_test
ctest --test-dir build -R schema_test --output-on-failure
```

Expected: 因004迁移不存在或新列不存在而失败。

- [ ] **Step 4：保留失败测试，暂不提交**

失败测试与Task 5迁移一起提交，避免在历史中留下无法通过的数据库测试。

## Task 5：实现004数据库迁移与模拟数据切换

**Files:**

- Create: `database/migrations/004_decimal_values.sql`
- Modify: `database/seeds/demo.sql`
- Test: `tests/integration/schema_test.cpp`

- [ ] **Step 1：新增十进制字段并转换旧值**

004必须在现有迁移事务内依次执行“新增、转换、校验、删除旧列”。转换规则：

```text
cents -> yuan：除以100，固定两位
Wh -> kWh：除以1000后四舍五入到两位
```

负资金流水必须保留负号，例如`-1200 -> "-12.00"`。使用SQLite字符串拼装完成一次性旧值转换，不用`REAL`中转金额。

- [ ] **Step 2：保留约束和索引**

迁移后检查并恢复：

```sql
CREATE UNIQUE INDEX IF NOT EXISTS ux_charge_order_active_pile
ON charge_order(pile_id)
WHERE status IN ('RESERVED', 'CHARGING', 'PENDING_PAYMENT');
```

新文本字段必须`NOT NULL`，余额、单价、电量、订单金额不得为负，资金流水不得为`"0.00"`。

- [ ] **Step 3：更新模拟种子**

所有插入和更新使用新列及两位文本，例如：

```sql
balance_yuan='100.00'
price_yuan_per_kwh='1.20'
energy_kwh='10.00'
amount_yuan='12.00'
```

- [ ] **Step 4：运行迁移测试两次**

```bash
cmake --build build --target schema_test
ctest --test-dir build -R schema_test --repeat until-fail:2 --output-on-failure
```

Expected: 新库和旧库迁移均通过；重复种子数量仍为3/12/20/30/3。

- [ ] **Step 5：提交**

```bash
git add database/migrations/004_decimal_values.sql database/seeds/demo.sql tests/integration/schema_test.cpp
git commit -m "feat: migrate business values to decimals"
```

## Task 6：迁移仓库与业务服务

**Files:**

- Modify: `server/repositories/user_repository.*`
- Modify: `server/repositories/order_repository.*`
- Modify: `server/repositories/wallet_repository.*`
- Modify: `server/services/user_service.*`
- Modify: `server/services/station_service.*`
- Modify: `server/services/order_service.*`
- Modify: `server/services/wallet_service.*`
- Modify: `server/services/admin_service.*`
- Modify: `tests/helpers/test_database.h`
- Modify: `tests/unit/user_service_test.cpp`
- Modify: `tests/unit/station_service_test.cpp`
- Modify: `tests/integration/reservation_race_test.cpp`
- Modify: `tests/integration/payment_idempotency_test.cpp`
- Modify: `tests/integration/admin_service_test.cpp`

- [ ] **Step 1：先将测试断言切换为新单位**

示例断言：

```cpp
QCOMPARE(testDb.scalar("SELECT balance_yuan FROM user_info WHERE id=?", {userId}).toString(),
         QStringLiteral("100.00"));
QCOMPARE(stopped.energyKwh.toString(), QStringLiteral("8.35"));
QCOMPARE(stopped.unitPriceYuanPerKwh.toString(), QStringLiteral("1.23"));
QCOMPARE(stopped.amountYuan.toString(), QStringLiteral("10.27"));
QCOMPARE(testDb.scalar("SELECT balance_yuan FROM user_info WHERE id=?", {userId}).toString(),
         QStringLiteral("88.00"));
```

增加`8.35 × 1.23 = 10.27`、`8.35 × 1.25 = 10.44`和余额不足整体回滚用例。

- [ ] **Step 2：运行并确认测试失败**

```bash
cmake --build build
ctest --test-dir build -R "user_service_test|station_service_test|reservation_race_test|payment_idempotency_test|admin_service_test" --output-on-failure
```

Expected: 因旧字段和旧类型仍存在而失败。

- [ ] **Step 3：迁移公共结构和方法签名**

采用以下命名：

```cpp
DecimalValue balanceYuan;
DecimalValue priceYuanPerKwh;
DecimalValue energyKwh;
DecimalValue amountYuan;

OrderResult recordTelemetry(const QString &orderId, const DecimalValue &energyKwh);
WalletResult recharge(qint64 userId, const DecimalValue &amountYuan,
                      const QString &requestId);
DecimalValue totalRevenueYuan() const;
```

仓库从SQLite读取时必须使用`parseCanonical`；解析失败应返回`DB_ERROR`，不能默认为零。

- [ ] **Step 4：迁移订单计算**

停止充电时：

```cpp
const DecimalValue amountYuan = multiplyRounded(order->energyKwh, *unitPriceYuanPerKwh);
```

支付事务继续保持“余额、流水、订单、充电桩”四项同成同败。资金流水支出使用：

```cpp
const DecimalValue debit = DecimalValue{} - order->amountYuan;
```

- [ ] **Step 5：迁移营收汇总**

SQLite不对十进制文本直接执行`SUM`。查询符合条件的`amount_yuan`文本，在服务端逐项`parseCanonical`后使用`DecimalValue`累加；任一非法值使该查询明确失败或返回内部错误，不得静默忽略。

- [ ] **Step 6：运行定向测试**

```bash
cmake --build build
ctest --test-dir build -R "user_service_test|station_service_test|reservation_race_test|payment_idempotency_test|admin_service_test" --output-on-failure
```

Expected: 全部通过。

- [ ] **Step 7：提交**

```bash
git add shared/domain server/repositories server/services tests/helpers tests/unit tests/integration
git commit -m "feat: use decimal values across services"
```

## Task 7：升级TCP协议并验证端到端闭环

**Files:**

- Modify: `server/network/request_router.cpp`
- Modify: `tests/e2e/member1_flow.cpp`
- Modify: `tests/integration/server_health_test.cpp`
- Modify: `docs/protocol.md`

- [ ] **Step 1：先修改端到端测试请求和断言**

请求字段：

```json
{"energyKwh":"8.35"}
{"amountYuan":"100.00"}
{"priceYuanPerKwh":"1.23"}
```

响应必须包含：

```json
{"energyKwh":"8.35","unitPriceYuanPerKwh":"1.23","amountYuan":"10.27"}
```

测试还必须确认数字类型`8.35`、科学计数`"8.35e0"`和带单位`"8.35kWh"`被拒绝为`PARAM_INVALID`，拒绝后连接仍可继续请求。

- [ ] **Step 2：运行并确认失败**

```bash
cmake --build build --target member1_flow_test server_health_test
ctest --test-dir build -R "member1_flow_test|server_health_test" --output-on-failure
```

Expected: 旧路由不识别v2字段，测试失败。

- [ ] **Step 3：实现路由严格解析和固定格式输出**

统一模式：

```cpp
const QJsonValue raw = message.payload.value(QStringLiteral("energyKwh"));
const auto energy = raw.isString() ? DecimalValue::parseRounded(raw.toString()) : std::nullopt;
if (!energy || energy->isNegative()) {
    return errorResponse(message.requestId, QStringLiteral("PARAM_INVALID"));
}
```

输出只使用：

```cpp
payload.insert(QStringLiteral("energyKwh"), result.energyKwh.toString());
payload.insert(QStringLiteral("amountYuan"), result.amountYuan.toString());
```

- [ ] **Step 4：把协议文档提升为v2.0**

文档列出新旧字段映射、字符串类型、单位、四舍五入示例和成员2/3切换要求；v1标记为历史，不再作为活动开发输入。

- [ ] **Step 5：运行闭环测试**

```bash
cmake --build build
ctest --test-dir build -R "server_health_test|member1_flow_test" --repeat until-fail:3 --output-on-failure
```

Expected: 连续三轮全部通过，订单完成、充电桩释放、支付仅一笔、营收增加`"10.27"`。

- [ ] **Step 6：提交**

```bash
git add server/network tests/e2e tests/integration/server_health_test.cpp docs/protocol.md
git commit -m "feat: publish decimal protocol v2"
```

## Task 8：升级分析契约与导出

**Files:**

- Create: `analytics/contracts/charge_event_v2.schema.json`
- Modify: `scripts/export-analysis-sample.sh`
- Modify: `tests/integration/analytics_export_contract_test.cpp`
- Modify: `docs/analytics-data-contract.md`
- Modify: `docs/data-dictionary.md`

- [ ] **Step 1：先修改契约测试**

测试要求字段：

```text
energy_kwh
amount_yuan
```

JSON Schema定义为字符串并限制两位小数：

```json
"energy_kwh": {"type":"string","pattern":"^[0-9]+\\.[0-9]{2}$"},
"amount_yuan": {"type":"string","pattern":"^[0-9]+\\.[0-9]{2}$"}
```

同时断言v2契约不含`energy_wh`和`amount_cents`。

- [ ] **Step 2：运行并确认失败**

```bash
cmake --build build --target analytics_export_contract_test
ctest --test-dir build -R analytics_export_contract_test --output-on-failure
```

Expected: `charge_event_v2.schema.json`尚不存在，测试失败。

- [ ] **Step 3：建立v2契约并修改导出**

导出表头固定使用：

```text
event_id,station_id,pile_id,occurred_at,ingested_at,timezone,energy_kwh,duration_seconds,amount_yuan,status,data_mode
```

导出值直接来自规范化数据库文本；不得重新用Shell浮点运算转换。输出继续排除手机号、姓名、支付账号和管理员信息。

- [ ] **Step 4：运行契约和30条导出验证**

```bash
ctest --test-dir build -R analytics_export_contract_test --output-on-failure
./scripts/export-analysis-sample.sh
```

Expected:

```text
CONTRACT=charge_event_v2
DATA_MODE=SIMULATED
RECORDS=30
INVALID_ROWS=0
```

- [ ] **Step 5：提交**

```bash
git add analytics/contracts/charge_event_v2.schema.json scripts/export-analysis-sample.sh tests/integration/analytics_export_contract_test.cpp docs/analytics-data-contract.md docs/data-dictionary.md
git commit -m "feat: add decimal analytics contract v2"
```

## Task 9：更新README与验收说明

**Files:**

- Modify: `README.md`
- Modify: `docs/test-results/stage-1-member1-acceptance.md`

- [ ] **Step 1：重写README当前状态**

README必须包含：

```text
charge_platform_project
Ubuntu 22.04 / Qt 6.2.4 / CMake 3.22.1 / Boost.Multiprecision
阶段一：完整界面与模拟数据，9月10日验收
阶段二：真实或公开数据与大数据分析，9月17日全量验收
成员1服务端范围已通过，团队三端联调未完成
金额/余额：元，两位小数
电量：kWh，两位小数
单价：元/kWh，两位小数
四舍五入：10.2705→10.27，10.275→10.28
构建、测试和运行命令
核心文档入口
```

删除`StudentMS`标题和“尚未开始业务代码开发”的过时描述。

- [ ] **Step 2：补充验收记录**

记录协议v2、数据库004、测试数量、四舍五入边界、迁移结果和分析导出结果。不得把成员2、3未交付内容写成已完成。

- [ ] **Step 3：检查文档一致性**

```bash
git grep -nE "amountCents|energyWh|amount_cents|energy_wh|balance_cents|price_cents_per_kwh" -- README.md docs analytics server shared tests database scripts
```

Expected: 只允许出现在迁移兼容测试、旧字段映射说明和历史v1契约中；活动v2代码和说明不得出现。

- [ ] **Step 4：提交**

```bash
git add README.md docs/test-results/stage-1-member1-acceptance.md
git commit -m "docs: update decimal project status"
```

## Task 10：全量验证与交付检查

**Files:** 仅在测试发现真实缺陷时修改对应责任文件，不做无关重构。

- [ ] **Step 1：全新配置和构建**

```bash
cmake -S . -B build -G Ninja -DBUILD_TESTING=ON
cmake --build build
```

Expected: 无编译错误。

- [ ] **Step 2：运行全部测试**

```bash
ctest --test-dir build --output-on-failure
```

Expected: 全部通过，0项失败。

- [ ] **Step 3：重复关键测试**

```bash
ctest --test-dir build -R "decimal_value_test|schema_test|reservation_race_test|payment_idempotency_test|admin_service_test|member1_flow_test" --repeat until-fail:10 --output-on-failure
```

Expected: 关键测试连续10轮通过。

- [ ] **Step 4：验证模拟分析导出**

```bash
./scripts/export-analysis-sample.sh
```

Expected: v2契约、30条记录、0条非法记录，金额和电量全部为两位小数。

- [ ] **Step 5：检查工作区和格式**

```bash
git diff --check
git status --short
git log --oneline --decorate -12
```

Expected: 无格式错误；只有计划内文件发生变化；功能提交记录清晰。

- [ ] **Step 6：更新最终验收记录并提交**

```bash
git add docs/test-results/stage-1-member1-acceptance.md
git commit -m "docs: verify decimal stage one services"
```

- [ ] **Step 7：停止在推送前检查点**

不得自动执行`git push`或合并`main`。向用户报告完整测试结果、提交列表、剩余成员2/3同步事项和GitHub首次推送影响，得到明确确认后再操作。
