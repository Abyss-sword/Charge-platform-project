# 成员2 Shared UI：QSS 覆盖记录

日期：2026-09-06

## 已覆盖

- 通用 QWidget、文字层级、页面背景和卡片/渐变卡片
- QPushButton/QToolButton：默认、悬停、按下、焦点、禁用、主按钮、危险按钮
- QLineEdit/QComboBox：正常、焦点、错误属性
- QTableView/QTableWidget 与表头基础样式
- 车主端 PageHeader、底部导航和运营端深海蓝侧栏/导航
- ConfirmDialog 普通/危险模式
- Toast 四种结果状态
- StatusBadge 的成功、信息、警告、错误 tone

## 当前未覆盖

- 复杂表格排序、分页、空数据行组件
- ConfirmDialog 的更细粒度按钮布局和平台原生无障碍主题
- Toast 的位置动画和多条消息队列

这些控件行为属于后续运营业务页面和动效工作包；本轮只补齐基础样式，不扩展业务逻辑。
