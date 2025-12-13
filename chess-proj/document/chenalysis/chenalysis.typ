#import "lib.typ": *

#show: doc.with(name: "孙育泉", id: 10234900421, title: [CHEnalySiS: 国际象棋数据分析])

#outline()

= 摘要

#lorem(200)


#chess-sym.bishop.white Bishop
#chess-sym.bishop.black Bishop

#figure(blue-board(starting-position, square-size: 18pt),caption: [111])

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


#figure(
  image("pic/death-place.png", width: 70%),
  caption: [国际象棋全兵种死亡位置热力图],
)
#lorem(100)
#figure(
  image("pic/kill-count-wb.png"),
  caption: [White Kills Black],
)

#figure(
  image("pic/kill-count-bw.png"),
  caption: [Black Kills White],
)


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
