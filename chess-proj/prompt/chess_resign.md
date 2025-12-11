# Chess Data Science Project: Context & Developer Guide

**致 AI 助手 / 开发者：**
你正在接手一个基于海量 PGN 数据（如 Lichess 数据库）的国际象棋数据科学分析项目。该项目旨在挖掘棋局背后的深层模式，目前包含两个核心分析模块（击杀关系、投降心理）和一个可视化模块。

以下是完整的项目架构、代码解析及扩展指南。

-----

## 1\. 项目架构 (System Architecture)

项目遵循 **ETL (Extract, Transform, Load)** 模式，但针对高性能需求进行了 C++ 优化。

  * **数据源 (Source)**: `.pgn` 格式的国际象棋对局文件（通常为几十 GB）。
  * **处理层 (Processing - C++)**:
      * 使用 `Disservin/chess-library` 进行 PGN 解析和棋局模拟。
      * 使用 `nlohmann/json` 进行数据序列化。
      * **模块 A (`chesskiller.cpp`)**: 关注微观战术——“谁杀死了谁”。
      * **模块 B (`chessresign.cpp`)**: 关注宏观心理——“在什么局势下投降”。
  * **存储层 (Storage)**: 结构化的 `.json` 文件（稀疏矩阵存储策略）。
  * **表现层 (Presentation - Python)**: Jupyter Notebook (`chess_killer_visualizer.ipynb`) 用于读取 JSON 并绘制热力图、分布曲线等。

-----

## 2\. 代码文件详解 (Codebase Analysis)

### 2.1. 核心分析器 A：`chesskiller.cpp`

**功能**：统计棋子之间的击杀关系（Kill Matrix）。它不只是统计“兵吃兵”，而是追踪“a列白兵”在“第20回合”吃掉了“c列黑马”。

  * **核心设计**：
      * **`TrackedPiece` 结构体**：为棋盘上的每个棋子分配唯一 ID（如 `White_Pawn_a`, `Black_Queen_Original`）。即使棋子移动，ID 保持不变；升变后 ID 会更新（如 `White_Queen_Promoted_from_a`）。
      * **`kill_matrix` (GlobalStats)**：
          * 结构：`Map<Attacker_ID, Map<Victim_ID, Map<Turn, Count>>>`。
          * 目的：构建一个高维稀疏矩阵，记录击杀者、受害者和时间的三元关系。
  * **实现技巧**：
      * **状态追踪**：在 `sanMove` 中，在执行 `makeMove` 之前，先识别移动的棋子 ID 和被吃掉的棋子 ID（如果有 capture）。
      * **断点续传**：定期将 `GlobalStats` 序列化为 JSON，记录文件流的 `offset`。

### 2.2. 核心分析器 B：`chessresign.cpp` (新建)

**功能**：研究“投降心理学”。统计玩家在投降（Resignation）时，当前的兵力差（Material Difference）和回合数（Turn Number）。

  * **核心设计**：
      * **`ResignationStats` 结构体**：
          * 存储两个主表：`white_resigned` 和 `black_resigned`。
          * 结构：`Map<Turn, Map<MaterialDiff_x10, Count>>`。
          * *注意*：`MaterialDiff` 被放大 10 倍存储为整数（Key），以避免浮点数作为 Map Key 的精度问题（如 `-3.5` 存为 `"-35"`）。
      * **`ResignationVisitor` 类**：继承自 `chess::pgn::Visitor`。
          * **`material_diff_window` (Deque)**：一个大小为 5 的滑动窗口，存储最近 5 步的兵力差。用于计算平均压力值，平滑掉瞬间的吃子交换。
  * **关键算法**：
      * **`calculate_material_score`**：
          * 标准分值：Q=9, R=5, B=3, N=3, P=1。
          * **进阶逻辑**：即将升变的兵（白兵 Rank 6，黑兵 Rank 1）价值提升为 **2.0**，反映其巨大的潜在威胁。
      * **`is_checkmate` 辅助函数**：库本身没有直接的 checkmate 检查，通过 `legal_moves.empty() && board.inCheck()` 实现。
  * **实现技巧**：
      * **原子化写入 (Atomic Write)**：在保存 Checkpoint 时，先写入 `.tmp` 文件，再 `rename`。防止程序在写入过程中崩溃导致存档损坏。
      * **文件流偏移 (Offset)**：利用 `pgn_stream.tellg()` 和 `seekg()` 实现秒级断点恢复。

