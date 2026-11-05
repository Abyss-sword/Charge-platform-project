/// <reference types="vite/client" /> // 让 TypeScript 认识 import.meta.env 和 Vite 客户端类型。

declare module '*.vue' { // 声明所有 .vue 文件都可以被 TS 直接 import。
  import type { DefineComponent } from 'vue' // 引用 Vue 组件类型，不引入运行时代码。
  const component: DefineComponent<Record<string, unknown>, Record<string, unknown>, unknown> // 用宽松泛型描述单文件组件。
  export default component // 每个 .vue 默认导出一个组件。
} // 结束 *.vue 模块声明。
/*
【文件讲解·答辩用】
功能作用：给 TypeScript 补环境类型。没有它，import App from './App.vue' 和 import.meta.env 都会报红。
技术原理：Vite 在编译期处理 .vue，但 tsc/Volar 需要模块声明才知道默认导出是组件。三斜线引用 vite/client，补齐 import.meta 的类型。
代码思路：只声明、不实现。DefineComponent 三个泛型用 unknown/空对象占位，因为本项目不在 env.d.ts 里锁每个组件的 props。
与其他文件关系：被 tsconfig 纳入类型检查；main.ts、App.vue 以及所有组件的 import 都依赖这份声明。不参与运行时，也不请求接口。
答辩时可以怎么讲：这是“给编辑器和测试用的类型胶水”，不是业务代码。老师问数据从哪来时，明确说 env.d.ts 不产生任何指标或遥测。
*/
