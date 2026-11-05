import { flushPromises, mount } from '@vue/test-utils' // 挂载 App 并刷完微任务，才能等到 onMounted 里的 refresh。
import { afterEach, describe, expect, it, vi } from 'vitest' // Vitest API：用例、断言、mock。
import App from '../src/Stage1Dashboard.vue' // 被测根组件。
import { getDashboardView, loadDashboard } from '../src/adapters/dashboardDataAdapter' // 预览模板与实时加载函数。
import { applyUiTheme, UI_THEME_KEY } from '../src/theme' // 每例结束把界面模式打回白昼。

describe('dashboard adapter', () => { // 适配器单测：不依赖真实 FastAPI。
  afterEach(() => vi.unstubAllGlobals()) // 每例结束恢复 fetch，避免串案。

  it.each(['loading', 'normal', 'empty', 'api-error', 'model-error', 'map-error'] as const)( // 六种预览态都要带齐数据源元字段。
    'returns complete source metadata for %s', // 用例标题带状态名。
    state => { // 参数化回调。
      const view = getDashboardView(state) // 取该状态的固定预览对象。
      expect(view.dataMode).toBe('SIMULATED') // 预览一律标模拟。
      expect(view.datasetVersion).toBeTruthy() // 版本非空。
      expect(view.coverage).toBeTruthy() // 覆盖说明非空。
      expect(view.updatedAt).toBeTruthy() // 更新时间非空。
      expect(view.dataTimestamp).toBeTruthy() // 业务时间非空。
      expect(view.refreshIntervalSeconds).toBe(3) // 默认 3 秒刷新。
    }, // 结束单状态回调。
  ) // 结束 it.each 参数化。

  it('does not invent data or copy actual values when sources are unavailable', () => { // 失败时不准拿实际值去填预测或 KPI。
    expect(getDashboardView('api-error').metrics).toHaveLength(0) // 接口失败预览清空 KPI。
    expect(getDashboardView('api-error').stale).toBe(true) // 标陈旧。
    expect(getDashboardView('model-error').load.every(item => item.forecast === null)).toBe(true) // 预测不足则全是 null。
  }) // 结束用例 does not invent da。

  it('exposes snapshotVersion metadata in preview views', () => { // S1-D：预览对象也要带快照元字段。
    expect(getDashboardView('normal').snapshotVersion).toBe(0) // 预览没有联动版本，按 0 展示。
    expect(getDashboardView('normal').snapshotAtUtc).toBeTruthy() // 快照时间字段存在。
  }) // 结束用例 exposes snapshotVersion。

  it('drops stale responses with older snapshotVersion and keeps the current view', async () => { // S1-D：旧快照丢弃契约。
    const live = { // 上次真实成功画面，快照版本 9。
      ...getDashboardView('normal'),
      sourceStatus: 'connected' as const,
      stale: false,
      snapshotVersion: 9,
      snapshotAtUtc: '2026-09-13T12:00:00+08:00',
    } // 结束live。
    const stalePayload = { // 伪造一个旧版本响应。
      ...live,
      snapshotVersion: 8, // 比当前 9 小 → 必须丢弃。
      updatedAt: '2026-09-13T11:00:00+08:00',
    } // 结束stalePayload。
    const fetchMock = vi.fn().mockResolvedValueOnce({ ok: true, json: async () => stalePayload })
    vi.stubGlobal('fetch', fetchMock)

    const kept = await loadDashboard(undefined, live) // 旧快照到来。
    expect(kept.snapshotVersion).toBe(9) // 仍显示当前版本。
    expect(kept.stale).toBe(true) // 标记陈旧提示。
    expect(kept.message).toContain('已丢弃') // 横幅说明丢弃动作。

    const freshPayload = { ...stalePayload, snapshotVersion: 10, message: undefined } // 新版本应被接受。
    vi.stubGlobal('fetch', vi.fn().mockResolvedValueOnce({ ok: true, json: async () => freshPayload }))
    const accepted = await loadDashboard(undefined, kept)
    expect(accepted.snapshotVersion).toBe(10) // 新快照进入画面。
    expect(accepted.stale).toBe(false) // 恢复正常。
  }) // 结束用例 drops stale responses。
  it('keeps the last success stale, then recovers automatically', async () => { // 先失败降级，再一次成功应恢复 connected。
    const live = { // 伪装一份“上次真实成功”的画面。
      ...getDashboardView('normal'), // 借用预览结构当 payload。
      sourceStatus: 'connected' as const, // 标记为已连接，这样失败分支会走陈旧降级。
      stale: false, // 成功帧不陈旧。
      updatedAt: '2026-09-09T10:00:00+08:00', // 失败文案会引用这个时间。
    } // 结束live。
    const fetchMock = vi.fn() // 顺序 mock：第一次抛错，第二次成功。
      .mockRejectedValueOnce(new Error('offline')) // 第一次：网络失败。
      .mockResolvedValueOnce({ ok: true, json: async () => live }) // 第二次：返回 live。
    vi.stubGlobal('fetch', fetchMock) // 替换全局 fetch。

    const failed = await loadDashboard(undefined, live) // 不传 state，走真实 fetch 路径。
    expect(failed.metrics.length).toBeGreaterThan(0) // 降级仍保留上次 KPI。
    expect(failed.sourceStatus).toBe('failed') // 连接失败。
    expect(failed.stale).toBe(true) // 陈旧。

    const recovered = await loadDashboard(undefined, failed) // 第二次应吃成功 mock。
    expect(recovered.sourceStatus).toBe('connected') // 恢复已连接。
    expect(recovered.stale).toBe(false) // 不再陈旧。
  }) // 结束用例 keeps the last suc。

  it('uses the official dashboard API and keeps SIMULATED when dataMode is omitted', async () => { // D7：只打正式接口；缺模式不冒充真实脱敏。
    const payload = { // 模拟旧后端漏了 dataMode。
      ...getDashboardView('normal'), // 结构仍合法。
      sourceStatus: 'connected' as const, // 成功连接。
      stale: false, // 非陈旧。
    } // 结束 payload。
    delete (payload as { dataMode?: string }).dataMode // 故意去掉模式。
    const fetchMock = vi.fn().mockResolvedValue({ ok: true, json: async () => payload }) // 一次成功。
    vi.stubGlobal('fetch', fetchMock) // 替换 fetch。
    const view = await loadDashboard() // 走正式入口。
    expect(fetchMock.mock.calls[0]?.[0]).toBe('/api/dashboard') // 不得去读 raw 文件。
    expect(view.dataMode).toBe('SIMULATED') // 缺省保持模拟，不改名为 REAL_ANONYMIZED。
  }) // 结束用例 uses the official da。
}) // 结束测试组 dashboard adapter。

