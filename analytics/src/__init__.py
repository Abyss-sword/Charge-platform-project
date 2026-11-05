# src 子包入口：流水线、只读 API、模拟器控制都作为这个包被导入。
"""Traceable analytics pipeline for the teaching charging platform."""  # 声明本子包是可追溯的教学充电平台分析流水线。

__version__ = "1.0.0"  # 与 pyproject.toml 的 version 对齐，供健康检查或文档引用。

# 【文件讲解·答辩用】
# 功能作用：把 analytics/src 变成可 import 的子包，并暴露版本号。
# 技术原理：相对导入（from .sqlite_dashboard import ...）依赖这里作为包根。
# 代码思路：不在 __init__ 里再导出全部符号，调用方按需 from .api / .ingest_raw 导入，减少耦合。
# 与其他文件关系：api.py、demo_pipeline.py、sqlite_dashboard.py 都挂在这个包下。
# 答辩时可以怎么讲：成员3代码集中在 src；版本 1.0.0 对应本学期可答辩的流水线与只读大屏。
