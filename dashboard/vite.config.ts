import { defineConfig } from 'vite' // 引入 Vite 配置工厂，导出开发与构建选项。
import vue from '@vitejs/plugin-vue' // 让 Vite 能编译 .vue 单文件组件。

export default defineConfig({ // 导出本仓库 dashboard 的 Vite 配置对象。
  plugins: [vue()], // 启用 Vue 插件，处理 template/script/style。
  optimizeDeps: { noDiscovery: true, include: [] }, // 关掉依赖预扫描，减少答辩机首次启动卡住。
  build: { // 生产打包选项，答辩一般用开发服务器，但构建也要能过。
    chunkSizeWarningLimit: 550, // 放宽分包体积告警，ECharts 单独一块会略大。
    rollupOptions: { // 自定义 Rollup 输出，把运行时库拆开。
      output: { // 只改输出分块，不改入口。
        manualChunks: { // 手动分包，首屏少下无关代码。
          vue: ['vue'], // Vue 运行时单独一块。
          charts: ['echarts/core', 'echarts/charts', 'echarts/components', 'echarts/renderers'], // 折线图库单独一块。
        }, // 结束 manualChunks。
      }, // 结束 output。
    }, // 结束 rollupOptions。
  }, // 结束 build。
  test: { environment: 'jsdom', globals: true, css: true }, // Vitest 用 jsdom 跑组件测试，并处理 CSS。
}) // 结束 defineConfig。
/*
【文件讲解·答辩用】
功能作用：配置 Vue 编译、Vitest 和生产分包。
技术原理：正式分析接口尚未冻结，因此不在构建配置中预设代理、端口或旧服务路径。
代码思路：plugins 只挂 Vue；optimizeDeps 降低冷启动风险；manualChunks 将 ECharts 独立分包。
与其他文件关系：index.html 和 src/main.ts 由本配置启动；tests/dashboard.test.ts 使用本文件的 test 字段。
答辩时可以怎么讲：当前构建只提供合规 UI 基础，接口冻结后再通过显式运行配置注入地址。
*/
