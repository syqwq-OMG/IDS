#import "lib.typ": *

#show: doc.with(name: "孙育泉", id: 10234900421, title: [CHEnalySiS: 国际象棋数据分析])


#heading(level: 1, numbering: none)[
  摘要
]
#lorem(60)

= 介绍
== 研究背景
国际象棋（Chess）作为世界上最古老且最具策略性的博弈游戏之一，其复杂性不仅体现在天文数字般的状态空间复杂度，更体现在其中蕴含的丰富人类决策行为。随着在线对弈平台（如lichess, chess.com）的兴起，海量的对局记录以 `.pgn` 格式被保存下来。这些数据不仅仅是棋子移动的坐标序列，更是数百万玩家在时间压力、战术博弈和心理对抗下的决策快照。对于数据科学而言，这是一个研究高维博弈模式与人类心理阈值的绝佳方向。

== 问题陈述与动机
现有的国际象棋数据分析主要集中在两个极端：一是以 Stockfish 为代表的引擎评估，关注“什么是最佳着法”；二是以胜率统计为主的描述性分析，关注“谁赢了”。然而，在这两者之间存在着巨大的研究空白：“人类是如何互动的？”以及“人类在何种心理状态下会崩溃？”。

具体而言，目前缺乏对棋盘上“微观生态系统”的量化研究——即不同棋子角色（如兵、升变后的后、骑士）之间的捕食与被捕食关系是否遵循特定的稀疏矩阵分布？同时，在宏观层面上，玩家选择“投降”的决策往往是非理性的。它不仅仅取决于客观的兵力差，还受到对局进程和心理预期的非线性影响。传统的正态分布模型往往无法准确描述这种具有明显偏态和厚尾特征的投降行为。

== 研究目标与方法
本项目旨在建立一个混合架构的高性能数据分析流水线，从海量非结构化的PGN文本数据中挖掘深层次的博弈模式。为了解决GB级文本数据的处理瓶颈并保证分析的灵活性，本项目采用了 `C++17` 结合 `Python` 的异构架构。

本研究的核心贡献主要体现在以下两个维度：

- 微观战术分析：通过构建高性能的PGN解析器，设计了细粒度的棋子追踪算法。我们引入了“状态感知”机制，区分原生棋子与升变棋子，构建了基于时间维度的稀疏击杀矩阵，揭示了棋子间的动态克制关系。

- 宏观心理建模 ：我们提出了一种基于加权兵力估值和滑动窗口平滑的算法，用于提取玩家投降时的精确局势快照。针对投降数据的分布特征，创新性地引入了非中心t-分布进行拟合，从而量化了玩家在不同兵力劣势下的心理承受阈值。

= 数据集的选择与处理
本次分析所使用的数据集来源于 #link("https://database.lichess.org/")[lichess.org Open Database]。该数据库包含了自 2013 年以来，数百万玩家在 lichess 平台上的对局记录，涵盖了从初学者到国际大师的广泛水平。每场对局均以标准的 PGN格式存储，包含了完整的棋谱信息、玩家等级、对局时间等元数据。

以下展示一份具体的对局棋谱样例：

