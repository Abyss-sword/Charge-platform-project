# dashboard\tsconfig.json 逐行注释

文件职责：收紧成员3大屏 TypeScript 的编译目标、模块解析、JSX 策略、类型入口和纳入编译的文件范围。

原始文件：`dashboard\tsconfig.json`

| 行号 | 原代码 | 详细注释 |
| ---: | --- | --- |
| 1 | <code>{</code> | TypeScript 配置根对象。`vue-tsc` 和编辑器都读它；Vite 自己打包 JS，但类型检查以本文件为准。 |
| 2 | <code>  "compilerOptions": {</code> | 编译选项开始。下面三行把多项选项写在同一行，是合法 JSON 压缩排版，注释按「整行」说明其中每一个键。 |
| 3 | <code>    "target": "ES2022", "module": "ESNext", "moduleResolution": "Bundler", "strict": true,</code> | `target=ES2022`：输出/类型检查按较新的 JS 语法理解。`module=ESNext`：保留 `import`/`export`。`moduleResolution=Bundler`：按 Vite 的包解析规则找模块（含 `exports` 条件）。`strict=true`：打开全部严格检查，适配器里的空数据/失败状态必须显式建模，不能偷偷 `any` 滑过。 |
| 4 | <code>    "jsx": "preserve", "resolveJsonModule": true, "esModuleInterop": true, "lib": ["ESNext", "DOM", "ESNext.Disposable"],</code> | `jsx=preserve`：JSX/TSX 留给 Vue 插件处理，tsc 不擅自变成 `React.createElement`。`resolveJsonModule=true`：允许 `import` JSON（若测试或配置需要）。`esModuleInterop=true`：默认导入 CJS 库更顺。`lib` 提供 ES 最新内置、浏览器 DOM，以及 `Disposable`（`using` 等资源协议）的类型，避免 DOM 类型缺失导致 `document` 报错。 |
| 5 | <code>    "types": ["node", "vitest/globals"], "skipLibCheck": true</code> | `types` 白名单：纳入 Node 类型（给 Vite 配置）和 Vitest 全局 `describe`/`it`/`expect`，测试文件不必逐个 import。`skipLibCheck=true`：不深挖 `node_modules` 里别人的 `.d.ts` 冲突，课程机上 ECharts/Vue 类型偶发不兼容时仍能检查我们自己的源码。 |
| 6 | <code>  },</code> | `compilerOptions` 结束。没有 `paths` 别名，导入一律相对路径，减少 Vite 与 tsc 两套解析不一致。 |
| 7 | <code>  "include": ["src/**/*.ts", "src/**/*.vue", "tests/**/*.ts"]</code> | 只编译大屏源码和测试：`src` 下 ts/vue，`tests` 下 ts。不把 `dist/`、`node_modules/` 算进工程，避免类型检查扫到构建产物。 |
| 8 | <code>}</code> | 配置结束。8 行保持短小，和 `package.json` 的 `vue-tsc --noEmit` 直接配合。 |

---

【文件讲解·答辩用】

**功能作用。** 给 Vue 大屏一套统一的 TypeScript 尺子：什么文件要检查、检查多严、按哪种模块和 DOM 环境理解代码。没有它，`npm run build` 的第一步 `vue-tsc` 不知道该查哪些文件。

**技术原理。** tsc 不负责 Vite 打包，但负责在打包前证明 `dashboardDataAdapter.ts`、`.vue` 脚本和测试在严格模式下可类型通过。`moduleResolution: Bundler` 与 Vite 解析对齐；`jsx: preserve` 把模板编译留给 `@vitejs/plugin-vue`；`vitest/globals` 让测试风格接近课程示例。

**对应 JSON 的设计思路。** 选项挤在三行是为了文件短、评审时一屏看完，但每个键都有含义：严格模式、浏览器 lib、跳过第三方类型坑。`include` 明确「只看 src 和 tests」，防止误把分析 Python 或根目录脚本加进来。不写 `exclude` 是因为默认已排除 `node_modules`。

**与代码关系。** `package.json` 的 `build` 调用 `vue-tsc --noEmit`，读的就是本文件。`vite.config.ts` 需要 Node 类型；`tests/dashboard.test.ts` 需要 Vitest globals；`src/**/*.vue` 必须出现在 `include` 里，否则面板组件改错类型检查看不见。运行时行为仍由 Vite 决定，本文件只管类型。

**答辩讲法。** 「这不是业务 JSON，是大屏的编译合同。」点出 `strict: true` 和 `include` 三套 glob。若问为什么和 `package.json` 分开：npm 管依赖和脚本，tsconfig 管类型语言；JSON 都不能写注释，所以说明都在 md 里。
