# 成员3严格JSON逐行注释索引

本目录不是配置文件，也不是可被程序加载的 JSON。它只解决一件课程约束：**JSON 语法不允许 `//`、`#` 或块注释**。若把说明写进原文件，`json.loads`、`npm`、Vite、JSON Schema 校验器都会直接报错，分析管道和大屏无法启动。

因此成员3采用「原文件保持合法 JSON + 旁路 Markdown 逐行对照」：

- 原 JSON 仍放在仓库业务路径中，继续被 Python / npm / 测试读取。
- 每一行的中文说明放在本目录**同相对路径**的 `.md` 里，表头是「行号 / 原代码 / 详细注释」。
- 行号必须与当前仓库里的真实 JSON 逐行对齐；括号、逗号、数组元素、压缩写在同一行的多个键，都算一行，都要有说明。
- 讲解和答辩材料追加在各 md 文末的【文件讲解·答辩用】，不混进 JSON。

## 对照表（md → 原 JSON）

| 本目录说明文件 | 仓库中的真实 JSON | 作用一句话 |
| --- | --- | --- |
| `analytics/contracts/charge_event_v1.schema.json.md` | `analytics/contracts/charge_event_v1.schema.json` | 历史分析事件契约（Wh / 分），只作版本对照。 |
| `analytics/contracts/charge_event_v2.schema.json.md` | `analytics/contracts/charge_event_v2.schema.json` | 当前活动契约（kWh / 元，两位小数字符串）。 |
| `analytics/manifests/source.example.json.md` | `analytics/manifests/source.example.json` | 模拟数据来源准入清单示例。 |
| `analytics/manifests/station_mapping.json.md` | `analytics/manifests/station_mapping.json` | 来源站点字符串 → 内部站点整数。 |
| `dashboard/package.json.md` | `dashboard/package.json` | 大屏脚本、运行依赖和开发依赖。 |
| `dashboard/tsconfig.json.md` | `dashboard/tsconfig.json` | TypeScript 编译范围和严格选项。 |

## 使用方式（给读代码和答辩的人）

1. 先打开左侧真实 JSON，再打开本目录对应 md，按行号对照，不要把 md 当成可解析配置。
2. 老师问「这一行为什么这样写」时，用 md 里的中文说明回答；问「这个文件在系统里干什么」时，翻到文末【文件讲解·答辩用】。
3. 原 JSON 改动后必须回头改对应 md 的行号和原文，否则对照表会骗人。
4. 不要把这些说明复制回 JSON。也不要给 `dashboard/package-lock.json` 做逐行注释：它是 `npm install` 生成的锁文件，手写注释会让 `npm ci` 失败。依赖设计只通过 `package.json` 及其 md 说明。

## 本 README 本身

本文件只做索引和规则说明，**没有对应的 JSON 行**，因此下面没有「第 N 行原代码」表格。

---

【文件讲解·答辩用】

**功能作用。** 把「代码必须能跑」和「课程要求逐行解释」拆开：业务 JSON 继续给程序用，中文说明集中在本目录，避免污染契约、清单和前端工程配置。

**技术原理。** RFC 8259 / ECMA-404 规定 JSON 只有对象、数组、字符串、数字、布尔和 null，没有注释语法。成员3的 Python 侧用 `json.loads`，前端用 npm 与 TypeScript 编译器读 JSON，任何 `//` 都会成为非法 token。旁路 Markdown 用表格保存「行号 + 原文 + 中文」，不进入运行时。

**对应 JSON 的设计思路。** 成员3真正手写、需要讲清楚的 JSON 只有六份：两份分析契约、两份清单、大屏的 `package.json` 和 `tsconfig.json`。其余 JSON（如 `package-lock.json`、运行时产物、测试夹具）要么是工具生成，要么不是答辩主线，不在本索引里假装成业务配置。

**与代码关系。** `source_registry.load_manifest`、`station_mapping.load_station_mapping`、`normalize_row`、`ingest_source`、Vite/Vitest 脚本都读取原 JSON 路径，从不读取本目录。本 README 只告诉评委「注释在哪、为什么不写进 JSON」。

**答辩讲法。** 先说一句：「JSON 不能写注释，所以说明在 `code-annotations-json/`，原文件保持可解析。」然后指着对照表打开 v2 契约或来源清单，按行讲字段，最后用文末讲解回答「为什么这样设计、和哪段 Python/Vue 对应」。若被问 v1，明确回答「只作历史对照，活动契约是 v2」。