```pgn
[Event "Rated Bullet tournament https://lichess.org/tournament/yc1WW2Ox"]
[Site "https://lichess.org/PpwPOZMq"]
[Date "2017.04.01"]
[Round "-"]
[White "Abbot"]
[Black "Costello"]
[Result "0-1"]
[UTCDate "2017.04.01"]
[UTCTime "11:32:01"]
[WhiteElo "2100"]
[BlackElo "2000"]
[WhiteRatingDiff "-4"]
[BlackRatingDiff "+1"]
[WhiteTitle "FM"]
[ECO "B30"]
[Opening "Sicilian Defense: Old Sicilian"]
[TimeControl "300+0"]
[Termination "Time forfeit"]

1. e4 { [%eval 0.17] [%clk 0:00:30] } 1... c5 { [%eval 0.19] [%clk 0:00:30] }
2. Nf3 { [%eval 0.25] [%clk 0:00:29] } 2... Nc6 { [%eval 0.33] [%clk 0:00:30] }
3. Bc4 { [%eval -0.13] [%clk 0:00:28] } 3... e6 { [%eval -0.04] [%clk 0:00:30] }
4. c3 { [%eval -0.4] [%clk 0:00:27] } 4... b5? { [%eval 1.18] [%clk 0:00:30] }
5. Bb3?! { [%eval 0.21] [%clk 0:00:26] } 5... c4 { [%eval 0.32] [%clk 0:00:29] }
6. Bc2 { [%eval 0.2] [%clk 0:00:25] } 6... a5 { [%eval 0.6] [%clk 0:00:29] }
7. d4 { [%eval 0.29] [%clk 0:00:23] } 7... cxd3 { [%eval 0.6] [%clk 0:00:27] }
8. Qxd3 { [%eval 0.12] [%clk 0:00:22] } 8... Nf6 { [%eval 0.52] [%clk 0:00:26] }
9. e5 { [%eval 0.39] [%clk 0:00:21] } 9... Nd5 { [%eval 0.45] [%clk 0:00:25] }
10. Bg5?! { [%eval -0.44] [%clk 0:00:18] } 10... Qc7 { [%eval -0.12] [%clk 0:00:23] }
11. Nbd2?? { [%eval -3.15] [%clk 0:00:14] } 11... h6 { [%eval -2.99] [%clk 0:00:23] }
12. Bh4 { [%eval -3.0] [%clk 0:00:11] } 12... Ba6? { [%eval -0.12] [%clk 0:00:23] }
13. b3?? { [%eval -4.14] [%clk 0:00:02] } 13... Nf4? { [%eval -2.73] [%clk 0:00:21] } 0-1
```








#figure(
  table(
    columns: 7,
    align: (horizon,) * 7,
    rows: (auto, 2em),
    stroke: none,
    table.hline(y: 0, stroke: 2pt),
    table.hline(y: 1),
    table.hline(y: 2, stroke: 2pt),

    [*Piece*],
    [#chess-sym.pawn.black \ Pawn],
    [#chess-sym.knight.black \ Knight],
    [#chess-sym.bishop.black  \ Bishop],
    [#chess-sym.rook.black \ Rook],
    [#chess-sym.queen.black \ Queen],
    [#chess-sym.king.black \ King],
    [*Material Points*], [1], [3], [3], [5], [9], [∞],
  ),
  caption: [Chess Standard Material Points System],
)<tbl:material-points>


= 展望

以上工作就是本次数据分析得到的所有有趣的分析与结论，当然这也只是冰山一角，未来还有很多有趣的方向可以继续探索，但是由于时间、物理硬件以及算力上的限制，这些内容都无法在本次分析中展开，以下是一些未来值得继续进行探索的主题：


+ 本次数据分析只使用了 lichess 数据库中 2025.11 月的所有对局数据，并没有在时间维度上进行纵向的对比。未来这也可以成为一个值得探索的方向，例如：分析不同时间段宏观上棋手风格的变化，或者选出某些特定的棋手，分析其风格岁随时间的变化，或者 AI 引擎的迭代是否会对顶尖棋手的开局选择或者应对有影响。

+ 本次分析中，在对局面的评估上使用的是传统的 Chess Material Points 体系，虽然我们进行了一些评估体系的小调整和优化，但是这种体系本身由于是离散的，且没有具体考虑空间上棋子在棋盘上的布局，其评估结果在一定程度上不能真正反映某些选手应对局面时的压力，而仅仅是一种正相关的指标。未来对于局面，可以使用如 Stockfish 这类国际象棋引擎进行打分，从而得到更为准确的局面的评估，或许可以为选手风格或者棋手的心理变化、抗压能力等方面有更准确、具体的刻画。

= 参考资源

+ #link("https://database.lichess.org/")[lichess.org Open Database]：国际象棋开源对局数据库
+ #link("https://github.com/Disservin/chess-library")[Disservin/chess-library]：`C++` 棋谱解析库
+ #link("https://github.com/nlohmann/json")[nlohmann/json]：`C++` JSON 数据解析库
+ #link("https://en.wikipedia.org/wiki/Chess_piece_relative_value")[Chess piece relative value Wikipedia]

#v(18pt)

#align(center)[
  #set text(size: 14pt, weight: 800)
  Abstract
]

This report presents an analysis of international chess games using data from the lichess.org database. Various aspects of gameplay, player behavior, and strategic patterns are examined to derive insights into the dynamics of chess matches. The analysis includes visualizations such as heatmaps of piece movements and kill counts, providing a comprehensive overview of player strategies and tendencies.
