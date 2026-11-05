import { createApp } from 'vue' // Vue 3 应用工厂，用来创建根实例。
import App from './App.vue' // Stage2 合规大屏根组件。
import Stage1Dashboard from './Stage1Dashboard.vue' // 保留已联动的第一阶段大屏。
import { initUiTheme } from './theme' // 第一阶段大屏仍使用白昼/黑夜设置。
import '@kjgl77/datav-vue3/dist/style.css' // DataV Vue3 组件样式。
import './styles/tokens.css' // 全局浅色色板和布局，答辩默认不是深色主题。
import './styles/motion.css' // 入场与呼吸动效；系统减少动效时会全部关掉。

const stage2 = new URLSearchParams(location.search).get('stage') === '2'
if (!stage2) initUiTheme()
createApp(stage2 ? App : Stage1Dashboard, stage2 ? { autoLoad: true } : {}).mount('#app') // 默认展示第一阶段大屏，?stage=2 打开第二阶段页。
/*
【文件讲解·答辩用】
功能作用：前端真正的启动脚本。创建 Vue 应用、注入全局样式，并挂载到页面。
技术原理：createApp 生成独立应用上下文；mount('#app') 把 App.vue 渲染进 HTML 壳。CSS 在挂载前 import，保证首屏就有浅色商业风，而不是无样式闪一下。
代码思路：只做三件事——引 Vue、引根组件、引两份全局样式。不在这里 fetch，也不在这里写场景切换，避免入口文件变成上帝对象。
与其他文件关系：被 index.html 的 module script 加载；默认渲染 Stage1Dashboard.vue，`?stage=2` 才渲染 App.vue；样式来自 DataV、tokens.css 与 motion.css。
答辩时可以怎么讲：一句话“main.ts 只负责把大屏挂上去”。正式接口冻结后，数据访问会由独立适配层接入，入口文件仍不承担网络职责。
*/
