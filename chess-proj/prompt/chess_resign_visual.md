这是一份完整的**项目技术文档与开发者指南**。

如果未来你需要重启本项目或将其移交给其他 AI/开发者，请将**下方的所有内容**连同源代码文件一起发送。这将确保接收方能够立刻理解项目的上下文、核心算法、数据结构设计以及扩展接口，从而无缝进行后续开发。

-----

# 国际象棋数据科学项目：深度模式挖掘 (Chess Data Science Project)

## 1\. 项目综述 (Project Overview)

本项目旨在处理海量国际象棋 PGN 数据（如 Lichess 数据库），挖掘高维度的博弈模式。项目采用 **C++17 (ETL 层)** + **Python (分析/可视化层)** 的混合架构，以解决 GB 级文本数据的性能瓶颈，同时保持数据分析的灵活性。

**核心功能模块：**

1.  **Chess Killer (`chesskiller.cpp`)**: 微观战术分析。追踪棋子的“前世今生”，构建稀疏击杀矩阵，统计“谁杀死了谁”。
2.  **Chess Resign (`chessresign.cpp`)**: 宏观心理分析。统计玩家在投降时的兵力差与回合数，结合高阶统计模型（非中心 t-分布）挖掘玩家的心理阈值。
3.  **Visualizer (Python)**: 读取 C++ 生成的 JSON 数据，进行生存分析、热力图绘制及分布拟合。

-----

## 2\. 模块详解：微观战术分析 (`chesskiller.cpp`)

### 2.1 功能与目的

该程序遍历 PGN 文件，记录每一对“击杀者 (Attacker)”与“受害者 (Victim)”之间的互动频率，并保留时间维度（Turn Index）。

  * **输入**: 标准 PGN 文件。
  * **输出**: `final_matrix_cpp.json`（包含 Kill Matrix 和 Death Timeline）。

### 2.2 核心数据结构

  * **`TrackedPiece`**: 使得每个棋子拥有唯一 ID（如 `White_Pawn_a`）。
      * *设计亮点*: 区分 `promoted` 状态。当兵升变为后时，ID 变更为 `White_Pawn_a_promoted`，从而能区分“原生后”与“升变后”的击杀行为。
  * **`GlobalStats`**:
      * 使用 `unordered_map<string, unordered_map<string, vector<int>>>` 存储稀疏矩阵。
      * *性能设计*: 第三层直接使用 `vector<int>` (Index = Turn Number) 而非 Map，因为回合数是连续且有限的 (0-150)，这极大减少了哈希开销。

### 2.3 关键实现技巧

1.  **高级状态追踪 (`AdvancedTracker`)**:
      * 不依赖 FEN 串，而是维护一个 `std::array<TrackedPiece, 64>` 映射。
      * **手动修正逻辑**: 在处理移动时，程序**不完全依赖**库返回的 Flag，而是手动复核 Move Type（如手动判定 King 跨越两格为易位、斜走且目标为空为吃过路兵）。这是为了容错处理某些非标准 PGN 或库的边缘情况。
2.  **断点续传 (Robustness)**:
      * 定期（`SAVE_INTERVAL`）将状态序列化为 JSON。
      * **Atomic Write**: 写入 `checkpoint.json.tmp` 后执行 `rename`，防止程序崩溃导致存档损坏。
3.  **IO 优化**:
      * `std::ios::sync_with_stdio(false)` 关闭同步。
      * 使用 1MB Buffer 的 `StreamParser`。

### 2.4 扩展接口 (How to Extend)

  * **添加新的统计事件**: 修改 `GlobalStats::commit_event` 和 `KillerVisitor::process_move_logic`。例如，若要统计“将军 (Check)”次数，需在 `process_move_logic` 尾部 `board.inCheck()` 为真时触发记录。

-----

## 3\. 模块详解：投降心理分析 (`chessresign.cpp`)

### 3.1 功能与目的

该程序专注于研究玩家在什么局势下会选择投降。它计算每一步的“兵力差 (Material Difference)”，并记录投降那一刻的快照。

### 3.2 核心算法

