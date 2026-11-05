# 把 analytics 目录登记成 Python 包，测试和 uvicorn 才能按包路径导入。
"""Member 3 analytics package root."""  # 模块说明：成员3数据分析包的根。

# 【文件讲解·答辩用】
# 功能作用：标记 analytics 是一个可导入的 Python 包，本身不含业务逻辑。
# 技术原理：目录内有 __init__.py，解释器才把它当包而不是普通文件夹。
# 代码思路：只放包说明，不在这里 import FastAPI 或 sklearn，避免无关启动成本。
# 与其他文件关系：真正实现在 analytics/src/；安装布局由 pyproject.toml 的 packages=["src"] 决定。
# 答辩时可以怎么讲：根目录这个文件只是包入口占位，流水线、只读大屏 API、场景按钮都在 src 里。
