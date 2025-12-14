这是一份为**上下文恢复（Context Restoration）**专门设计的项目技术白皮书。

如果你是接手此任务的 AI 助手，请仔细阅读以下内容。这份文档概括了从项目启动到目前为止（`chess_death_map_v2` 阶段）的所有架构决策、核心算法、数据结构约定以及可视化逻辑。

---

# 国际象棋大数据挖掘与可视化项目 (Chess Data Mining & Visualization)

## 1. 项目愿景与架构 (Project Context)

本项目旨在处理海量国际象棋 PGN 数据（如 Lichess 数据库），通过 C++ 进行高性能 ETL（抽取、转换、加载），并使用 Python 进行深度统计分析与可视化。

**核心架构：**
* **后端 (C++17)**: 负责解析 `.pgn` 文件，执行复杂的棋规判定（如过路兵、易位、升变），并维护棋子状态追踪（Tracking），最终输出轻量级的 JSON 统计文件。
* **前端 (Python)**: 读取 JSON 数据，使用 `matplotlib` / `seaborn` 绘制热力图、分布图，关注美学设计与数据可读性。

---

## 2. 核心 C++ 模块详解

项目包含三个主要的 C++ 分析器，其中 **`chess_death_map_v2.cpp`** 是目前最先进的版本。

### 2.1 空间死亡分析器 V2 (`chess_death_map_v2.cpp`)
**功能**：统计棋盘上每个格子（0-63）发生“死亡”事件的频率，并精确追踪每个具体的棋子（如“a列兵”、“白格象”）。

* **设计模式**：Visitor Pattern (`chess::pgn::Visitor`)。
* **核心组件**：
    1.  **`AdvancedTracker` 类**：
        * **目的**：不依赖 FEN 串，而是手动维护一个 `std::array<TrackedPiece, 64>`。
        * **功能**：给每个棋子分配唯一 ID（例如 `White_Pawn_a`, `Black_Knight_g`）。即便棋子移动，ID 也会跟随坐标更新。这解决了“区分中央兵和边路兵命运”的问题。
    2.  **手动 Move Logic (`process_move_logic`)**：
        * **技巧**：不完全信任 PGN 解析库的 Flag，而是手动复核 `Castling`（防误判为吃子）、`En Passant`（修正受害者坐标到被跨过的格子）和 `Promotion`。
        * **将杀统计 (Checkmate)**：在 `board.makeMove()` 后，若检测到 `inCheck()` 且无合法移动，则判定为**将杀**。此时，程序会找到**输家 King 的坐标**，记为 King 的“死亡位置”。
* **数据结构 (JSON Output)**：
    * 采用 `std::map<string, std::array<int, 64>>` 存储。
    * `detailed` 键包含细粒度数据：`"White_Pawn_a": [0, 0, 15, ...]`（64个整数）。

### 2.2 心理投降分析器 (`chessresign.cpp`)
**功能**：研究玩家在何种兵力差距（Material Diff）下会选择投降。

* **核心算法**：
    * **平滑窗口**：维护一个 `size=5` 的 `deque`，记录最近5步的兵力差均值，过滤掉兑子过程中的瞬间波动。
    * **兵力估值**：`P=1, N/B=3, R=5, Q=9`。**特殊加权**：即将升变的兵（Rank 7/2）权重 +2.0。
* **输出**：投降时的 {回合数, 兵力差} 分布矩阵。

### 2.3 战术击杀矩阵 (`chesskiller.cpp`)
**功能**：构建“谁杀死了谁”的稀疏矩阵（Attacker vs Victim）。

* **特点**：主要关注棋子间的互动关系，而非空间位置。支持断点续传（Checkpoint）。

---

## 3. JSON 数据接口约定 (Interface Specifications)

任何 C++ 模块生成的 JSON 必须遵循以下原则，以便 Python 脚本通用化处理：

1.  **扁平化棋盘 (Flat Board)**：
    * 棋盘数据必须是一个长度为 64 的整数数组 `[int; 64]`。
    * **索引映射**：`0` = `a1`, `7` = `h1`, `56` = `a8`, `63` = `h8` (Little-Endian Rank-File Mapping)。
2.  **键名规范**：
    * 格式：`{Color}_{PieceType}_{Suffix}`。
    * 示例：`White_Pawn_a`, `Black_Queen`, `White_King`。
    * Python 端通过字符串匹配解析这些键。

---

## 4. Python 可视化层详解

当前有一套 Python 脚本用于处理 `chess_death_map_v2.json`：

1.  **`visualize_beauty.py` (推荐)**：
    * **功能**：绘制“相对死亡率”图（Relative Death Rate）。
    * **算法**：$\text{Rate}_{i,j} = \frac{\text{Count}_{i,j}}{\sum \text{Count}}$。找出每个格子上 Rate 最高的兵种作为该格子的“领主”。
    * **技巧**：
        * **混合字体**：使用 `Microsoft YaHei` 显示中文标题，强制使用 `DejaVu Sans` 显示 Unicode 棋子符号 (♟, ♞)。
        * **透明度映射**：Alpha 通道与概率挂钩，概率越高颜色越深。
2.  **`visualize_full_army.py`**：
    * **功能**：生成 2x8 网格，分别展示白方/黑方 16 个棋子的独立热力图。
    * **逻辑**：分别解析 `_a`, `_b` 等后缀，还原棋子阵型。

---

## 5. 开发者指南 (Developer Guide)

如果我要继续添加功能，请遵循以下路径：

### 如何添加新的统计维度？
1.  **C++ 端**：
    * 修改 `GlobalStats` (或 `DeathStats`) 结构体，增加新的容器（如 `std::map<int, int> check_locations`）。
    * 在 `Visitor::move` 或 `process_move_logic` 中添加触发条件。
    * 在 `main` 函数的 JSON 序列化部分将新数据写入。
2.  **Python 端**：
    * 加载 JSON，使用 Pandas 或 NumPy 转换数据结构，然后绘图。

### 如何修改棋子追踪逻辑？
* 修改 `AdvancedTracker` 类。例如，如果你想追踪“后翼兵”和“王翼兵”的群体行为，可以在 `initialize_ids` 中修改 ID 分配规则。

### 常见陷阱 (Gotchas)
1.  **坐标翻转**：C++ 输出的数组通常索引 0 是 a1（底部），但 Matplotlib 绘制矩阵时索引 0 是顶部（Top-Left）。Python 脚本必须包含 `np.flipud(matrix.reshape(8,8))`。
2.  **字体缺失**：在 Windows 环境下绘制 Unicode 棋子符号必须显式指定 `fontname='DejaVu Sans'`，否则会显示方框。
3.  **吃过路兵 (EP)**：受害者不在目标格 `to_sq`，而是在 `(to_file, from_rank)`。C++ 代码必须包含此修正。

---

## 6. 下一步可能的改进 (Roadmap)

* **ELO 分段**：解析 PGN Header 中的 `WhiteElo`，将数据分为 `HighElo_Stats` 和 `LowElo_Stats`，对比新手和大师的死亡热力图区别。
* **时间维度**：目前的 map 丢失了时间。可以将其改为 `std::map<string, std::vector<int>>`（Index=Turn），分析“开局死得最多的位置” vs “残局死得最多的位置”。
* **引擎评估**：接入 Stockfish，统计“失误率热力图”（Blunder Heatmap）。

---

**给 AI 的指令**：
当你读取以上文本时，请假定我已经完成了上述所有 `v2` 版本的代码编写。你可以直接基于 `chess_death_map_v2.cpp` 和 `visualize_beauty.py` 的逻辑进行对话，无需重新询问基本架构。