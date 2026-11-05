import { mount } from '@vue/test-utils'
import { afterAll, afterEach, beforeAll, describe, expect, it, vi } from 'vitest'
import App from '../src/App.vue'

let widthSpy: ReturnType<typeof vi.spyOn>
let heightSpy: ReturnType<typeof vi.spyOn>

describe('Stage2 formal dashboard foundation', () => {
  beforeAll(() => {
    widthSpy = vi.spyOn(HTMLElement.prototype, 'clientWidth', 'get').mockReturnValue(1920)
    heightSpy = vi.spyOn(HTMLElement.prototype, 'clientHeight', 'get').mockReturnValue(1080)
  })

  afterAll(() => {
    widthSpy.mockRestore()
    heightSpy.mockRestore()
  })

  afterEach(() => {
    vi.restoreAllMocks()
    delete document.documentElement.dataset.motion
  })

  it('mounts without requesting an unfrozen API', () => {
    const fetchSpy = vi.spyOn(globalThis, 'fetch')
    const wrapper = mount(App)
    expect(wrapper.find('.stage2-dashboard').exists()).toBe(true)
    expect(fetchSpy).not.toHaveBeenCalled()
  })

  it('separates online, historical, and quality domains', () => {
    const wrapper = mount(App)
    expect(wrapper.get('#online-business').text()).toContain('在线业务区')
    expect(wrapper.get('#historical-analysis').text()).toContain('历史分析区')
    expect(wrapper.get('#quality-batch').text()).toContain('质量与批次区')
    expect(wrapper.get('.source-ledger').text()).toContain('在线业务来源')
    expect(wrapper.get('.source-ledger').text()).toContain('历史分析来源')
  })

  it.each([
    ['unavailable', '分析服务暂不可用'],
    ['empty', '暂无分析数据'],
    ['expired', '分析数据已过期'],
  ] as const)('renders the %s state without business values', (state, copy) => {
    const wrapper = mount(App, { props: { initialStatus: state } })
    expect(wrapper.attributes('data-status')).toBe(state)
    expect(wrapper.get('.formal-state').text()).toContain(copy)
    expect(wrapper.findAll('.chart-placeholder')).toHaveLength(6)
    expect(wrapper.text()).not.toMatch(/\b\d+(?:\.\d+)?\s*(?:kWh|kW|%|台|元)\b/)
  })

  it('provides at least six distinct chart types with explicit empty states', () => {
    const wrapper = mount(App)
    const kinds = wrapper.findAll('[data-chart-kind]').map(item => item.attributes('data-chart-kind'))
    expect(new Set(kinds)).toEqual(new Set(['line', 'bar', 'donut', 'heatmap', 'gauge', 'scatter']))
    for (const chart of wrapper.findAll('[data-chart-kind]')) {
      expect(chart.text()).toContain('等待正式分析数据')
      expect(chart.text()).toContain('未接入')
    }
  })

  it('renders DataV framing and keeps the required responsive structure', () => {
    const wrapper = mount(App)
    expect(wrapper.find('.dv-border-box-8.datav-frame').exists()).toBe(true)
    expect(wrapper.find('.dv-decoration-5.datav-heading').exists()).toBe(true)
    expect(wrapper.attributes('data-responsive')).toBe('dashboard')
    expect(wrapper.findAll('.dashboard-region')).toHaveLength(3)
    expect(wrapper.findAll('.chart-grid')).toHaveLength(2)
  })

  it('allows motion to be disabled without changing content', async () => {
    const wrapper = mount(App)
    const chartCount = wrapper.findAll('[data-chart-kind]').length
    const toggle = wrapper.get('.motion-toggle')
    expect(document.documentElement.dataset.motion).toBe('on')
    await toggle.trigger('click')
    expect(document.documentElement.dataset.motion).toBe('off')
    expect(toggle.attributes('aria-pressed')).toBe('true')
    expect(wrapper.findAll('[data-chart-kind]')).toHaveLength(chartCount)
  })

  it('does not expose demo, fixture, simulator, or fixed forecast copy', () => {
    const text = mount(App).text()
    for (const forbidden of ['SIMULATED', 'mock', 'fixture', '教学', '模拟器', '固定预测', 'FastAPI']) {
      expect(text.toLowerCase()).not.toContain(forbidden.toLowerCase())
    }
  })
})