1.  **兵力估值 (`calculate_material_score`)**:
      * 基础分：P=1, N/B=3, R=5, Q=9。
      * **进阶加权**: 位于 Rank 6 (白) 或 Rank 1 (黑) 的兵，分值 +2.0。这反映了“即将升变”的巨大威胁，比单纯统计兵力更符合人类心理。
2.  **平滑窗口 (`material_diff_window`)**:
      * 维护一个大小为 5 的 `std::deque`。
      * *目的*: 过滤掉“吃子交换 (Exchange)”过程中的瞬间兵力波动。只有当兵力差稳定在劣势时投降，才被计入有效数据。
3.  **数据压缩技巧**:
      * Map Key 不支持浮点数。程序将 `diff * 10` 转为 `int` 作为 Key（如 `-3.5` 存为 `-35`），在 JSON 中保持精度且易于序列化。

### 3.3 扩展接口 (How to Extend)

  * **修改估值模型**: 直接修改 `calculate_material_score` 函数。例如，可以添加“双象优势 (+0.5)”或“王被裸露 (-1.0)”的逻辑。
  * **调整平滑度**: 修改 `const int WINDOW_SIZE = 5;`。

-----

## 4\. 数据分析与可视化 (Python Guidelines)

### 4.1 数据加载规范

生成的 JSON 文件采用稀疏结构，Python 端读取时需注意：

  * **Key 类型转换**: JSON 中的 Key 永远是 String。读取 `chessresign` 数据时，必须执行 `float(key) / 10.0` 还原兵力差。
  * **结构展平**: 建议加载后立即转化为 Pandas DataFrame (`columns=['Turn', 'Material_Diff', 'Count', 'Color']`) 以便后续处理。

### 4.2 统计拟合方法 (Best Practice)

在分析投降分布时，**强烈建议**不要使用正态分布，而是使用 **非中心 t-分布 (Non-central t-distribution, NCT)**。

  * **原因**: 玩家投降数据具有 **偏态 (Skewed)**（赢输心态不对称）和 **厚尾 (Heavy-tailed)**（存在极端顽固玩家）特征。
  * **代码参考**:
    ```python
    from scipy.stats import nct
    # p0 = [amp, df, nc, loc, scale]
    # df (自由度) < 10 证明存在厚尾
    # nc (非中心参数) != 0 证明存在偏态
    popt, _ = curve_fit(scaled_nct, x_data, y_data, p0=[...])
    ```

-----

## 5\. 编译与运行指南 (Build & Run)

### 依赖环境

  * C++17 编译器 (GCC 9+, Clang 10+, MSVC 2019+)
  * 头文件库 (需放在同级目录):
      * `chess.hpp` (Disservin/chess-library)
      * `json.hpp` (nlohmann/json)

### 编译命令

```bash
# 编译微观战术分析器
g++ -O3 -std=c++17 chesskiller.cpp -o chesskiller

# 编译投降心理分析器
g++ -O3 -std=c++17 chessresign.cpp -o chessresign
```

### 运行流程

1.  确保 `dataset/` 目录下有 `.pgn` 文件。
2.  运行 `./chesskiller` 或 `./chessresign`。
3.  程序会自动生成 `output/` 目录和 Checkpoint。
4.  运行结束后，使用提供的 Python 脚本读取 `final_*.json` 进行分析。

-----

## 6\. 未来改进方向 (Roadmap)

如果你接手此项目，以下是高价值的优化方向：

1.  **多线程并行 (Concurrency)**: 目前解析是单线程的。可以引入 OpenMP 或 `std::thread`，让每个线程处理 PGN 的不同 Chunk，最后合并 `GlobalStats`。
2.  **ELO 分段分析**: 目前未区分玩家水平。可以在 C++ 的 `Visitor::header` 中解析 `WhiteElo`/`BlackElo`，将数据分层（如 \<1500, 1500-2000, \>2000），这将揭示不同水平玩家完全不同的心理阈值。
3.  **引擎评估接入 (Stockfish)**: 目前使用静态兵力计算 (`material points`)。若能接入 Stockfish 库（或轻量级 NNUE），将兵力差替换为 `Centipawn Loss`，分析将更加精准，尽管这会显著降低处理速度。