describe('dashboard view', () => { // 根组件渲染测。
  afterEach(() => { // 清 mock 和界面模式，避免影响下一例。
    vi.restoreAllMocks() // 还原 spy。
    vi.unstubAllGlobals() // 还原 fetch。
    localStorage.removeItem(UI_THEME_KEY) // 清掉黑夜记忆。
    applyUiTheme('day') // 主题单例打回白昼。
  }) // 结束 afterEach。

  it('renders metrics, live telemetry, pile states, alarms, and tickets', async () => { // 正常态应看到 KPI、场景入口、遥测和业务面板。
    const live = { ...getDashboardView('normal'), sourceStatus: 'connected' as const, stale: false } // 用预览结构冒充接口 JSON。
    vi.stubGlobal('fetch', vi.fn().mockResolvedValue({ // 任何 fetch 都返回这份 live。
      ok: true, // HTTP 成功。
      json: async () => live, // 解析为 live。
    })) // 结束 fetch mock。
    const wrapper = mount(App, { // 挂根组件。
      global: { // 测试全局配置。
        stubs: { // 不真正初始化 ECharts。
          LoadChart: { template: '<div aria-label="实际用电量与规则估算折线图" />' }, // 占位，保留无障碍标签。
        }, // 结束stubs。
      }, // 结束global。
    }) // 结束wrapper。
    await flushPromises() // 等 onMounted → refresh 完成。
    expect(wrapper.text()).toContain('SIMULATED') // 顶栏模式。
    expect(wrapper.findAll('.metric-card')).toHaveLength(live.metrics.length) // KPI 张数对齐。
    expect(wrapper.text()).toContain('电桩实时遥测') // 遥测面板标题。
    expect(wrapper.text()).toContain('模拟场景切换') // 场景入口标题，证明不是顶部预览条。
    expect(wrapper.text()).toContain('正常充电') // 场景按钮文案。
    expect(wrapper.text()).toContain('26.0 ℃') // 预览遥测温度格式化。
    expect(wrapper.text()).toContain('7.20 kW') // 功率两位小数。
    expect(wrapper.text()).toContain('100.50 kWh') // 累计电表。
    expect(wrapper.text()).toContain('充电桩状态分布') // 状态面板。
    expect(wrapper.text()).toContain('最新运维工单') // 工单面板。
    expect(wrapper.text()).toContain('数据来源与质量') // 质量面板。
    expect(wrapper.text()).toContain('推荐站点') // D9 根页挂上可解释推荐。
    const jumpNav = wrapper.get('.section-nav') // 顶栏只列四大分区。
    for (const label of ['总览', '态势', '设备', '运维']) { // 分区目录。
      expect(jumpNav.text()).toContain(label) // 顶栏能看见四条带。
    } // 结束跳转文案循环。
    expect(wrapper.text()).toContain('趋势') // 态势带里仍有细项入口。
    expect(wrapper.text()).toContain('工单') // 运维带里仍有细项入口。
    const insightJump = jumpNav.findAll('button').find(candidate => candidate.text() === '态势') // 点态势应高亮该分区。
    await insightJump!.trigger('click') // 触发跳转。
    expect(insightJump!.classes()).toContain('active') // 目录高亮当前带。
    wrapper.unmount() // 释放定时器。
  }) // 结束用例 renders metrics, l。

  it('keeps the live view open when the API omits telemetry', async () => { // 旧接口没有 telemetry 时仍保持 normal，只显示空表提示。
    const { telemetry: _ignored, ...legacy } = { // 拆掉 telemetry 模拟后端漏字段。
      ...getDashboardView('normal'), // 其余字段齐。
      sourceStatus: 'connected' as const, // 已连接。
      stale: false, // 不陈旧。
    } // 结束telemetry。
    vi.stubGlobal('fetch', vi.fn().mockResolvedValue({ // 返回无 telemetry 的对象。
      ok: true, // 成功。
      json: async () => legacy, // 缺遥测。
    })) // 结束缺遥测 fetch mock。
    const wrapper = mount(App, { // 挂根组件。
      global: { // 测试全局配置。
        stubs: { // stub 图表。
          LoadChart: { template: '<div aria-label="实际用电量与规则估算折线图" />' }, // 同上一例。
        }, // 结束stubs。
      }, // 结束global。
    }) // 结束wrapper。
    await flushPromises() // 等刷新。
    expect(wrapper.get('[data-state]').attributes('data-state')).toBe('normal') // 不得掉进错误态。
    expect(wrapper.find('.page-state').exists()).toBe(false) // 不应出现失败横幅。
    expect(wrapper.text()).toContain('电桩实时遥测') // 面板还在。
    expect(wrapper.text()).toContain('统一数据库中还没有遥测记录') // 空态文案。
    wrapper.unmount() // 卸载。
  }) // 结束用例 keeps the live vie。

  it('shows every required acceptance state without page reload', async () => { // 顶部预览按钮应能无刷新切六种验收画面。
    vi.stubGlobal('fetch', vi.fn().mockRejectedValue(new Error('offline'))) // 实时刷新会失败，但不妨碍点预览。
    const wrapper = mount(App, { // 挂根组件。
      global: { // 测试全局配置。
        stubs: { // stub 图表。
          LoadChart: { template: '<div />' }, // 最简占位。
        }, // 结束stubs。
      }, // 结束global。
    }) // 结束wrapper。
    await flushPromises() // 等首次失败刷新结束。
    for (const label of ['加载中', '空数据', '接口失败', '数据不足', '地图失败']) { // 逐个点预览按钮（不含「实时」）。
      const button = wrapper.get('.state-switcher').findAll('button') // 只在顶部预览条里找，避免点到场景按钮。
        .find(candidate => candidate.text() === label) // 按可见文案匹配。
      await button!.trigger('click') // 触发预览。
      expect(wrapper.text()).toContain(label === '数据不足' ? '预测数据不足' : label) // 数据不足看横幅长文案，其余看按钮字。
    } // 结束预览按钮循环。
    wrapper.unmount() // 卸载。
  }) // 结束用例 shows every requir。

  it('switches the interface between day and night without changing data', async () => { // 白昼/黑夜只换皮肤。
    const live = { ...getDashboardView('normal'), sourceStatus: 'connected' as const, stale: false } // 真实画面结构。
    vi.stubGlobal('fetch', vi.fn().mockResolvedValue({ ok: true, json: async () => live })) // 不连 FastAPI。
    const wrapper = mount(App, { // 挂根组件。
      global: { stubs: { LoadChart: { template: '<div />' } } }, // stub 图表。
    }) // 结束wrapper。
    await flushPromises() // 等首屏刷新。
    expect(wrapper.get('.theme-switch').text()).toContain('白昼') // 开关在顶栏。
    expect(wrapper.get('.theme-switch').text()).toContain('黑夜') // 两档都在。
    const night = wrapper.get('.theme-switch').findAll('button').find(candidate => candidate.text() === '黑夜') // 点黑夜。
    await night!.trigger('click') // 切换。
    expect(document.documentElement.dataset.theme).toBe('night') // html 打上黑夜。
    expect(localStorage.getItem(UI_THEME_KEY)).toBe('night') // 刷新后还能记住。
    expect(wrapper.findAll('.metric-card')).toHaveLength(live.metrics.length) // 数字还在，没被换肤清掉。
    const day = wrapper.get('.theme-switch').findAll('button').find(candidate => candidate.text() === '白昼') // 点回白昼。
    await day!.trigger('click') // 切回。
    expect(document.documentElement.dataset.theme).toBe('day') // 回到白昼。
    wrapper.unmount() // 卸载。
  }) // 结束用例 switches the inter。

  it('turns motion off from the query string without changing data', async () => { // D10：关动效只改 CSS 标记。
    const current = new URL('http://127.0.0.1:4173/?motion=off') // 验收用查询串。
    vi.stubGlobal('location', current) // jsdom 下替换 location.search。
    vi.stubGlobal('fetch', vi.fn().mockRejectedValue(new Error('offline'))) // 不连真实接口。
    const wrapper = mount(App, { // 挂根组件。
      global: { stubs: { LoadChart: { template: '<div />' } } }, // stub 图表。
    }) // 结束wrapper。
    await flushPromises() // 等 onMounted。
    expect(document.documentElement.dataset.testMotion).toBe('off') // motion.css 会据此停动画。
    wrapper.unmount() // 卸载。
    delete document.documentElement.dataset.testMotion // 不污染后续用例。
  }) // 结束用例 turns motion off。
}) // 结束测试组 dashboard view。
/*
【文件讲解·答辩用】
功能作用：锁住三件答辩易翻车的事：预览元数据齐全、失败不造假、根页面能渲染遥测和场景入口，以及旧接口缺 telemetry 仍保持正常态。
技术原理：Vitest + Vue Test Utils。fetch 全部 stub，不连 8000。LoadChart stub 掉以免 jsdom 里跑 ECharts。flushPromises 等待 App 的 onMounted refresh。
代码思路：adapter 组测 getDashboardView/loadDashboard；view 组 mount(App)。验收按钮测试明确从 .state-switcher 取按钮，避免和 ScenarioSwitcher 的「正常充电」等文案冲突。
与其他文件关系：覆盖 App.vue 与 dashboardDataAdapter.ts。场景按钮文案「正常充电」出现在渲染断言里，证明场景入口已挂上。真实切场景仍需连 FastAPI，本文件不测 TCP。
答辩时可以怎么讲：我们用测试保证“接口挂了不清空上次成功、也不把 preview 数字当生产数据”。老师若问顶部按钮，可以说测试里点的是验收预览，不是模拟器场景。
*/
