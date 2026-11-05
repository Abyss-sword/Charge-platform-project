import { ref } from 'vue' // 白昼/黑夜共用一份响应式状态，顶栏和图都读它。

export type UiTheme = 'day' | 'night' // 界面模式：白昼浅色，黑夜暗色。
export const UI_THEME_KEY = 'dashboard-ui-theme' // localStorage 键，刷新后保持上次选择。

const theme = ref<UiTheme>('day') // 模块级单例，避免多个组件各记一份。

export function readUiTheme(): UiTheme { // 只认显式存过的 night，缺省回白昼。
  try { // 隐私模式读存储可能抛错。
    return localStorage.getItem(UI_THEME_KEY) === 'night' ? 'night' : 'day' // 其它值都当白昼。
  } catch { // 读失败不挡启动。
    return 'day' // 回退白昼。
  } // 结束try。
} // 结束 readUiTheme。

export function applyUiTheme(next: UiTheme) { // 写到 html，供 CSS 和 color-scheme 使用。
  theme.value = next // 同步响应式，LoadChart 会跟着重绘。
  document.documentElement.dataset.theme = next // tokens.css 用 [data-theme=night] 换色。
  document.documentElement.style.colorScheme = next === 'night' ? 'dark' : 'light' // 滚动条等系统控件。
  document.querySelector('meta[name="color-scheme"]')?.setAttribute('content', next === 'night' ? 'dark' : 'light') // 同步 meta，避免浏览器表单控件反色。
} // 结束 applyUiTheme。

export function persistUiTheme(next: UiTheme) { // 应用并记住，下次打开还是这套。
  try { // 写入失败只影响记忆，不挡换肤。
    localStorage.setItem(UI_THEME_KEY, next) // 存 day 或 night。
  } catch { // 忽略存储失败。
    /* 隐私模式写不进去时，本次会话仍可切换。 */
  } // 结束try。
  applyUiTheme(next) // 立刻改 DOM。
} // 结束 persistUiTheme。

export function initUiTheme() { // 启动时恢复上次选择，默认白昼。
  applyUiTheme(readUiTheme()) // 先于 Vue 挂载调用，减少闪白。
} // 结束 initUiTheme。

export function useUiTheme() { // 顶栏开关和图表共用。
  return { // 返回同一份 ref。
    theme, // 当前模式。
    setTheme: persistUiTheme, // 点「白昼/黑夜」时调用。
  } // 结束返回值。
} // 结束 useUiTheme。
/*
【文件讲解·答辩用】
功能作用：大屏白昼/黑夜两套界面，只改 CSS 和图表颜色，不改任何业务数字。
技术原理：html[data-theme] 驱动 tokens.css；选择写入 localStorage。ECharts 读 CSS 变量后重绘。
代码思路：theme 做成模块单例，避免顶栏和图各记一份。缺省白昼，只有显式选黑夜才变暗。
与其他文件关系：main.ts 启动时 initUiTheme；DataSourceBar 放开关；LoadChart watch theme。
答辩时可以怎么讲：这是投影环境切换。教室亮就白昼，关灯演示就黑夜。数据接口不变。
*/