### 2.3. 可视化工具：`chess_killer_visualizer.ipynb`

**功能**：读取 JSON 数据并生成图表。

  * **数据处理**：
      * 将 JSON 中的稀疏数据转换为 Pandas DataFrame 或 Numpy 数组。
      * 对于 `chessresign` 的数据，需要将 Key 除以 10 还原为真实兵力差。
  * **图表类型**：
      * 热力图 (Heatmap)：展示 X轴(回合) vs Y轴(兵力差) 的投降密度。
      * 生存/压力曲线：不同分段玩家的心理阈值对比（需扩展）。

-----

## 3\. 使用指南 (User Guide)

### 环境依赖

  * C++ 编译器 (支持 C++17, 如 g++ 9+ 或 MSVC)。
  * 依赖文件（需在同级目录）：
      * `chess.hpp` (Disservin/chess-library)
      * `json.hpp` (nlohmann/json)

### 编译命令

```bash
# 编译击杀分析器
g++ -O3 -std=c++17 chesskiller.cpp -o chesskiller

# 编译投降分析器
g++ -O3 -std=c++17 chessresign.cpp -o chessresign
```

### 运行

```bash
# 运行投降分析（支持断点续传，随时可 Ctrl+C 中止）
./chessresign
```

  * 输出文件位于 `output/` 目录。
  * 中间存档为 `checkpoint_*.json`，最终结果为 `final_*.json`。

-----

## 4\. 接口与扩展指南 (Interface & Extension)

如果你希望修改或增加功能，请参考以下“接口”逻辑：

### 场景 1：调整“心理压力”的计算公式 (`chessresign.cpp`)

  * **需求**：你想把“双象优势”计入兵力评分，或者调整滑动窗口大小。
  * **修改点**：
    1.  **窗口大小**：修改顶部的 `const int WINDOW_SIZE = 5;`。
    2.  **估值逻辑**：修改 `calculate_material_score` 函数。
          * *示例*：若要增加双象加分，检查 `board.pieces(BISHOP, color).count() >= 2`，然后 score `+= 0.5`。

### 场景 2：增加新的统计事件 (`chesskiller.cpp`)

  * **需求**：统计“造成将军（Check）”的次数。
  * **修改步骤**：
    1.  **数据结构**：在 `GlobalStats` 中新增 `std::map<std::string, int> check_counts;`。
    2.  **JSON 接口**：在 `to_json` 和 `from_json` 中添加对 `check_counts` 的序列化/反序列化代码。
    3.  **逻辑捕获**：在 `Visitor::sanMove` 中，移动后调用 `board.inCheck()`，若为真，获取当前移动方 ID 并自增计数。

### 场景 3：数据可视化适配

  * **注意**：Python 端读取 JSON 时，Keys 都是 **String** 类型。
      * 在 `chessresign` 中，Key `"-35"` 需要解析为 `int(-35) / 10.0`。
      * 在 `chesskiller` 中，Key 是棋子 ID 字符串。

-----

## 5\. 数据字典 (Data Dictionary)

### `final_resignation_stats.json`

| 字段 Key | 类型 | 说明 |
| :--- | :--- | :--- |
| `meta_info` | Object | 包含源文件路径、总局数、处理窗口大小等元数据。 |
| `white_resigned` | Object | 白方投降的数据集。 |
| `black_resigned` | Object | 黑方投降的数据集。 |
| └ `Turn (e.g. "30")` | Object | 第一层 Key：回合数。 |
|   └ `Diff (e.g. "-35")` | Int | 第二层 Key：(白分-黑分)\*10。Value：该情况发生的次数。 |

### `final_matrix.json` (Chess Killer)

| 字段 Key | 类型 | 说明 |
| :--- | :--- | :--- |
| `kill_matrix` | Object | 击杀矩阵。 |
| └ `Attacker_ID` | Object | 谁是凶手（如 `White_Queen`）。 |
|   └ `Victim_ID` | Object | 谁是死者（如 `Black_Pawn_e`）。 |
|     └ `Turn` | Int | 发生在第几回合 -\> 次数。 |

-----

这份文档概括了当前代码的所有智慧与逻辑。请基于此上下文继续你的工作。