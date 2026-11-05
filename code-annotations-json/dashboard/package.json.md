# dashboard\package.json 逐行注释

文件职责：声明成员3浅色 Web 大屏的包名、ESM 类型、启动/构建/测试脚本，以及运行时与开发依赖。

原始文件：`dashboard\package.json`

| 行号 | 原代码 | 详细注释 |
| ---: | --- | --- |
| 1 | <code>{</code> | npm 包清单根对象开始。Vite、Vitest、`vue-tsc` 都读这份文件找脚本和依赖，不是业务数据。 |
| 2 | <code>  "name": "charge-platform-dashboard",</code> | 包名，对应仓库 `dashboard/` 工程。私有课程项目，名称只用于本地识别，不发布到 npm。 |
| 3 | <code>  "private": true,</code> | 禁止 `npm publish`。防止误把课程大屏当成公开包上传。 |
| 4 | <code>  "version": "1.0.0",</code> | 工程版本号。与课程阶段标签无关，满足 npm 对 `version` 字段的必填要求。 |
| 5 | <code>  "type": "module",</code> | 按 ES Module 解析 `.js`。这样 `vite.config.ts`、测试和源码可以使用 `import`，与 Vue 3 + Vite 7 默认一致。 |
| 6 | <code>  "scripts": {</code> | npm 脚本字典开始。答辩机上用 `npm run dev` / `test:run` 等短命令，避免手打一长串 Vite 参数。 |
| 7 | <code>    "dev": "vite --configLoader runner --host 127.0.0.1 --port 4173",</code> | 开发服务器：只绑回环地址 127.0.0.1、端口 4173（与 preview 同一端口，文档只记一个 URL）。`--configLoader runner` 让 Vite 7 用 runner 加载 TypeScript 配置，避免答辩机因配置加载器差异起不来。 |
| 8 | <code>    "preview": "vite preview --configLoader runner --host 127.0.0.1 --port 4173",</code> | 预览已构建产物，同样绑 127.0.0.1:4173。用于验收「build 之后的静态页」而不是热更新开发页。 |
| 9 | <code>    "build": "vue-tsc --noEmit &amp;&amp; vite build --configLoader runner",</code> | 先用 `vue-tsc --noEmit` 做 Vue+TS 类型检查（不输出 JS），通过后再 `vite build`。任一失败则整命令失败，避免带着类型错误打包。 |
| 10 | <code>    "test": "vitest --configLoader runner",</code> | 监视模式跑 Vitest，改测试文件会重跑。日常开发用；CI/验收不用这个，以免进程挂起。 |
| 11 | <code>    "test:run": "vitest --configLoader runner --run"</code> | 单次跑完全部测试后退出。课程验收和脚本调用这条，对应 `dashboard/tests/dashboard.test.ts`。 |
| 12 | <code>  },</code> | 脚本字典结束。没有 `lint`/`format` 脚本，范围收敛到能演示和能测。 |
| 13 | <code>  "dependencies": {</code> | 运行时依赖：打进浏览器包的库。只放页面真正 import 的两项，减小安装面。 |
| 14 | <code>    "echarts": "^6.0.0",</code> | 图表库。负荷曲线、站点对比等由 `echarts` 绘制；`vite.config.ts` 把它打进独立 `charts` chunk，减轻首屏。 |
| 15 | <code>    "vue": "^3.5.0"</code> | Vue 3 运行时。`App.vue`、适配器和各面板组件都依赖它；大版本锁定在 3.x。 |
| 16 | <code>  },</code> | 运行时依赖结束。分析 API 用浏览器 `fetch` 访问同源 `/api`，不需要 axios。 |
| 17 | <code>  "devDependencies": {</code> | 开发/测试/打包依赖：不会作为业务运行时单独再装一份到生产 CDN，只在 `dashboard/` 开发安装。 |
| 18 | <code>    "@vitejs/plugin-vue": "^6.0.0",</code> | Vite 的 Vue 单文件组件插件，编译 `.vue` 的 template/script/style。版本与 Vite 7 配套。 |
| 19 | <code>    "@types/node": "^24.0.0",</code> | Node 类型声明。`vite.config.ts` 和测试里用到的 Node 全局/路径类型由此提供。 |
| 20 | <code>    "@vue/test-utils": "^2.4.6",</code> | Vue 组件测试工具。挂载面板、触发场景切换等测试用它，而不是起真实浏览器点按钮。 |
| 21 | <code>    "jsdom": "^26.0.0",</code> | 在 Node 里模拟 DOM。`vite.config.ts` 的 `test.environment = 'jsdom'` 依赖本包，才能测适配器和带 DOM 的组件。 |
| 22 | <code>    "typescript": "^5.8.0",</code> | TypeScript 编译器核心。`vue-tsc` 和 `tsconfig.json` 都建立在这个版本上。 |
| 23 | <code>    "vite": "^7.0.0",</code> | 开发服务器与打包器。`--configLoader runner`、代理 `/api` 到 FastAPI 8000 端口都由 Vite 完成。 |
| 24 | <code>    "vitest": "^3.2.0",</code> | 与 Vite 共用配置的测试运行器。`test`/`test:run` 脚本调用它，globals 模式由 tsconfig 的 `types` 打开。 |
| 25 | <code>    "vue-tsc": "^3.0.0"</code> | 理解 `.vue` 的 tsc 封装。`build` 脚本第一步调用，保证模板里的 props/类型错误在打包前暴露。 |
| 26 | <code>  }</code> | 开发依赖结束。没有 UI 框架、没有状态库：样式用项目 tokens，状态用 Vue 组件内数据。 |
| 27 | <code>}</code> | 清单结束。锁版本细节在 `package-lock.json`（工具生成，本目录不做逐行注释）。 |

---

【文件讲解·答辩用】

**功能作用。** 这是大屏工程的「启动说明书」：别人克隆仓库后，靠它知道用 Vue 3 + Vite 7 + ECharts 6，以及如何 `dev`/`build`/`test:run`。没有这份文件就没有可复现的前端工具链。

**技术原理。** npm 以 JSON 描述图依赖；`type: module` 决定加载语义；`scripts` 把答辩机差异（配置加载器、绑定地址、端口）收口成固定命令。`private` 防止误发布。运行时与开发依赖分开，浏览器包只含 Vue 和 ECharts，测试工具留在开发机。

**对应 JSON 的设计思路。** 端口固定 4173、host 固定 127.0.0.1，是为了文档和代理简单、不对外网暴露开发服务。所有 Vite/Vitest 命令带 `--configLoader runner`，是为了 TypeScript 配置在 Vite 7 下稳定加载。`build` 把类型检查和打包做成短路与，体现「类型不过不去就不能交构建产物」。依赖尽量少，降低课程机安装失败率。

**与代码关系。** `vite.config.ts` 的 `@vitejs/plugin-vue`、`/api` 代理、`echarts` manualChunks、`vitest` jsdom 环境，都假定这些包已按本文件安装。`src/App.vue`、`adapters/dashboardDataAdapter.ts` 运行时只依赖 `vue` 与 `echarts`。验收脚本应调用 `npm run test:run` 而不是 `npm test`。

**答辩讲法。** 「大屏是独立 npm 包，不塞进分析 Python。」指 `dependencies` 只有 Vue 和 ECharts，指 `dev` 绑 127.0.0.1:4173。若问 lock 文件：锁的是精确版本，不能手写注释，设计意图以本 `package.json` 为准。
